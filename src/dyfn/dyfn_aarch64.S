#include "dyfn_offsets.h"

.text

// dyfn_call(DyfnCallArgs* args, DyfnCallResult* result)
.global dyfn_call
dyfn_call:
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    stp  x19, x20, [sp, #16]

    mov  x19, x0 // x19 = args
    mov  x20, x1 // x20 = result

    ldp  d0, d1, [x19, #(DYFN_CALL_FLOAT_REGS + 0)]
    ldp  d2, d3, [x19, #(DYFN_CALL_FLOAT_REGS + 16)]
    ldp  d4, d5, [x19, #(DYFN_CALL_FLOAT_REGS + 32)]
    ldp  d6, d7, [x19, #(DYFN_CALL_FLOAT_REGS + 48)]

    ldp  x0, x1, [x19, #(DYFN_CALL_INT_REGS + 0)]
    ldp  x2, x3, [x19, #(DYFN_CALL_INT_REGS + 16)]
    ldp  x4, x5, [x19, #(DYFN_CALL_INT_REGS + 32)]
    ldp  x6, x7, [x19, #(DYFN_CALL_INT_REGS + 48)]

    ldr  x9, [x19, #DYFN_CALL_FUNC]
    blr  x9

    str  x0, [x20, #DYFN_RESULT_INT_VAL]
    str  d0, [x20, #DYFN_RESULT_FLOAT_VAL]

    ldp  x19, x20, [sp, #16]
    ldp  x29, x30, [sp], #32
    ret

#ifndef SBOX_NO_CALLBACKS

.altmacro

.macro closure_stub i
    .p2align 4
    .global dyfn_stub_\i
    dyfn_stub_\i:
        mov  w9, #\i
        b    dyfn_closure_common
.endm

.macro gen_stubs i
    .if \i < DYFN_MAX_CLOSURES_ASM
        closure_stub \i
        gen_stubs %(\i + 1)
    .endif
.endm

gen_stubs 0

// Common closure handler.
// On entry: w9 = stub index, all arg registers live from caller.
// x30 holds return address, sp is 16-byte aligned.
//
// Stack layout:
//   x29, x30 save
//   DyfnClosureSavedRegs
//   DyfnClosureResul

#define CLOSURE_TOTAL_SIZE (16 + DYFN_CLOSURE_FRAME_SIZE)
#define SAVED_BASE  16
#define RESULT_BASE (16 + DYFN_SIZEOF_CLOSURE_SAVED_REGS)

dyfn_closure_common:
    stp  x29, x30, [sp, #-CLOSURE_TOTAL_SIZE]!
    mov  x29, sp

    stp  x0, x1, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 0)]
    stp  x2, x3, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 16)]
    stp  x4, x5, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 32)]
    stp  x6, x7, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 48)]

    stp  d0, d1, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 0)]
    stp  d2, d3, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 16)]
    stp  d4, d5, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 32)]
    stp  d6, d7, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 48)]

    str  w9, [sp, #(SAVED_BASE + DYFN_SAVED_STUB_INDEX)]

    add  x0, sp, #SAVED_BASE
    add  x1, sp, #RESULT_BASE
    bl   dyfn_closure_dispatch

    ldr  x0, [sp, #(RESULT_BASE + DYFN_CLOSURE_INT_VAL)]
    ldr  d0, [sp, #(RESULT_BASE + DYFN_CLOSURE_FLOAT_VAL)]

    ldp  x29, x30, [sp], #CLOSURE_TOTAL_SIZE
    ret

.macro gen_table_entry i
    .if \i < DYFN_MAX_CLOSURES_ASM
        .quad dyfn_stub_\i
        gen_table_entry %(\i + 1)
    .endif
.endm

.data
.p2align 3
.global dyfn_stub_table
dyfn_stub_table:
    gen_table_entry 0

#endif // SBOX_NO_CALLBACKS

.section .note.GNU-stack,"",@progbits
