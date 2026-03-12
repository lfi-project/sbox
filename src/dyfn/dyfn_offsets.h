#pragma once

// DyfnCallArgs offsets
#define DYFN_CALL_INT_REGS     0
#define DYFN_CALL_FLOAT_REGS   64
#define DYFN_CALL_INT_COUNT    128
#define DYFN_CALL_FLOAT_COUNT  132
#define DYFN_CALL_STACK_ARGS   136
#define DYFN_CALL_STACK_COUNT  168
#define DYFN_CALL_FUNC         176
#define DYFN_CALL_RET_CLASS    184

// DyfnCallResult offsets
#define DYFN_RESULT_INT_VAL    0
#define DYFN_RESULT_FLOAT_VAL  8

// DyfnClosureSavedRegs offsets
#define DYFN_SAVED_INT_REGS    0
#define DYFN_SAVED_FLOAT_REGS  64
#define DYFN_SAVED_STUB_INDEX  128

// DyfnClosureResult offsets
#define DYFN_CLOSURE_INT_VAL   0
#define DYFN_CLOSURE_FLOAT_VAL 8
#define DYFN_CLOSURE_RET_CLASS 16

// Struct sizes (for assembly stack frame layout)
#define DYFN_SIZEOF_CLOSURE_SAVED_REGS 136
#define DYFN_SIZEOF_CLOSURE_RESULT     24

// Closure common handler frame size (must be 16-byte aligned)
#define DYFN_CLOSURE_FRAME_SIZE \
    (DYFN_SIZEOF_CLOSURE_SAVED_REGS + DYFN_SIZEOF_CLOSURE_RESULT)

// Maximum number of closure stubs (for assembly use; must match DYFN_MAX_CLOSURES in dyfn.h)
#define DYFN_MAX_CLOSURES_ASM 64
