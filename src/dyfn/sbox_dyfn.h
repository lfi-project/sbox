#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SBOX_DYFN_MAX_ARGS 8
#define SBOX_DYFN_MAX_CLOSURES 64

// Argument/return types
enum SboxDyfnType {
    SBOX_DYFN_TYPE_VOID = 0,
    SBOX_DYFN_TYPE_UINT8,
    SBOX_DYFN_TYPE_SINT8,
    SBOX_DYFN_TYPE_UINT16,
    SBOX_DYFN_TYPE_SINT16,
    SBOX_DYFN_TYPE_UINT32,
    SBOX_DYFN_TYPE_SINT32,
    SBOX_DYFN_TYPE_UINT64,
    SBOX_DYFN_TYPE_SINT64,
    SBOX_DYFN_TYPE_FLOAT,
    SBOX_DYFN_TYPE_DOUBLE,
    SBOX_DYFN_TYPE_POINTER,
};

// Type classification for ABI dispatch
enum SboxDyfnClass {
    SBOX_DYFN_CLASS_INT = 0,
    SBOX_DYFN_CLASS_FLOAT = 1,
    SBOX_DYFN_CLASS_DOUBLE = 2,
    SBOX_DYFN_CLASS_VOID = 3,
};

// Prepared call arguments (C fills, assembly reads).
// Struct layout must match offsets in sbox_dyfn_offsets.h (verified at compile time).
struct SboxDyfnCallArgs {
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

struct SboxDyfnCallResult {
    uint64_t int_val;
    double float_val;
};

// Saved register state from closure invocation (filled by asm, read by C)
struct SboxDyfnClosureSavedRegs {
    uint64_t int_regs[8];
    uint64_t float_regs[8];
    int stub_index;
};

// Return value from closure dispatch (filled by C, read by asm)
struct SboxDyfnClosureResult {
    uint64_t int_val;
    double float_val;
    int ret_class;
};

// Per-slot type info (thread-local, indexed by stub slot)
struct SboxDyfnClosureInfo {
    int callback_id;
    int nargs;
    enum SboxDyfnType arg_types[SBOX_DYFN_MAX_ARGS];
    enum SboxDyfnType ret_type;
    bool active;
};

// Classify a type into INT/FLOAT/DOUBLE/VOID
enum SboxDyfnClass sbox_dyfn_classify(enum SboxDyfnType type);

// Get size of a type in bytes
size_t sbox_dyfn_type_size(enum SboxDyfnType type);

// Prepare call: classify args, fill register arrays
bool sbox_dyfn_prep_call(struct SboxDyfnCallArgs* call, void* func,
                         enum SboxDyfnType ret_type, int nargs,
                         const enum SboxDyfnType* arg_types,
                         void** arg_values);

// Store result from SboxDyfnCallResult into a buffer based on ret_type
void sbox_dyfn_store_result(const struct SboxDyfnCallResult* result,
                            enum SboxDyfnType ret_type, void* out);

// Assembly: perform the call
extern void sbox_dyfn_call(struct SboxDyfnCallArgs* args,
                           struct SboxDyfnCallResult* result);

// Closure management (thread-local)
extern __thread struct SboxDyfnClosureInfo
    sbox_dyfn_closure_info[SBOX_DYFN_MAX_CLOSURES];
extern __thread int sbox_dyfn_closure_count;

void* sbox_dyfn_closure_alloc(int callback_id, enum SboxDyfnType ret_type,
                              int nargs, const enum SboxDyfnType* arg_types);
void sbox_dyfn_closure_free_all(void);

// Assembly: table of 64 stub function pointers
extern void* sbox_dyfn_stub_table[SBOX_DYFN_MAX_CLOSURES];

#ifdef __cplusplus
}
#endif
