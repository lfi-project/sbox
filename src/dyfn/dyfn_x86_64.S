#include "dyfn_offsets.h"

.text

// dyfn_call(DyfnCallArgs* args, DyfnCallResult* result)
.global dyfn_call
dyfn_call:
    push %rbp
    mov  %rsp, %rbp
    push %rbx
    push %r12

    mov  %rdi, %rbx
    mov  %rsi, %r12

    movsd  (DYFN_CALL_FLOAT_REGS + 0)(%rbx), %xmm0
    movsd  (DYFN_CALL_FLOAT_REGS + 8)(%rbx), %xmm1
    movsd  (DYFN_CALL_FLOAT_REGS + 16)(%rbx), %xmm2
    movsd  (DYFN_CALL_FLOAT_REGS + 24)(%rbx), %xmm3
    movsd  (DYFN_CALL_FLOAT_REGS + 32)(%rbx), %xmm4
    movsd  (DYFN_CALL_FLOAT_REGS + 40)(%rbx), %xmm5
    movsd  (DYFN_CALL_FLOAT_REGS + 48)(%rbx), %xmm6
    movsd  (DYFN_CALL_FLOAT_REGS + 56)(%rbx), %xmm7

    movl DYFN_CALL_STACK_COUNT(%rbx), %eax
    test %eax, %eax
    jz   .Lno_stack

    // Pad if odd stack_count to maintain 16-byte alignment.
    test $1, %eax
    jz   .Lparity_ok
    sub  $8, %rsp
.Lparity_ok:
    lea  DYFN_CALL_STACK_ARGS(%rbx), %rcx
    lea  (%rcx,%rax,8), %rdx
.Lpush:
    sub  $8, %rdx
    pushq (%rdx)
    cmp  %rcx, %rdx
    jne  .Lpush
.Lno_stack:

    mov  (DYFN_CALL_INT_REGS + 0)(%rbx), %rdi
    mov  (DYFN_CALL_INT_REGS + 8)(%rbx), %rsi
    mov  (DYFN_CALL_INT_REGS + 16)(%rbx), %rdx
    mov  (DYFN_CALL_INT_REGS + 24)(%rbx), %rcx
    mov  (DYFN_CALL_INT_REGS + 32)(%rbx), %r8
    mov  (DYFN_CALL_INT_REGS + 40)(%rbx), %r9

    call *DYFN_CALL_FUNC(%rbx)

    mov   %rax, DYFN_RESULT_INT_VAL(%r12)
    movsd %xmm0, DYFN_RESULT_FLOAT_VAL(%r12)

    lea  -16(%rbp), %rsp
    pop  %r12
    pop  %rbx
    pop  %rbp
    ret

#ifndef SBOX_NO_CALLBACKS

.altmacro

.macro closure_stub i
    .p2align 4
    .global dyfn_stub_\i
    dyfn_stub_\i:
        movl $\i, %r10d
        jmp  dyfn_closure_common
.endm

.macro gen_stubs i
    .if \i < DYFN_MAX_CLOSURES_ASM
        closure_stub \i
        gen_stubs %(\i + 1)
    .endif
.endm

gen_stubs 0

// Common closure handler.
// On entry: %r10d = stub index, all arg registers live from caller.
// Stack has caller's return address at (%rsp), so %rsp % 16 == 8.
//
// Stack layout (offsets from %rsp after allocation):
//   0                              : DyfnClosureSavedRegs
//   DYFN_SIZEOF_CLOSURE_SAVED_REGS : DyfnClosureResult

#define SAVED_BASE  0
#define RESULT_BASE DYFN_SIZEOF_CLOSURE_SAVED_REGS

dyfn_closure_common:
    // +8 for alignment: return address makes %rsp 8-misaligned on entry
    sub  $(DYFN_CLOSURE_FRAME_SIZE + 8), %rsp

    mov  %rdi, (SAVED_BASE + DYFN_SAVED_INT_REGS + 0)(%rsp)
    mov  %rsi, (SAVED_BASE + DYFN_SAVED_INT_REGS + 8)(%rsp)
    mov  %rdx, (SAVED_BASE + DYFN_SAVED_INT_REGS + 16)(%rsp)
    mov  %rcx, (SAVED_BASE + DYFN_SAVED_INT_REGS + 24)(%rsp)
    mov  %r8,  (SAVED_BASE + DYFN_SAVED_INT_REGS + 32)(%rsp)
    mov  %r9,  (SAVED_BASE + DYFN_SAVED_INT_REGS + 40)(%rsp)

#define CALLER_STACK_BASE (DYFN_CLOSURE_FRAME_SIZE + 8 + 8)
    mov  (CALLER_STACK_BASE + 0)(%rsp), %rax
    mov  %rax, (SAVED_BASE + DYFN_SAVED_STACK_ARGS + 0)(%rsp)
    mov  (CALLER_STACK_BASE + 8)(%rsp), %rax
    mov  %rax, (SAVED_BASE + DYFN_SAVED_STACK_ARGS + 8)(%rsp)
    mov  (CALLER_STACK_BASE + 16)(%rsp), %rax
    mov  %rax, (SAVED_BASE + DYFN_SAVED_STACK_ARGS + 16)(%rsp)
    mov  (CALLER_STACK_BASE + 24)(%rsp), %rax
    mov  %rax, (SAVED_BASE + DYFN_SAVED_STACK_ARGS + 24)(%rsp)

    movsd %xmm0, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 0)(%rsp)
    movsd %xmm1, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 8)(%rsp)
    movsd %xmm2, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 16)(%rsp)
    movsd %xmm3, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 24)(%rsp)
    movsd %xmm4, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 32)(%rsp)
    movsd %xmm5, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 40)(%rsp)
    movsd %xmm6, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 48)(%rsp)
    movsd %xmm7, (SAVED_BASE + DYFN_SAVED_FLOAT_REGS + 56)(%rsp)

    movl %r10d, (SAVED_BASE + DYFN_SAVED_STUB_INDEX)(%rsp)

    lea  SAVED_BASE(%rsp), %rdi
    lea  RESULT_BASE(%rsp), %rsi
    call dyfn_closure_dispatch

    mov   (RESULT_BASE + DYFN_CLOSURE_INT_VAL)(%rsp), %rax
    movsd (RESULT_BASE + DYFN_CLOSURE_FLOAT_VAL)(%rsp), %xmm0

    add  $(DYFN_CLOSURE_FRAME_SIZE + 8), %rsp
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
