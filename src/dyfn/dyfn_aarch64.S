#include "dyfn_offsets.h"

// Mark stack as non-executable
.section .note.GNU-stack,"",@progbits

.text

// dyfn_call(DyfnCallArgs* args, DyfnCallResult* result)
// x0 = args, x1 = result
.global dyfn_call
dyfn_call:
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    stp  x19, x20, [sp, #16]

    mov  x19, x0          // x19 = args
    mov  x20, x1          // x20 = result

    // Load all float regs unconditionally (harmless if unused)
    ldp  d0, d1, [x19, #(DYFN_CALL_FLOAT_REGS + 0)]
    ldp  d2, d3, [x19, #(DYFN_CALL_FLOAT_REGS + 16)]
    ldp  d4, d5, [x19, #(DYFN_CALL_FLOAT_REGS + 32)]
    ldp  d6, d7, [x19, #(DYFN_CALL_FLOAT_REGS + 48)]

    // Load function pointer into scratch register before clobbering x0
    ldr  x9, [x19, #DYFN_CALL_FUNC]

    // Load integer arg regs
    ldp  x0, x1, [x19, #(DYFN_CALL_INT_REGS + 0)]
    ldp  x2, x3, [x19, #(DYFN_CALL_INT_REGS + 16)]
    ldp  x4, x5, [x19, #(DYFN_CALL_INT_REGS + 32)]
    ldp  x6, x7, [x19, #(DYFN_CALL_INT_REGS + 48)]

    // Call the function
    blr  x9

    // Store results
    str  x0, [x20, #DYFN_RESULT_INT_VAL]
    str  d0, [x20, #DYFN_RESULT_FLOAT_VAL]

    // Restore callee-saved registers
    ldp  x19, x20, [sp, #16]
    ldp  x29, x30, [sp], #32
    ret

#ifndef SBOX_NO_CALLBACKS

// --- Closure stubs ---

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
// Stack layout (offsets from sp after allocation):
//   0   : x29, x30 save
//   16  : DyfnClosureSavedRegs  (136 bytes)
//   152 : DyfnClosureResult     (24 bytes)
//   Total: 176 bytes (16-byte aligned)

#define CLOSURE_TOTAL_SIZE (16 + DYFN_CLOSURE_FRAME_SIZE)
#define SAVED_BASE  16
#define RESULT_BASE (16 + DYFN_SIZEOF_CLOSURE_SAVED_REGS)

dyfn_closure_common:
    stp  x29, x30, [sp, #-CLOSURE_TOTAL_SIZE]!
    mov  x29, sp

    // Save integer arg registers
    stp  x0, x1, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 0)]
    stp  x2, x3, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 16)]
    stp  x4, x5, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 32)]
    stp  x6, x7, [sp, #(SAVED_BASE + DYFN_SAVED_INT_REGS + 48)]

    // Save float arg registers
    stp  d0, d1, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 0)]
    stp  d2, d3, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 16)]
    stp  d4, d5, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 32)]
    stp  d6, d7, [sp, #(SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 48)]

    // Save stub index
    str  w9, [sp, #(SAVED_BASE + DYFN_SAVED_STUB_INDEX)]

    // Call dyfn_closure_dispatch(&saved_regs, &result)
    add  x0, sp, #SAVED_BASE
    add  x1, sp, #RESULT_BASE
    bl   dyfn_closure_dispatch

    // Load return values
    ldr  x0, [sp, #(RESULT_BASE + DYFN_CLOSURE_INT_VAL)]
    ldr  d0, [sp, #(RESULT_BASE + DYFN_CLOSURE_FLOAT_VAL)]

    // Restore frame and return
    ldp  x29, x30, [sp], #CLOSURE_TOTAL_SIZE
    ret

// --- Stub table ---

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
