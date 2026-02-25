#pragma once

#include "pbox.h"

#include <linux/futex.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

// Channel states
enum {
    PBOX_STATE_IDLE = 0,
    PBOX_STATE_REQUEST = 1,
    PBOX_STATE_RESPONSE = 2,
    PBOX_STATE_EXIT = 3,
    PBOX_STATE_DEAD = 4,
    PBOX_STATE_CALLBACK = 5  // Sandbox requesting a callback to host
};

// Request types
enum {
    PBOX_REQ_DLSYM = 1,
    PBOX_REQ_CALL = 2,
    PBOX_REQ_RECV_FD = 3,
    PBOX_REQ_SPAWN_WORKER = 4,
    PBOX_REQ_CREATE_CLOSURE = 5  // Create ffi_closure in sandbox
};

#define PBOX_MAX_SYMBOL_NAME 256
#define PBOX_SPIN_ITERATIONS 0
#define PBOX_ARG_STORAGE 1024
#define PBOX_RESULT_STORAGE 32
#define PBOX_MEM_STORAGE 4096
#define PBOX_MAX_CLOSURES 64
#define PBOX_IDMEM_DEFAULT_SIZE (1 << 20)  // 1MB default identity region

// Shared memory channel layout
struct PBoxChannel {
    atomic_int state;

    // Sandbox's view of this channel's address
    uintptr_t sandbox_channel_addr;

    int request_type;

    // For PBOX_REQ_CALL
    uint64_t func_addr;
    int nargs;
    int ret_type;
    int arg_types[PBOX_MAX_ARGS];
    // These are offsets into arg_storage.
    uint64_t args[PBOX_MAX_ARGS];

    // For PBOX_REQ_DLSYM
    char symbol_name[PBOX_MAX_SYMBOL_NAME];
    uintptr_t symbol_addr;

    // For PBOX_REQ_RECV_FD
    int received_fd;

    // For PBOX_REQ_SPAWN_WORKER
    int worker_shm_fd;  // Sandbox fd of new channel

    // For PBOX_REQ_CREATE_CLOSURE
    int closure_callback_id;
    int closure_nargs;
    int closure_ret_type;
    int closure_arg_types[PBOX_MAX_ARGS];
    uintptr_t closure_addr;  // Result: sandbox address of created closure

    // For PBOX_STATE_CALLBACK
    int callback_id;

    char arg_storage[PBOX_ARG_STORAGE];
    char result_storage[PBOX_RESULT_STORAGE];
    char mem_storage[PBOX_MEM_STORAGE];
};

#if defined(__i386__) || defined(__x86_64__)
#define PAUSE() __asm__ __volatile__("pause")
#elif defined(__aarch64__) || defined(__arm__)
#define PAUSE() __asm__ __volatile__("yield")
#else
#define PAUSE() \
    do {        \
    } while (0)
#endif

static inline int pbox_futex_wait(atomic_int* addr, int expected) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static inline int pbox_futex_wake(atomic_int* addr) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}

// Hybrid spin-then-wait
static inline void pbox_wait_for_state(atomic_int* addr, int expected) {
    for (int i = 0; i < PBOX_SPIN_ITERATIONS; i++) {
        if (atomic_load(addr) == expected)
            return;
        PAUSE();
    }
    int current;
    while ((current = atomic_load(addr)) != expected) {
        pbox_futex_wait(addr, current);
    }
}

static inline void pbox_set_state(atomic_int* addr, int value) {
    atomic_store(addr, value);
    pbox_futex_wake(addr);
}
