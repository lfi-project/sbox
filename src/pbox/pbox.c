#define _GNU_SOURCE

#include "pbox.h"

#include "pbox_internal.h"
#include "pbox_procmaps.h"

#include <assert.h>
#include <fcntl.h>
#include <ffi.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PBOX_FD_DIRECT_MAX 128
#define PBOX_MAX_CALLBACKS 64

struct PBoxCallback {
    void* func_ptr;  // Host function pointer
    enum PBoxType ret_type;
    int nargs;
    enum PBoxType arg_types[PBOX_MAX_ARGS];
    void* sandbox_closure;                   // Sandbox closure address
    ffi_cif cif;                             // Cached call interface
    ffi_type* ffi_arg_types[PBOX_MAX_ARGS];  // Cached ffi types for cif
};

struct PBoxFdEntry {
    int host_fd;
    int sandbox_fd;
};

struct PBoxThreadChannel {
    struct PBoxChannel* channel;
    int shm_fd;
    struct PBox* box;  // Back-pointer for destructor

    // Identity-mapped arena
    void* idmem_base;
    size_t idmem_size;
    size_t idmem_offset;
};

struct PBox {
    // Control channel (channel 0)
    struct PBoxChannel* control_channel;
    int control_shm_fd;

    pid_t pid;
    int sock_fd;  // Unix socket for fd passing
    pthread_t watcher_thread;

    // Thread-local channel support
    pthread_key_t channel_key;
    pthread_mutex_t channel_lock;

    // Per-thread channels (dynamically allocated)
    struct PBoxThreadChannel** channels;
    size_t channel_count;
    size_t channel_cap;

    // Cached symbols
    void* sym_malloc;
    void* sym_calloc;
    void* sym_realloc;
    void* sym_free;
    void* sym_mmap;
    void* sym_munmap;
    void* sym_memcpy;
    void* sym_close;

    // Fd mapping: direct table for small fds, dynamic vector for large fds
    pthread_mutex_t fd_lock;
    int fd_direct[PBOX_FD_DIRECT_MAX];  // -1 = not mapped
    struct PBoxFdEntry* fd_overflow;
    size_t fd_overflow_count;
    size_t fd_overflow_cap;

    // Callback registry
    pthread_mutex_t callback_lock;
    struct PBoxCallback callbacks[PBOX_MAX_CALLBACKS];
    atomic_int callback_count;

    // Set when intentionally destroying (suppresses signal message)
    atomic_int destroying;
};

static void* watcher_thread_fn(void* arg) {
    struct PBox* box = arg;
    int status;
    waitpid(box->pid, &status, 0);

    if (!atomic_load(&box->destroying)) {
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            fprintf(stderr, "pbox: sandbox killed by signal %d", sig);
            if (sig == SIGSYS)
                fprintf(stderr, " (seccomp violation)");
            fprintf(stderr, "\n");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "pbox: sandbox exited with status %d\n",
                    WEXITSTATUS(status));
        }
    }

    pbox_set_state(&box->control_channel->state, PBOX_STATE_DEAD);
    return NULL;
}

// TLS destructor - called when a host thread exits
static void channel_destructor(void* ptr) {
    if (!ptr)
        return;

    struct PBoxThreadChannel* tch = ptr;
    struct PBox* box = tch->box;

    // Signal the sandbox worker to exit
    pbox_set_state(&tch->channel->state, PBOX_STATE_EXIT);

    // Unmap host side of identity region only. The worker was just told
    // to exit, so we can't send further requests on this channel.
    // Using pbox_munmap_identity here would create a throwaway channel
    // and could hang if the sandbox is unresponsive. The sandbox will
    // clean up its own mappings when it exits.
    if (tch->idmem_base)
        munmap(tch->idmem_base, tch->idmem_size);

    // Unmap and close
    munmap(tch->channel, sizeof(struct PBoxChannel));
    close(tch->shm_fd);

    // Remove from channels list
    pthread_mutex_lock(&box->channel_lock);
    for (size_t i = 0; i < box->channel_count; i++) {
        if (box->channels[i] == tch) {
            box->channels[i] = box->channels[--box->channel_count];
            break;
        }
    }
    pthread_mutex_unlock(&box->channel_lock);

    free(tch);
}

// Forward declarations
static int pbox_send_fd_on_channel(struct PBox* box, struct PBoxChannel* ch,
                                   int fd);
static void* pbox_dlsym_control(struct PBox* box, const char* symbol);

// Create a new worker channel (must hold channel_lock)
static struct PBoxThreadChannel* create_channel_locked(struct PBox* box) {
    // Create shared memory for new channel
    int shm_fd = memfd_create("pbox_worker", MFD_CLOEXEC);
    if (shm_fd < 0) {
        return NULL;
    }

    if (ftruncate(shm_fd, sizeof(struct PBoxChannel)) < 0) {
        close(shm_fd);
        return NULL;
    }

    struct PBoxChannel* ch =
        mmap(NULL, sizeof(struct PBoxChannel), PROT_READ | PROT_WRITE,
             MAP_SHARED, shm_fd, 0);
    if (ch == MAP_FAILED) {
        close(shm_fd);
        return NULL;
    }

    atomic_store(&ch->state, PBOX_STATE_IDLE);

    // Send shm_fd to sandbox via control channel
    int sandbox_shm_fd =
        pbox_send_fd_on_channel(box, box->control_channel, shm_fd);
    if (sandbox_shm_fd < 0) {
        munmap(ch, sizeof(struct PBoxChannel));
        close(shm_fd);
        return NULL;
    }

    // Spawn worker thread in sandbox via control channel
    struct PBoxChannel* ctrl = box->control_channel;
    ctrl->request_type = PBOX_REQ_SPAWN_WORKER;
    ctrl->worker_shm_fd = sandbox_shm_fd;

    pbox_set_state(&ctrl->state, PBOX_STATE_REQUEST);
    pbox_wait_for_state(&ctrl->state, PBOX_STATE_RESPONSE);
    atomic_store(&ctrl->state, PBOX_STATE_IDLE);

    // Wait for worker to set sandbox_channel_addr (indicates it's ready)
    while (ch->sandbox_channel_addr == 0) {
        PAUSE();
    }

    // Allocate thread channel struct
    struct PBoxThreadChannel* tch = malloc(sizeof(struct PBoxThreadChannel));
    if (!tch) {
        pbox_set_state(&ch->state, PBOX_STATE_EXIT);
        munmap(ch, sizeof(struct PBoxChannel));
        close(shm_fd);
        return NULL;
    }

    tch->channel = ch;
    tch->shm_fd = shm_fd;
    tch->box = box;

    // Initialize identity-mapped arena (non-fatal if it fails)
    tch->idmem_base = NULL;
    tch->idmem_size = 0;
    tch->idmem_offset = 0;

    // Add to channels list
    if (box->channel_count >= box->channel_cap) {
        size_t new_cap = box->channel_cap ? box->channel_cap * 2 : 4;
        struct PBoxThreadChannel** new_channels =
            realloc(box->channels, new_cap * sizeof(struct PBoxThreadChannel*));
        if (!new_channels) {
            pbox_set_state(&ch->state, PBOX_STATE_EXIT);
            munmap(ch, sizeof(struct PBoxChannel));
            close(shm_fd);
            free(tch);
            return NULL;
        }
        box->channels = new_channels;
        box->channel_cap = new_cap;
    }
    box->channels[box->channel_count++] = tch;

    return tch;
}

// Get or create thread-local channel
static struct PBoxChannel* get_or_create_channel(struct PBox* box) {
    struct PBoxThreadChannel* tch = pthread_getspecific(box->channel_key);
    if (tch) {
        return tch->channel;
    }

    // Need to create a new channel - lock protects channels list and control
    // channel
    pthread_mutex_lock(&box->channel_lock);
    tch = create_channel_locked(box);
    pthread_mutex_unlock(&box->channel_lock);

    if (!tch) {
        return NULL;
    }

    pthread_setspecific(box->channel_key, tch);
    return tch->channel;
}

struct PBox* pbox_create(const char* sandbox_executable) {
    struct PBox* box = malloc(sizeof(struct PBox));
    if (!box) {
        return NULL;
    }

    atomic_init(&box->destroying, 0);

    // Initialize fd mapping.
    if (pthread_mutex_init(&box->fd_lock, NULL) != 0) {
        perror("pbox: pthread_mutex_init");
        free(box);
        return NULL;
    }
    for (int i = 0; i < PBOX_FD_DIRECT_MAX; i++)
        box->fd_direct[i] = -1;
    box->fd_overflow = NULL;
    box->fd_overflow_count = 0;
    box->fd_overflow_cap = 0;

    // Initialize callback registry.
    if (pthread_mutex_init(&box->callback_lock, NULL) != 0) {
        perror("pbox: pthread_mutex_init");
        pthread_mutex_destroy(&box->fd_lock);
        free(box);
        return NULL;
    }
    atomic_init(&box->callback_count, 0);

    // Initialize channel list.
    box->channels = NULL;
    box->channel_count = 0;
    box->channel_cap = 0;

    // Initialize TLS key for per-thread channels.
    if (pthread_key_create(&box->channel_key, channel_destructor) != 0) {
        perror("pbox: pthread_key_create");
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        free(box);
        return NULL;
    }

    // Initialize mutex for channel creation.
    if (pthread_mutex_init(&box->channel_lock, NULL) != 0) {
        perror("pbox: pthread_mutex_init");
        pthread_key_delete(box->channel_key);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        free(box);
        return NULL;
    }

    // Create anonymous shared memory for control channel.
    box->control_shm_fd = memfd_create("pbox_control", MFD_CLOEXEC);
    if (box->control_shm_fd < 0) {
        perror("pbox: memfd_create");
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    // Size the shared memory.
    if (ftruncate(box->control_shm_fd, sizeof(struct PBoxChannel)) < 0) {
        perror("pbox: ftruncate");
        close(box->control_shm_fd);
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    // Map shared memory.
    box->control_channel =
        mmap(NULL, sizeof(struct PBoxChannel), PROT_READ | PROT_WRITE,
             MAP_SHARED, box->control_shm_fd, 0);
    if (box->control_channel == MAP_FAILED) {
        perror("pbox: mmap");
        close(box->control_shm_fd);
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    // Initialize control channel.
    atomic_store(&box->control_channel->state, PBOX_STATE_IDLE);

    // Create socket pair for fd passing.
    int sock_fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sock_fds) < 0) {
        perror("pbox: socketpair");
        munmap(box->control_channel, sizeof(struct PBoxChannel));
        close(box->control_shm_fd);
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    // Fork and exec the sandbox process.
    box->pid = fork();
    if (box->pid < 0) {
        perror("pbox: fork");
        munmap(box->control_channel, sizeof(struct PBoxChannel));
        close(box->control_shm_fd);
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    if (box->pid == 0) {
        // Child process.
        // Mark all FDs >= 3 as close-on-exec to prevent leaking host FDs,
        // then clear close-on-exec on the two FDs we need to pass.
        close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);
        fcntl(box->control_shm_fd, F_SETFD, 0);
        fcntl(sock_fds[1], F_SETFD, 0);

        char fd_str[16], sock_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", box->control_shm_fd);
        snprintf(sock_str, sizeof(sock_str), "%d", sock_fds[1]);
        execl(sandbox_executable, sandbox_executable, fd_str, sock_str, NULL);
        perror("pbox: execl");
        exit(1);
    }

    // Parent: close child's end, keep ours
    close(sock_fds[1]);
    box->sock_fd = sock_fds[0];

    // Start watcher thread to detect sandbox death.
    if (pthread_create(&box->watcher_thread, NULL, watcher_thread_fn, box) !=
        0) {
        perror("pbox: pthread_create");
        kill(box->pid, SIGKILL);
        waitpid(box->pid, NULL, 0);
        munmap(box->control_channel, sizeof(struct PBoxChannel));
        close(box->control_shm_fd);
        close(box->sock_fd);
        pthread_mutex_destroy(&box->channel_lock);
        pthread_mutex_destroy(&box->callback_lock);
        pthread_mutex_destroy(&box->fd_lock);
        pthread_key_delete(box->channel_key);
        free(box);
        return NULL;
    }

    // Cache common symbols (use control channel for initial lookups).
    pthread_mutex_lock(&box->channel_lock);
    box->sym_malloc = pbox_dlsym_control(box, "malloc");
    box->sym_calloc = pbox_dlsym_control(box, "calloc");
    box->sym_realloc = pbox_dlsym_control(box, "realloc");
    box->sym_free = pbox_dlsym_control(box, "free");
    box->sym_mmap = pbox_dlsym_control(box, "mmap");
    box->sym_munmap = pbox_dlsym_control(box, "munmap");
    box->sym_memcpy = pbox_dlsym_control(box, "memcpy");
    box->sym_close = pbox_dlsym_control(box, "close");
    pthread_mutex_unlock(&box->channel_lock);

    return box;
}

void pbox_destroy(struct PBox* box) {
    // Kill the sandbox process.
    atomic_store(&box->destroying, 1);
    kill(box->pid, SIGKILL);

    // Wait for watcher thread (which waits for child).
    pthread_join(box->watcher_thread, NULL);

    // Clean up worker channel resources.
    pthread_mutex_lock(&box->channel_lock);
    for (size_t i = 0; i < box->channel_count; i++) {
        struct PBoxThreadChannel* tch = box->channels[i];
        // Only unmap host side - sandbox is already dead
        if (tch->idmem_base)
            munmap(tch->idmem_base, tch->idmem_size);
        munmap(tch->channel, sizeof(struct PBoxChannel));
        close(tch->shm_fd);
        free(tch);
    }
    free(box->channels);
    box->channels = NULL;
    box->channel_count = 0;
    pthread_mutex_unlock(&box->channel_lock);

    munmap(box->control_channel, sizeof(struct PBoxChannel));
    close(box->control_shm_fd);
    close(box->sock_fd);

    pthread_mutex_destroy(&box->channel_lock);
    pthread_mutex_destroy(&box->callback_lock);
    pthread_mutex_destroy(&box->fd_lock);
    pthread_key_delete(box->channel_key);

    free(box->fd_overflow);
    free(box);
}

pid_t pbox_pid(const struct PBox* box) {
    return box->pid;
}

int pbox_alive(const struct PBox* box) {
    return atomic_load(&box->control_channel->state) != PBOX_STATE_DEAD;
}

// Internal: dlsym using control channel (must hold channel_lock)
static void* pbox_dlsym_control(struct PBox* box, const char* symbol) {
    struct PBoxChannel* ch = box->control_channel;

    ch->request_type = PBOX_REQ_DLSYM;
    strncpy(ch->symbol_name, symbol, PBOX_MAX_SYMBOL_NAME - 1);
    ch->symbol_name[PBOX_MAX_SYMBOL_NAME - 1] = '\0';

    pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
    pbox_wait_for_state(&ch->state, PBOX_STATE_RESPONSE);
    atomic_store(&ch->state, PBOX_STATE_IDLE);

    return (void*) ch->symbol_addr;
}

void* pbox_dlsym(struct PBox* box, const char* symbol) {
    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch)
        return NULL;

    ch->request_type = PBOX_REQ_DLSYM;
    strncpy(ch->symbol_name, symbol, PBOX_MAX_SYMBOL_NAME - 1);
    ch->symbol_name[PBOX_MAX_SYMBOL_NAME - 1] = '\0';

    pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
    pbox_wait_for_state(&ch->state, PBOX_STATE_RESPONSE);
    atomic_store(&ch->state, PBOX_STATE_IDLE);

    return (void*) ch->symbol_addr;
}

static size_t pbox_type_size(enum PBoxType type) {
    switch (type) {
        case PBOX_TYPE_VOID:
            return 0;
        case PBOX_TYPE_UINT8:
            return sizeof(uint8_t);
        case PBOX_TYPE_SINT8:
            return sizeof(int8_t);
        case PBOX_TYPE_UINT16:
            return sizeof(uint16_t);
        case PBOX_TYPE_SINT16:
            return sizeof(int16_t);
        case PBOX_TYPE_UINT32:
            return sizeof(uint32_t);
        case PBOX_TYPE_SINT32:
            return sizeof(int32_t);
        case PBOX_TYPE_UINT64:
            return sizeof(uint64_t);
        case PBOX_TYPE_SINT64:
            return sizeof(int64_t);
        case PBOX_TYPE_FLOAT:
            return sizeof(float);
        case PBOX_TYPE_DOUBLE:
            return sizeof(double);
        case PBOX_TYPE_POINTER:
            return sizeof(void*);
        default:
            return 0;
    }
}

static ffi_type* pbox_get_ffi_type(enum PBoxType type) {
    switch (type) {
        case PBOX_TYPE_VOID:
            return &ffi_type_void;
        case PBOX_TYPE_UINT8:
            return &ffi_type_uint8;
        case PBOX_TYPE_SINT8:
            return &ffi_type_sint8;
        case PBOX_TYPE_UINT16:
            return &ffi_type_uint16;
        case PBOX_TYPE_SINT16:
            return &ffi_type_sint16;
        case PBOX_TYPE_UINT32:
            return &ffi_type_uint32;
        case PBOX_TYPE_SINT32:
            return &ffi_type_sint32;
        case PBOX_TYPE_UINT64:
            return &ffi_type_uint64;
        case PBOX_TYPE_SINT64:
            return &ffi_type_sint64;
        case PBOX_TYPE_FLOAT:
            return &ffi_type_float;
        case PBOX_TYPE_DOUBLE:
            return &ffi_type_double;
        case PBOX_TYPE_POINTER:
            return &ffi_type_pointer;
        default:
            return &ffi_type_void;
    }
}

// Dispatch a callback request from sandbox to host
static void pbox_dispatch_callback(struct PBox* box, struct PBoxChannel* ch) {
    int id = ch->callback_id;
    if (id < 0 || id >= atomic_load(&box->callback_count))
        return;

    struct PBoxCallback* cb = &box->callbacks[id];

    // Unpack arguments from channel.
    // Offsets are sandbox-controlled; bounds-check to prevent out-of-bounds
    // reads on the host. Callback functions are still responsible for
    // validating argument values (e.g., pointers) from the untrusted sandbox.
    void* arg_values[PBOX_MAX_ARGS];
    for (int i = 0; i < cb->nargs; i++) {
        // Read offset once into a local to prevent TOCTOU -- the sandbox
        // could race to change ch->args[i] between the bounds check and use.
        uint64_t arg_offset = ch->args[i];
        if (arg_offset >= PBOX_ARG_STORAGE) {
            fprintf(stderr, "pbox: sandbox violated callback protocol\n");
            kill(box->pid, SIGKILL);
            return;
        }
        arg_values[i] = &ch->arg_storage[arg_offset];
    }

    // Call host function using cached cif
    ffi_call(&cb->cif, cb->func_ptr, ch->result_storage, arg_values);
}

// Wait for response, handling callbacks from sandbox
static void pbox_wait_for_response(struct PBox* box, struct PBoxChannel* ch) {
    while (1) {
        int state = atomic_load(&ch->state);

        if (state == PBOX_STATE_RESPONSE)
            return;

        if (state == PBOX_STATE_CALLBACK) {
            pbox_dispatch_callback(box, ch);
            pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
            continue;
        }

        if (state == PBOX_STATE_DEAD)
            return;

        pbox_futex_wait(&ch->state, state);
    }
}

void pbox_call(struct PBox* box, void* func_addr, enum PBoxType ret_type,
               int nargs, const enum PBoxType* arg_types, void** args,
               void* ret) {
    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch)
        return;

    // nargs should be statically enforced by the C++ wrapper (static_assert).
    assert(nargs <= PBOX_MAX_ARGS);

    ch->request_type = PBOX_REQ_CALL;
    ch->func_addr = (uintptr_t) func_addr;
    ch->nargs = nargs;
    ch->ret_type = ret_type;

    // Pack arguments into arg_storage.
    // Cannot overflow with current max types (8 * 8 = 64 bytes << 1024).
    _Static_assert(PBOX_MAX_ARGS * sizeof(uint64_t) <= PBOX_ARG_STORAGE,
                   "arg_storage too small for max args");
    size_t offset = 0;
    for (int i = 0; i < nargs; i++) {
        size_t size = pbox_type_size(arg_types[i]);
        assert(offset + size <= PBOX_ARG_STORAGE);
        ch->arg_types[i] = arg_types[i];
        ch->args[i] = offset;
        memcpy(&ch->arg_storage[offset], args[i], size);
        offset += size;
    }

    pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
    pbox_wait_for_response(box, ch);
    atomic_store(&ch->state, PBOX_STATE_IDLE);

    if (ret != NULL) {
        memcpy(ret, ch->result_storage, pbox_type_size(ret_type));
    }
}

// Internal: actually send an fd without checking cache
static int pbox_send_fd_on_channel(struct PBox* box, struct PBoxChannel* ch,
                                   int fd) {
    // Send fd over socket using SCM_RIGHTS
    struct msghdr msg = {0};
    struct iovec iov;
    char buf[1] = {0};
    char cmsg_buf[CMSG_SPACE(sizeof(int))];

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    if (sendmsg(box->sock_fd, &msg, 0) < 0)
        return -1;

    // Signal sandbox to receive the fd
    ch->request_type = PBOX_REQ_RECV_FD;
    pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
    pbox_wait_for_state(&ch->state, PBOX_STATE_RESPONSE);
    atomic_store(&ch->state, PBOX_STATE_IDLE);

    return ch->received_fd;
}

// Internal: add fd mapping to cache
static void pbox_cache_fd(struct PBox* box, int host_fd, int sandbox_fd) {
    if (host_fd < PBOX_FD_DIRECT_MAX) {
        box->fd_direct[host_fd] = sandbox_fd;
        return;
    }

    // Grow overflow vector if needed
    if (box->fd_overflow_count >= box->fd_overflow_cap) {
        size_t new_cap = box->fd_overflow_cap ? box->fd_overflow_cap * 2 : 8;
        struct PBoxFdEntry* new_vec =
            realloc(box->fd_overflow, new_cap * sizeof(struct PBoxFdEntry));
        if (!new_vec)
            return;  // Can't cache, but not fatal
        box->fd_overflow = new_vec;
        box->fd_overflow_cap = new_cap;
    }

    box->fd_overflow[box->fd_overflow_count].host_fd = host_fd;
    box->fd_overflow[box->fd_overflow_count].sandbox_fd = sandbox_fd;
    box->fd_overflow_count++;
}

// Internal: lookup fd in cache, returns -1 if not found
static int pbox_lookup_fd(struct PBox* box, int host_fd) {
    if (host_fd < PBOX_FD_DIRECT_MAX)
        return box->fd_direct[host_fd];

    for (size_t i = 0; i < box->fd_overflow_count; i++) {
        if (box->fd_overflow[i].host_fd == host_fd)
            return box->fd_overflow[i].sandbox_fd;
    }
    return -1;
}

int pbox_send_fd(struct PBox* box, int fd) {
    if (fd < 0)
        return fd;

    pthread_mutex_lock(&box->fd_lock);

    // Return cached sandbox fd if already sent
    int cached = pbox_lookup_fd(box, fd);
    if (cached >= 0) {
        pthread_mutex_unlock(&box->fd_lock);
        return cached;
    }

    // Get thread-local channel
    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch) {
        pthread_mutex_unlock(&box->fd_lock);
        return -1;
    }

    // Send and cache
    int sandbox_fd = pbox_send_fd_on_channel(box, ch, fd);
    if (sandbox_fd >= 0)
        pbox_cache_fd(box, fd, sandbox_fd);

    pthread_mutex_unlock(&box->fd_lock);
    return sandbox_fd;
}

// Invalidate fd cache entry for a sandbox fd
static void pbox_uncache_fd(struct PBox* box, int sandbox_fd) {
    // Check direct table
    for (int i = 0; i < PBOX_FD_DIRECT_MAX; i++) {
        if (box->fd_direct[i] == sandbox_fd) {
            box->fd_direct[i] = -1;
            return;
        }
    }

    // Check overflow table
    for (size_t i = 0; i < box->fd_overflow_count; i++) {
        if (box->fd_overflow[i].sandbox_fd == sandbox_fd) {
            box->fd_overflow[i] = box->fd_overflow[--box->fd_overflow_count];
            return;
        }
    }
}

int pbox_close(struct PBox* box, int sandbox_fd) {
    if (!box->sym_close || sandbox_fd < 0)
        return -1;

    int result;
    enum PBoxType arg_types[] = {PBOX_TYPE_SINT32};
    void* args[] = {&sandbox_fd};
    pbox_call(box, box->sym_close, PBOX_TYPE_SINT32, 1, arg_types, args,
              &result);

    // Invalidate cache entry
    if (result == 0) {
        pthread_mutex_lock(&box->fd_lock);
        pbox_uncache_fd(box, sandbox_fd);
        pthread_mutex_unlock(&box->fd_lock);
    }

    return result;
}

void* pbox_register_callback(struct PBox* box, void* host_func,
                             enum PBoxType ret_type, int nargs,
                             const enum PBoxType* arg_types) {
    pthread_mutex_lock(&box->callback_lock);

    if (atomic_load(&box->callback_count) >= PBOX_MAX_CALLBACKS) {
        pthread_mutex_unlock(&box->callback_lock);
        return NULL;
    }

    // nargs should be statically enforced by the C++ wrapper (static_assert).
    assert(nargs <= PBOX_MAX_ARGS);

    // Use current count as slot index but don't publish yet -- the
    // callback must be fully initialized before it becomes visible to
    // pbox_dispatch_callback (which reads callback_count without the lock).
    int id = atomic_load(&box->callback_count);
    struct PBoxCallback* cb = &box->callbacks[id];
    cb->func_ptr = host_func;
    cb->ret_type = ret_type;
    cb->nargs = nargs;
    for (int i = 0; i < nargs && i < PBOX_MAX_ARGS; i++) {
        cb->arg_types[i] = arg_types[i];
        cb->ffi_arg_types[i] = pbox_get_ffi_type(arg_types[i]);
    }

    // Pre-compute ffi_cif for callback dispatch
    ffi_type* ffi_ret = pbox_get_ffi_type(ret_type);
    if (ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, nargs, ffi_ret,
                     cb->ffi_arg_types) != FFI_OK) {
        pthread_mutex_unlock(&box->callback_lock);
        return NULL;
    }

    // Struct is fully initialized -- publish it so dispatch can see it.
    atomic_store(&box->callback_count, id + 1);

    // Request sandbox to create closure
    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch) {
        atomic_store(&box->callback_count, id);
        pthread_mutex_unlock(&box->callback_lock);
        return NULL;
    }

    ch->request_type = PBOX_REQ_CREATE_CLOSURE;
    ch->closure_callback_id = id;
    ch->closure_ret_type = ret_type;
    ch->closure_nargs = nargs;
    for (int i = 0; i < nargs && i < PBOX_MAX_ARGS; i++)
        ch->closure_arg_types[i] = arg_types[i];

    pbox_set_state(&ch->state, PBOX_STATE_REQUEST);
    pbox_wait_for_state(&ch->state, PBOX_STATE_RESPONSE);

    void* closure_addr = (void*) ch->closure_addr;
    cb->sandbox_closure = closure_addr;

    atomic_store(&ch->state, PBOX_STATE_IDLE);
    pthread_mutex_unlock(&box->callback_lock);
    return closure_addr;
}

void* pbox_mmap_box_fd(struct PBox* box, void* addr, size_t length, int prot,
                       int flags, int sandbox_fd, off_t offset) {
    if (!box->sym_mmap)
        return MAP_FAILED;

    void* result;
    enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_UINT64,
                                 PBOX_TYPE_SINT32,  PBOX_TYPE_SINT32,
                                 PBOX_TYPE_SINT32,  PBOX_TYPE_SINT64};
    void* args[] = {&addr, &length, &prot, &flags, &sandbox_fd, &offset};
    pbox_call(box, box->sym_mmap, PBOX_TYPE_POINTER, 6, arg_types, args,
              &result);
    return result;
}

void* pbox_mmap(struct PBox* box, void* addr, size_t length, int prot,
                int flags, int fd, off_t offset) {
    // Translate host fd to sandbox fd (sends if not already sent)
    int sandbox_fd = pbox_send_fd(box, fd);
    if (fd >= 0 && sandbox_fd < 0)
        return MAP_FAILED;

    return pbox_mmap_box_fd(box, addr, length, prot, flags, sandbox_fd, offset);
}

int pbox_munmap(struct PBox* box, void* addr, size_t length) {
    if (!box->sym_munmap)
        return -1;

    int result;
    enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_UINT64};
    void* args[] = {&addr, &length};
    pbox_call(box, box->sym_munmap, PBOX_TYPE_SINT32, 2, arg_types, args,
              &result);
    return result;
}

void* pbox_mmap_identity(struct PBox* box, size_t length, int prot) {
    if (!box->sym_mmap)
        return NULL;

    // Create anonymous shared memory
    int memfd = memfd_create("pbox_shared", MFD_CLOEXEC);
    if (memfd < 0)
        return NULL;

    if (ftruncate(memfd, length) < 0) {
        close(memfd);
        return NULL;
    }

    // Map in host - let kernel pick address
    void* host_addr = mmap(NULL, length, prot, MAP_SHARED, memfd, 0);
    if (host_addr == MAP_FAILED) {
        close(memfd);
        return NULL;
    }

    // Send fd to sandbox without caching -- this memfd is temporary and
    // will be closed after mapping, so caching would leave a stale entry.
    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch) {
        munmap(host_addr, length);
        close(memfd);
        return NULL;
    }
    int sandbox_fd = pbox_send_fd_on_channel(box, ch, memfd);
    if (sandbox_fd < 0) {
        munmap(host_addr, length);
        close(memfd);
        return NULL;
    }

    // Try to map in sandbox at the same address
    int flags = MAP_SHARED | MAP_FIXED_NOREPLACE;
    off_t offset = 0;
    void* sandbox_addr;
    enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_UINT64,
                                 PBOX_TYPE_SINT32,  PBOX_TYPE_SINT32,
                                 PBOX_TYPE_SINT32,  PBOX_TYPE_SINT64};
    void* args[] = {&host_addr, &length, &prot, &flags, &sandbox_fd, &offset};
    pbox_call(box, box->sym_mmap, PBOX_TYPE_POINTER, 6, arg_types, args,
              &sandbox_addr);

    if (sandbox_addr == host_addr) {
        close(memfd);
        return host_addr;
    }

    // First attempt failed - fallback to /proc/maps
    if (sandbox_addr != MAP_FAILED)
        pbox_munmap(box, sandbox_addr, length);
    munmap(host_addr, length);

    void* common_addr =
        pbox_find_common_free_address(getpid(), box->pid, length);
    if (!common_addr) {
        close(memfd);
        return NULL;
    }

    // Map in host at chosen address
    host_addr = mmap(common_addr, length, prot,
                     MAP_SHARED | MAP_FIXED_NOREPLACE, memfd, 0);
    if (host_addr != common_addr) {
        if (host_addr != MAP_FAILED)
            munmap(host_addr, length);
        close(memfd);
        return NULL;
    }

    // Map in sandbox at same address
    args[0] = &common_addr;
    pbox_call(box, box->sym_mmap, PBOX_TYPE_POINTER, 6, arg_types, args,
              &sandbox_addr);

    if (sandbox_addr != common_addr) {
        if (sandbox_addr != MAP_FAILED)
            pbox_munmap(box, sandbox_addr, length);
        munmap(host_addr, length);
        close(memfd);
        return NULL;
    }

    close(memfd);
    return common_addr;
}

int pbox_munmap_identity(struct PBox* box, void* addr, size_t length) {
    int sandbox_result = pbox_munmap(box, addr, length);
    int host_result = munmap(addr, length);
    return (sandbox_result == 0 && host_result == 0) ? 0 : -1;
}

void* pbox_malloc(struct PBox* box, size_t size) {
    if (!box->sym_malloc)
        return NULL;

    void* result;
    enum PBoxType arg_types[] = {PBOX_TYPE_UINT64};
    void* args[] = {&size};
    pbox_call(box, box->sym_malloc, PBOX_TYPE_POINTER, 1, arg_types, args,
              &result);
    return result;
}

void* pbox_calloc(struct PBox* box, size_t nmemb, size_t size) {
    if (!box->sym_calloc)
        return NULL;

    void* result;
    enum PBoxType arg_types[] = {PBOX_TYPE_UINT64, PBOX_TYPE_UINT64};
    void* args[] = {&nmemb, &size};
    pbox_call(box, box->sym_calloc, PBOX_TYPE_POINTER, 2, arg_types, args,
              &result);
    return result;
}

void* pbox_realloc(struct PBox* box, void* p, size_t size) {
    if (!box->sym_realloc)
        return NULL;

    void* result;
    enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_UINT64};
    void* args[] = {&p, &size};
    pbox_call(box, box->sym_realloc, PBOX_TYPE_POINTER, 2, arg_types, args,
              &result);
    return result;
}

void pbox_free(struct PBox* box, void* p) {
    if (!box->sym_free)
        return;

    enum PBoxType arg_types[] = {PBOX_TYPE_POINTER};
    void* args[] = {&p};
    pbox_call(box, box->sym_free, PBOX_TYPE_VOID, 1, arg_types, args, NULL);
}

void pbox_copy_to(struct PBox* box, void* dest, const void* src, size_t n) {
    if (!box->sym_memcpy)
        return;

    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch)
        return;

    uintptr_t sandbox_mem_storage =
        ch->sandbox_channel_addr + offsetof(struct PBoxChannel, mem_storage);

    const char* s = src;
    char* d = dest;

    while (n > 0) {
        size_t chunk = n < PBOX_MEM_STORAGE ? n : PBOX_MEM_STORAGE;

        // Copy from host to shared mem_storage.
        memcpy(ch->mem_storage, s, chunk);

        // Call sandbox's memcpy(dest, mem_storage, chunk).
        void* mem_storage_ptr = (void*) sandbox_mem_storage;
        enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_POINTER,
                                     PBOX_TYPE_UINT64};
        void* args[] = {&d, &mem_storage_ptr, &chunk};
        pbox_call(box, box->sym_memcpy, PBOX_TYPE_POINTER, 3, arg_types, args,
                  NULL);

        s += chunk;
        d += chunk;
        n -= chunk;
    }
}

// Get thread-local channel struct (not just the channel pointer)
static struct PBoxThreadChannel* get_or_create_thread_channel(
    struct PBox* box) {
    struct PBoxThreadChannel* tch = pthread_getspecific(box->channel_key);
    if (tch)
        return tch;

    pthread_mutex_lock(&box->channel_lock);
    tch = create_channel_locked(box);
    pthread_mutex_unlock(&box->channel_lock);

    if (!tch)
        return NULL;

    pthread_setspecific(box->channel_key, tch);
    return tch;
}

void* pbox_idmem_alloc(struct PBox* box, size_t size) {
    struct PBoxThreadChannel* tch = get_or_create_thread_channel(box);
    if (!tch)
        return NULL;

    // Lazy initialization of identity region
    if (!tch->idmem_base) {
        tch->idmem_base = pbox_mmap_identity(box, PBOX_IDMEM_DEFAULT_SIZE,
                                             PROT_READ | PROT_WRITE);
        if (!tch->idmem_base)
            return NULL;
        tch->idmem_size = PBOX_IDMEM_DEFAULT_SIZE;
        tch->idmem_offset = 0;
    }

    // Align to 16 bytes
    size = (size + 15) & ~(size_t) 15;

    // Check if we have space
    if (tch->idmem_offset + size > tch->idmem_size)
        return NULL;

    void* ptr = (char*) tch->idmem_base + tch->idmem_offset;
    tch->idmem_offset += size;
    return ptr;
}

void pbox_idmem_reset(struct PBox* box) {
    struct PBoxThreadChannel* tch = pthread_getspecific(box->channel_key);
    if (tch && tch->idmem_base)
        tch->idmem_offset = 0;
}

void pbox_copy_from(struct PBox* box, void* dest, const void* src, size_t n) {
    if (!box->sym_memcpy)
        return;

    struct PBoxChannel* ch = get_or_create_channel(box);
    if (!ch)
        return;

    uintptr_t sandbox_mem_storage =
        ch->sandbox_channel_addr + offsetof(struct PBoxChannel, mem_storage);

    char* d = dest;
    const char* s = src;

    while (n > 0) {
        size_t chunk = n < PBOX_MEM_STORAGE ? n : PBOX_MEM_STORAGE;

        // Call sandbox's memcpy(mem_storage, src, chunk)
        void* mem_storage_ptr = (void*) sandbox_mem_storage;
        void* src_ptr = (void*) s;
        enum PBoxType arg_types[] = {PBOX_TYPE_POINTER, PBOX_TYPE_POINTER,
                                     PBOX_TYPE_UINT64};
        void* args[] = {&mem_storage_ptr, &src_ptr, &chunk};
        pbox_call(box, box->sym_memcpy, PBOX_TYPE_POINTER, 3, arg_types, args,
                  NULL);

        // Copy from shared mem_storage to host
        memcpy(d, ch->mem_storage, chunk);

        s += chunk;
        d += chunk;
        n -= chunk;
    }
}
