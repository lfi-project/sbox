#include "sbox_dyfn.h"
#include "sbox_dyfn_offsets.h"

#include <stddef.h>

#define CHECK(type, field, expected) \
    _Static_assert(offsetof(type, field) == expected, \
                   "offset mismatch: " #type "." #field)

// SboxDyfnCallArgs
CHECK(struct SboxDyfnCallArgs, int_regs,    DYFN_CALL_INT_REGS);
CHECK(struct SboxDyfnCallArgs, float_regs,  DYFN_CALL_FLOAT_REGS);
CHECK(struct SboxDyfnCallArgs, int_count,   DYFN_CALL_INT_COUNT);
CHECK(struct SboxDyfnCallArgs, float_count, DYFN_CALL_FLOAT_COUNT);
CHECK(struct SboxDyfnCallArgs, stack_args,  DYFN_CALL_STACK_ARGS);
CHECK(struct SboxDyfnCallArgs, stack_count, DYFN_CALL_STACK_COUNT);
CHECK(struct SboxDyfnCallArgs, func,        DYFN_CALL_FUNC);
CHECK(struct SboxDyfnCallArgs, ret_class,   DYFN_CALL_RET_CLASS);

// SboxDyfnCallResult
CHECK(struct SboxDyfnCallResult, int_val,   DYFN_RESULT_INT_VAL);
CHECK(struct SboxDyfnCallResult, float_val, DYFN_RESULT_FLOAT_VAL);

// SboxDyfnClosureSavedRegs
CHECK(struct SboxDyfnClosureSavedRegs, int_regs,   DYFN_SAVED_INT_REGS);
CHECK(struct SboxDyfnClosureSavedRegs, float_regs, DYFN_SAVED_FLOAT_REGS);
CHECK(struct SboxDyfnClosureSavedRegs, stub_index, DYFN_SAVED_STUB_INDEX);

// SboxDyfnClosureResult
CHECK(struct SboxDyfnClosureResult, int_val,   DYFN_CLOSURE_INT_VAL);
CHECK(struct SboxDyfnClosureResult, float_val, DYFN_CLOSURE_FLOAT_VAL);
CHECK(struct SboxDyfnClosureResult, ret_class, DYFN_CLOSURE_RET_CLASS);

// Struct sizes
_Static_assert(sizeof(struct SboxDyfnClosureSavedRegs) == DYFN_SIZEOF_CLOSURE_SAVED_REGS,
               "size mismatch: SboxDyfnClosureSavedRegs");
_Static_assert(sizeof(struct SboxDyfnClosureResult) == DYFN_SIZEOF_CLOSURE_RESULT,
               "size mismatch: SboxDyfnClosureResult");
_Static_assert(DYFN_CLOSURE_FRAME_SIZE % 16 == 0,
               "DYFN_CLOSURE_FRAME_SIZE must be 16-byte aligned");
_Static_assert(DYFN_MAX_CLOSURES == SBOX_DYFN_MAX_CLOSURES,
               "DYFN_MAX_CLOSURES must match SBOX_DYFN_MAX_CLOSURES");
