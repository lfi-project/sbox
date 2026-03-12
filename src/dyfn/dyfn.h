#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DYFN_MAX_ARGS 8
#define DYFN_MAX_CLOSURES 64

// Argument/return types
enum DyfnType {
    DYFN_TYPE_VOID = 0,
    DYFN_TYPE_UINT8,
    DYFN_TYPE_SINT8,
    DYFN_TYPE_UINT16,
    DYFN_TYPE_SINT16,
    DYFN_TYPE_UINT32,
    DYFN_TYPE_SINT32,
    DYFN_TYPE_UINT64,
    DYFN_TYPE_SINT64,
    DYFN_TYPE_FLOAT,
    DYFN_TYPE_DOUBLE,
    DYFN_TYPE_POINTER,
};

// Type classification for ABI dispatch
enum DyfnClass {
    DYFN_CLASS_INT = 0,
    DYFN_CLASS_FLOAT = 1,
    DYFN_CLASS_DOUBLE = 2,
    DYFN_CLASS_VOID = 3,
};

// Prepared call arguments (C fills, assembly reads).
// Struct layout must match offsets in dyfn_offsets.h (verified at compile time).
struct DyfnCallArgs {
    uint64_t int_regs[8];
    uint64_t float_regs[8];
    int int_count;
    int float_count;
    uint64_t stack_args[4];
    int stack_count;
    // 4 bytes padding
    void* func;
    int ret_class;
};

struct DyfnCallResult {
    uint64_t int_val;
    double float_val;
};

// Classify a type into INT/FLOAT/DOUBLE/VOID
enum DyfnClass dyfn_classify(enum DyfnType type);

// Get size of a type in bytes
size_t dyfn_type_size(enum DyfnType type);

// Prepare call: classify args, fill register arrays
bool dyfn_prep_call(struct DyfnCallArgs* call, void* func,
                    enum DyfnType ret_type, int nargs,
                    const enum DyfnType* arg_types, void** arg_values);

// Store result from DyfnCallResult into a buffer based on ret_type
void dyfn_store_result(const struct DyfnCallResult* result,
                       enum DyfnType ret_type, void* out);

// Assembly: perform the call
extern void dyfn_call(struct DyfnCallArgs* args, struct DyfnCallResult* result);

#ifndef SBOX_NO_CALLBACKS

// Saved register state from closure invocation (filled by asm, read by C)
struct DyfnClosureSavedRegs {
    uint64_t int_regs[8];
    uint64_t float_regs[8];
    int stub_index;
};

// Return value from closure dispatch (filled by C, read by asm)
struct DyfnClosureResult {
    uint64_t int_val;
    double float_val;
    int ret_class;
};

// Per-slot type info (thread-local, indexed by stub slot)
struct DyfnClosureInfo {
    int callback_id;
    int nargs;
    enum DyfnType arg_types[DYFN_MAX_ARGS];
    enum DyfnType ret_type;
    bool active;
};

// Closure management (thread-local)
extern __thread struct DyfnClosureInfo
    dyfn_closure_info[DYFN_MAX_CLOSURES];
extern __thread int dyfn_closure_count;

void* dyfn_closure_alloc(int callback_id, enum DyfnType ret_type, int nargs,
                         const enum DyfnType* arg_types);
void dyfn_closure_free_all(void);

// Assembly: table of closure stub function pointers
extern void* dyfn_stub_table[DYFN_MAX_CLOSURES];

#endif // SBOX_NO_CALLBACKS

#ifdef __cplusplus
}
#endif
