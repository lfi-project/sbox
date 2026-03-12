#include "pbox_internal.h"
#include "pbox_seccomp.h"

#include <assert.h>
#include <dlfcn.h>
#include "sbox_dyfn.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>

// Global socket fd for fd passing (shared by all workers)
static int g_sock_fd;

// Thread-local storage for current channel (used by callback closures)
static __thread struct PBoxChannel* tls_current_channel = NULL;



// Called by assembly closure common handler.
// Extracts args from saved registers, signals host, returns result.
void sbox_dyfn_closure_dispatch(struct SboxDyfnClosureSavedRegs* saved,
                                struct SboxDyfnClosureResult* result) {
    struct SboxDyfnClosureInfo* info =
        &sbox_dyfn_closure_info[saved->stub_index];
    struct PBoxChannel* ch = tls_current_channel;

    if (!ch)
        return;

    ch->callback_id = info->callback_id;
    ch->nargs = info->nargs;

    // Extract args from saved registers using type info
    int int_idx = 0, float_idx = 0;
    size_t offset = 0;
    for (int i = 0; i < info->nargs; i++) {
        enum SboxDyfnClass cls = sbox_dyfn_classify(info->arg_types[i]);
        size_t size = sbox_dyfn_type_size(info->arg_types[i]);
        ch->args[i] = offset;
        if (cls == SBOX_DYFN_CLASS_FLOAT || cls == SBOX_DYFN_CLASS_DOUBLE)
            memcpy(&ch->arg_storage[offset], &saved->float_regs[float_idx++],
                   size);
        else
            memcpy(&ch->arg_storage[offset], &saved->int_regs[int_idx++],
                   size);
        offset += size;
    }

    // Signal callback to host
    pbox_set_state(&ch->state, PBOX_STATE_CALLBACK);

    // Wait for host to complete
    pbox_wait_for_state(&ch->state, PBOX_STATE_REQUEST);

    // Copy result back
    result->ret_class = sbox_dyfn_classify(info->ret_type);
    size_t ret_size = sbox_dyfn_type_size(info->ret_type);
    if (result->ret_class == SBOX_DYFN_CLASS_FLOAT ||
        result->ret_class == SBOX_DYFN_CLASS_DOUBLE)
        memcpy(&result->float_val, ch->result_storage, ret_size);
    else if (result->ret_class != SBOX_DYFN_CLASS_VOID)
        memcpy(&result->int_val, ch->result_storage, ret_size);
}

// Receive a file descriptor over a Unix socket
static int recv_fd(int sock_fd) {
    struct msghdr msg = {0};
    struct iovec iov;
    char buf[1];
    char cmsg_buf[CMSG_SPACE(sizeof(int))];

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(sock_fd, &msg, 0) < 0)
        return -1;

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS)
        return -1;

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

// Perform a dynamic function call
static bool do_ffi_call(struct PBoxChannel* ch) {
    _Static_assert(PBOX_MAX_ARGS * sizeof(uint64_t) <= PBOX_ARG_STORAGE,
                   "arg_storage too small for max args");

    void* arg_values[PBOX_MAX_ARGS];
    for (int i = 0; i < ch->nargs; i++) {
        assert(ch->args[i] < PBOX_ARG_STORAGE);
        arg_values[i] = &ch->arg_storage[ch->args[i]];
    }

    struct SboxDyfnCallArgs call;
    sbox_dyfn_prep_call(&call, (void*) (uintptr_t) ch->func_addr,
                        (enum SboxDyfnType) ch->ret_type, ch->nargs,
                        (const enum SboxDyfnType*) ch->arg_types, arg_values);

    struct SboxDyfnCallResult result;
    sbox_dyfn_call(&call, &result);

    sbox_dyfn_store_result(&result, (enum SboxDyfnType) ch->ret_type,
                           ch->result_storage);
    return true;
}

// Forward declaration
static void dispatch_loop(struct PBoxChannel* ch, bool is_control);

// Worker thread entry point
static void* worker_thread_fn(void* arg) {
    int shm_fd = (intptr_t) arg;

    // Map the channel
    struct PBoxChannel* ch =
        mmap(NULL, sizeof(struct PBoxChannel), PROT_READ | PROT_WRITE,
             MAP_SHARED, shm_fd, 0);
    if (ch == MAP_FAILED) {
        return NULL;
    }

    close(shm_fd);

    // Install worker seccomp filter that blocks clone
    // This prevents worker threads from spawning more threads
    if (pbox_install_seccomp_worker() < 0) {
        munmap(ch, sizeof(struct PBoxChannel));
        return NULL;
    }

    // Store channel address for host
    ch->sandbox_channel_addr = (uintptr_t) ch;

    // Run dispatch loop (not control channel)
    dispatch_loop(ch, false);

    munmap(ch, sizeof(struct PBoxChannel));
    return NULL;
}

// Spawn a new worker thread for a channel
// Called by host via control channel
int pbox_spawn_worker(int shm_fd) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker_thread_fn,
                       (void*) (intptr_t) shm_fd) != 0) {
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

// Main dispatch loop - handles requests until EXIT state
static void dispatch_loop(struct PBoxChannel* ch, bool is_control) {
    // Set TLS for callback closures
    tls_current_channel = ch;
    sbox_dyfn_closure_free_all();

    while (1) {
        // Wait for a request (or exit signal)
        while (1) {
            int state = atomic_load(&ch->state);
            if (state == PBOX_STATE_REQUEST)
                break;
            if (state == PBOX_STATE_EXIT) {
                sbox_dyfn_closure_free_all();
                return;
            }
            pbox_futex_wait(&ch->state, state);
        }

        // Dispatch based on request type
        switch (ch->request_type) {
            case PBOX_REQ_DLSYM: {
                ch->symbol_name[PBOX_MAX_SYMBOL_NAME - 1] = '\0';
                void* sym = dlsym(RTLD_DEFAULT, ch->symbol_name);
                ch->symbol_addr = (uintptr_t) sym;
                break;
            }
            case PBOX_REQ_CALL: {
                bool ok = do_ffi_call(ch);
                if (!ok) {
                    fprintf(stderr, "pbox: ffi call failed\n");
                }
                break;
            }
            case PBOX_REQ_RECV_FD:
                ch->received_fd = recv_fd(g_sock_fd);
                break;
            case PBOX_REQ_SPAWN_WORKER:
                // Only control channel can spawn workers
                if (is_control) {
                    pbox_spawn_worker(ch->worker_shm_fd);
                }
                break;
            case PBOX_REQ_CREATE_CLOSURE: {
                void* stub = sbox_dyfn_closure_alloc(
                    ch->closure_callback_id,
                    (enum SboxDyfnType) ch->closure_ret_type,
                    ch->closure_nargs,
                    (const enum SboxDyfnType*) ch->closure_arg_types);
                ch->closure_addr = (uintptr_t) stub;
                break;
            }
            default:
                assert(!"unhandled request_type");
                break;
        }

        // Signal response ready
        pbox_set_state(&ch->state, PBOX_STATE_RESPONSE);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <shm_fd> <sock_fd>\n", argv[0]);
        return 1;
    }

    int shm_fd = atoi(argv[1]);
    g_sock_fd = atoi(argv[2]);

    // Map the shared memory (control channel)
    struct PBoxChannel* channel =
        mmap(NULL, sizeof(struct PBoxChannel), PROT_READ | PROT_WRITE,
             MAP_SHARED, shm_fd, 0);
    if (channel == MAP_FAILED) {
        perror("pbox_sandbox: mmap");
        return 1;
    }

    close(shm_fd);

    // Install seccomp filter before entering the main loop
    // This restricts syscalls to memory/threading operations only
    if (pbox_install_seccomp() < 0) {
        perror("pbox_sandbox: seccomp");
        return 1;
    }

    // Store channel address so host can compute mem_storage address.
    channel->sandbox_channel_addr = (uintptr_t) channel;

    // Run dispatch loop (this is the control channel)
    dispatch_loop(channel, true);

    munmap(channel, sizeof(struct PBoxChannel));
    return 0;
}
