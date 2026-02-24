#include "pbox_internal.h"
#include "pbox_seccomp.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ffi.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <dlfcn.h>

// Global socket fd for fd passing (shared by all workers)
static int g_sock_fd;

// Thread-local storage for current channel (used by callback closures)
static __thread struct PBoxChannel *tls_current_channel = NULL;

// Track closures for cleanup
struct ClosureInfo {
    ffi_closure *closure;
    ffi_cif *cif;
    ffi_type **arg_types;
};

static __thread struct ClosureInfo tls_closures[PBOX_MAX_CLOSURES];
static __thread int tls_closure_count = 0;

static void free_all_closures(void) {
    for (int i = 0; i < tls_closure_count; i++) {
        ffi_closure_free(tls_closures[i].closure);
        free(tls_closures[i].cif);
        free(tls_closures[i].arg_types);
    }
    tls_closure_count = 0;
}

// Map pbox type codes to libffi types
static ffi_type *get_ffi_type(int type_code) {
    switch (type_code) {
        case PBOX_TYPE_VOID:    return &ffi_type_void;
        case PBOX_TYPE_UINT8:   return &ffi_type_uint8;
        case PBOX_TYPE_SINT8:   return &ffi_type_sint8;
        case PBOX_TYPE_UINT16:  return &ffi_type_uint16;
        case PBOX_TYPE_SINT16:  return &ffi_type_sint16;
        case PBOX_TYPE_UINT32:  return &ffi_type_uint32;
        case PBOX_TYPE_SINT32:  return &ffi_type_sint32;
        case PBOX_TYPE_UINT64:  return &ffi_type_uint64;
        case PBOX_TYPE_SINT64:  return &ffi_type_sint64;
        case PBOX_TYPE_FLOAT:   return &ffi_type_float;
        case PBOX_TYPE_DOUBLE:  return &ffi_type_double;
        case PBOX_TYPE_POINTER: return &ffi_type_pointer;
        default:                return &ffi_type_void;
    }
}

// Closure handler - invoked by libffi when sandbox calls a callback stub
static void closure_handler(ffi_cif *cif, void *ret, void **args, void *user_data) {
    int callback_id = (int)(uintptr_t)user_data;
    struct PBoxChannel *ch = tls_current_channel;

    if (!ch) {
        // Should not happen - callback called outside of pbox_call context
        return;
    }

    // Pack callback request into channel
    ch->callback_id = callback_id;
    ch->nargs = cif->nargs;
    ch->ret_type = PBOX_TYPE_VOID;  // Will be set from stored types

    size_t offset = 0;
    for (unsigned i = 0; i < cif->nargs; i++) {
        size_t size = cif->arg_types[i]->size;
        ch->args[i] = offset;
        memcpy(&ch->arg_storage[offset], args[i], size);
        offset += size;
    }

    // Signal callback to host
    pbox_set_state(&ch->state, PBOX_STATE_CALLBACK);

    // Wait for host to complete
    pbox_wait_for_state(&ch->state, PBOX_STATE_REQUEST);

    // Copy result back
    if (cif->rtype != &ffi_type_void && ret != NULL)
        memcpy(ret, ch->result_storage, cif->rtype->size);
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

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
        return -1;

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

// Perform a dynamic function call using libffi
static bool do_ffi_call(struct PBoxChannel *ch) {
    ffi_cif cif;
    ffi_type *arg_types[PBOX_MAX_ARGS];
    void *arg_values[PBOX_MAX_ARGS];

    // Build argument type array and value pointers
    for (int i = 0; i < ch->nargs; i++) {
        arg_types[i] = get_ffi_type(ch->arg_types[i]);
        // TODO: bounds check this.
        arg_values[i] = &ch->arg_storage[ch->args[i]];
    }

    // Prepare the call interface
    ffi_type *ret_type = get_ffi_type(ch->ret_type);
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, ch->nargs, ret_type, arg_types) != FFI_OK) {
        return false;
    }

    ffi_call(&cif, (void (*)(void))(uintptr_t)ch->func_addr, ch->result_storage, arg_values);

    return true;
}

// Forward declaration
static void dispatch_loop(struct PBoxChannel *ch, bool is_control);

// Worker thread entry point
static void *worker_thread_fn(void *arg) {
    int shm_fd = (intptr_t)arg;

    // Map the channel
    struct PBoxChannel *ch = mmap(NULL, sizeof(struct PBoxChannel),
                                  PROT_READ | PROT_WRITE,
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
    ch->sandbox_channel_addr = (uintptr_t)ch;

    // Run dispatch loop (not control channel)
    dispatch_loop(ch, false);

    munmap(ch, sizeof(struct PBoxChannel));
    return NULL;
}

// Spawn a new worker thread for a channel
// Called by host via control channel
int pbox_spawn_worker(int shm_fd) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker_thread_fn, (void *)(intptr_t)shm_fd) != 0) {
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

// Main dispatch loop - handles requests until EXIT state
static void dispatch_loop(struct PBoxChannel *ch, bool is_control) {
    // Set TLS for callback closures
    tls_current_channel = ch;
    tls_closure_count = 0;

    while (1) {
        // Wait for a request (or exit signal)
        while (1) {
            int state = atomic_load(&ch->state);
            if (state == PBOX_STATE_REQUEST)
                break;
            if (state == PBOX_STATE_EXIT) {
                free_all_closures();
                return;
            }
            pbox_futex_wait(&ch->state, state);
        }

        // Dispatch based on request type
        switch (ch->request_type) {
            case PBOX_REQ_DLSYM: {
                ch->symbol_name[PBOX_MAX_SYMBOL_NAME - 1] = '\0';
                void *sym = dlsym(RTLD_DEFAULT, ch->symbol_name);
                ch->symbol_addr = (uintptr_t)sym;
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
                // Check closure limit
                if (tls_closure_count >= PBOX_MAX_CLOSURES) {
                    ch->closure_addr = 0;
                    break;
                }

                // Allocate closure using libffi
                void *closure_mem;
                ffi_closure *closure = ffi_closure_alloc(sizeof(ffi_closure), &closure_mem);
                if (!closure) {
                    ch->closure_addr = 0;
                    break;
                }

                // Build ffi_cif for this signature (must persist for closure lifetime)
                ffi_cif *cif = malloc(sizeof(ffi_cif));
                ffi_type **arg_types = NULL;
                if (ch->closure_nargs > 0) {
                    arg_types = malloc(ch->closure_nargs * sizeof(ffi_type*));
                    for (int i = 0; i < ch->closure_nargs; i++)
                        arg_types[i] = get_ffi_type(ch->closure_arg_types[i]);
                }

                ffi_type *ret_type = get_ffi_type(ch->closure_ret_type);
                if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, ch->closure_nargs, ret_type, arg_types) != FFI_OK) {
                    ffi_closure_free(closure);
                    free(cif);
                    free(arg_types);
                    ch->closure_addr = 0;
                    break;
                }

                // Create closure with callback_id as user_data
                if (ffi_prep_closure_loc(closure, cif, closure_handler,
                                         (void*)(uintptr_t)ch->closure_callback_id,
                                         closure_mem) != FFI_OK) {
                    ffi_closure_free(closure);
                    free(cif);
                    free(arg_types);
                    ch->closure_addr = 0;
                    break;
                }

                // Track for cleanup
                tls_closures[tls_closure_count].closure = closure;
                tls_closures[tls_closure_count].cif = cif;
                tls_closures[tls_closure_count].arg_types = arg_types;
                tls_closure_count++;

                ch->closure_addr = (uintptr_t)closure_mem;
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <shm_fd> <sock_fd>\n", argv[0]);
        return 1;
    }

    int shm_fd = atoi(argv[1]);
    g_sock_fd = atoi(argv[2]);

    // Map the shared memory (control channel)
    struct PBoxChannel *channel = mmap(NULL, sizeof(struct PBoxChannel),
                                       PROT_READ | PROT_WRITE,
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
    channel->sandbox_channel_addr = (uintptr_t)channel;

    // Run dispatch loop (this is the control channel)
    dispatch_loop(channel, true);

    munmap(channel, sizeof(struct PBoxChannel));
    return 0;
}
