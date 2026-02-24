#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// FFI type codes
enum PBoxType {
    PBOX_TYPE_VOID = 0,
    PBOX_TYPE_UINT8,
    PBOX_TYPE_SINT8,
    PBOX_TYPE_UINT16,
    PBOX_TYPE_SINT16,
    PBOX_TYPE_UINT32,
    PBOX_TYPE_SINT32,
    PBOX_TYPE_UINT64,
    PBOX_TYPE_SINT64,
    PBOX_TYPE_FLOAT,
    PBOX_TYPE_DOUBLE,
    PBOX_TYPE_POINTER
};

#define PBOX_MAX_ARGS 8

struct PBox;

// Initialize a sandbox running the given executable
// Returns NULL on failure
struct PBox *pbox_create(const char *sandbox_executable);

// Destroy a sandbox and free resources
void pbox_destroy(struct PBox *box);

// Get the sandbox process ID
pid_t pbox_pid(const struct PBox *box);

// Check if the sandbox is still alive
int pbox_alive(const struct PBox *box);

// Look up a symbol address in the sandbox
// Returns NULL if not found
void *pbox_dlsym(struct PBox *box, const char *symbol);

// Call a function in the sandbox
// func_addr: address from pbox_dlsym
// ret_type: return type (PBOX_TYPE_*)
// nargs: number of arguments (0 to PBOX_MAX_ARGS)
// arg_types: array of argument types
// args: array of pointers to argument values
// ret: pointer to storage for return value (can be NULL for void)
void pbox_call(struct PBox *box, void *func_addr,
               enum PBoxType ret_type, int nargs,
               const enum PBoxType *arg_types, void **args, void *ret);

// Copy data to sandbox memory
// dest: address in sandbox (from pbox_malloc)
// src: pointer in host memory
// n: number of bytes
void pbox_copy_to(struct PBox *box, void *dest, const void *src, size_t n);

// Copy data from sandbox memory
// dest: pointer in host memory
// src: address in sandbox
// n: number of bytes
void pbox_copy_from(struct PBox *box, void *dest, const void *src, size_t n);

// Memory allocation in sandbox
void *pbox_malloc(struct PBox *box, size_t size);
void *pbox_calloc(struct PBox *box, size_t nmemb, size_t size);
void *pbox_realloc(struct PBox *box, void *p, size_t size);
void pbox_free(struct PBox *box, void *p);

// Memory mapping in sandbox using a sandbox fd (from pbox_send_fd)
void *pbox_mmap_box_fd(struct PBox *box, void *addr, size_t length, int prot, int flags, int sandbox_fd, off_t offset);

// Memory mapping in sandbox using a host fd (automatically sent to sandbox)
void *pbox_mmap(struct PBox *box, void *addr, size_t length, int prot, int flags, int fd, off_t offset);

int pbox_munmap(struct PBox *box, void *addr, size_t length);

// Create identity-mapped memory between host and sandbox
// The returned pointer is valid and identical in both processes
// Returns NULL on failure
void *pbox_mmap_identity(struct PBox *box, size_t length, int prot);

// Unmap identity-mapped memory (unmaps in both host and sandbox)
int pbox_munmap_identity(struct PBox *box, void *addr, size_t length);

// Arena allocator for per-channel identity-mapped memory
// Each thread's channel has a dedicated identity region
// Returns pointer valid in both host and sandbox, or NULL on failure
void *pbox_idmem_alloc(struct PBox *box, size_t size);

// Reset arena, freeing all allocations from the current channel's identity region
void pbox_idmem_reset(struct PBox *box);

// Send a file descriptor to the sandbox
// Returns the fd number in the sandbox, or -1 on error
int pbox_send_fd(struct PBox *box, int fd);

// Close a file descriptor in the sandbox
// Takes the sandbox fd (returned by pbox_send_fd)
int pbox_close(struct PBox *box, int sandbox_fd);

// Register a host function as a callback
// Returns a function pointer valid in the sandbox address space
// The sandbox can call this pointer like a normal function
void *pbox_register_callback(struct PBox *box,
                             void *host_func,
                             enum PBoxType ret_type,
                             int nargs,
                             const enum PBoxType *arg_types);

// Convenience macros for callback registration
#define pbox_register_callback0(box, fn, ret_ptype) \
    pbox_register_callback(box, fn, ret_ptype, 0, NULL)

#define pbox_register_callback1(box, fn, ret_ptype, ptype0) \
    ({ enum PBoxType _t[] = {ptype0}; \
       pbox_register_callback(box, fn, ret_ptype, 1, _t); })

#define pbox_register_callback2(box, fn, ret_ptype, ptype0, ptype1) \
    ({ enum PBoxType _t[] = {ptype0, ptype1}; \
       pbox_register_callback(box, fn, ret_ptype, 2, _t); })

#define pbox_register_callback3(box, fn, ret_ptype, ptype0, ptype1, ptype2) \
    ({ enum PBoxType _t[] = {ptype0, ptype1, ptype2}; \
       pbox_register_callback(box, fn, ret_ptype, 3, _t); })

// Convenience macros for common calling patterns
// Usage: pbox_callN(box, fn, ret_ctype, ret_ptype, ctype0, ptype0, val0, ...)
// Returns the result directly as ret_ctype

#define pbox_call0(box, fn, ret_ctype, ret_ptype) \
    ({ \
        ret_ctype _ret; \
        pbox_call(box, fn, ret_ptype, 0, NULL, NULL, &_ret); \
        _ret; \
    })

#define pbox_call1(box, fn, ret_ctype, ret_ptype, ctype0, ptype0, val0) \
    ({ \
        ctype0 _a0 = (val0); \
        ret_ctype _ret; \
        enum PBoxType _t[] = {ptype0}; \
        void *_a[] = {&_a0}; \
        pbox_call(box, fn, ret_ptype, 1, _t, _a, &_ret); \
        _ret; \
    })

#define pbox_call2(box, fn, ret_ctype, ret_ptype, ctype0, ptype0, val0, ctype1, ptype1, val1) \
    ({ \
        ctype0 _a0 = (val0); \
        ctype1 _a1 = (val1); \
        ret_ctype _ret; \
        enum PBoxType _t[] = {ptype0, ptype1}; \
        void *_a[] = {&_a0, &_a1}; \
        pbox_call(box, fn, ret_ptype, 2, _t, _a, &_ret); \
        _ret; \
    })

#define pbox_call3(box, fn, ret_ctype, ret_ptype, ctype0, ptype0, val0, ctype1, ptype1, val1, ctype2, ptype2, val2) \
    ({ \
        ctype0 _a0 = (val0); \
        ctype1 _a1 = (val1); \
        ctype2 _a2 = (val2); \
        ret_ctype _ret; \
        enum PBoxType _t[] = {ptype0, ptype1, ptype2}; \
        void *_a[] = {&_a0, &_a1, &_a2}; \
        pbox_call(box, fn, ret_ptype, 3, _t, _a, &_ret); \
        _ret; \
    })

#ifdef __cplusplus
}
#endif
