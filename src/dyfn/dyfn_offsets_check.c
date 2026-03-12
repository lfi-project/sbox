#include "dyfn.h"
#include "dyfn_offsets.h"

#include <stddef.h>

#define CHECK(type, field, expected) \
    _Static_assert(offsetof(type, field) == expected, \
                   "offset mismatch: " #type "." #field)

// DyfnCallArgs
CHECK(struct DyfnCallArgs, int_regs,    DYFN_CALL_INT_REGS);
CHECK(struct DyfnCallArgs, float_regs,  DYFN_CALL_FLOAT_REGS);
CHECK(struct DyfnCallArgs, int_count,   DYFN_CALL_INT_COUNT);
CHECK(struct DyfnCallArgs, float_count, DYFN_CALL_FLOAT_COUNT);
CHECK(struct DyfnCallArgs, stack_args,  DYFN_CALL_STACK_ARGS);
CHECK(struct DyfnCallArgs, stack_count, DYFN_CALL_STACK_COUNT);
CHECK(struct DyfnCallArgs, func,        DYFN_CALL_FUNC);
CHECK(struct DyfnCallArgs, ret_class,   DYFN_CALL_RET_CLASS);

// DyfnCallResult
CHECK(struct DyfnCallResult, int_val,   DYFN_RESULT_INT_VAL);
CHECK(struct DyfnCallResult, float_val, DYFN_RESULT_FLOAT_VAL);

#ifndef SBOX_NO_CALLBACKS
// DyfnClosureSavedRegs
CHECK(struct DyfnClosureSavedRegs, int_regs,   DYFN_SAVED_INT_REGS);
CHECK(struct DyfnClosureSavedRegs, float_regs, DYFN_SAVED_FLOAT_REGS);
CHECK(struct DyfnClosureSavedRegs, stub_index, DYFN_SAVED_STUB_INDEX);

// DyfnClosureResult
CHECK(struct DyfnClosureResult, int_val,   DYFN_CLOSURE_INT_VAL);
CHECK(struct DyfnClosureResult, float_val, DYFN_CLOSURE_FLOAT_VAL);
CHECK(struct DyfnClosureResult, ret_class, DYFN_CLOSURE_RET_CLASS);

// Struct sizes
_Static_assert(sizeof(struct DyfnClosureSavedRegs) == DYFN_SIZEOF_CLOSURE_SAVED_REGS,
               "size mismatch: DyfnClosureSavedRegs");
_Static_assert(sizeof(struct DyfnClosureResult) == DYFN_SIZEOF_CLOSURE_RESULT,
               "size mismatch: DyfnClosureResult");
_Static_assert(DYFN_CLOSURE_FRAME_SIZE % 16 == 0,
               "DYFN_CLOSURE_FRAME_SIZE must be 16-byte aligned");
_Static_assert(DYFN_MAX_CLOSURES_ASM == DYFN_MAX_CLOSURES,
               "DYFN_MAX_CLOSURES_ASM must match DYFN_MAX_CLOSURES");
#endif
