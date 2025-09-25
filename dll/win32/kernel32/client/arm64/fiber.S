/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/kernel32/client/arm64/fiber.S
 * PURPOSE:     ARM64 Fiber Implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMERS: ReactOS Team
 */

#include <asm.inc>

/* TEB offsets for ARM64 */
#define TEB_SELF                        0x18
#define TEB_FIBER_DATA                  0x20
#define TEB_EXCEPTION_LIST             0x00
#define TEB_STACK_BASE                 0x08
#define TEB_STACK_LIMIT                0x10
#define TEB_DEALLOCATION_STACK         0x78
#define TEB_GUARANTEED_STACK_BYTES     0x78
#define TEB_FLS_DATA                   0x28
#define TEB_ACTIVATION_CONTEXT_PTR     0x2C8

/* FIBER structure offsets for ARM64 */
#define FIBER_FIBER_DATA               0x00
#define FIBER_EXCEPTION_LIST           0x08
#define FIBER_STACK_BASE               0x10
#define FIBER_STACK_LIMIT              0x18
#define FIBER_DEALLOCATION_STACK       0x20
#define FIBER_CONTEXT                  0x30
#define FIBER_GUARANTEED_STACK_BYTES   0x518
#define FIBER_FLS_DATA                 0x510
#define FIBER_ACTIVATION_CONTEXT_STACK 0x508

/* ARM64 CONTEXT offsets */
#define CONTEXT_FLAGS                  0x00
#define CONTEXT_X0                     0x08
#define CONTEXT_X1                     0x10
#define CONTEXT_X19                    0xA8
#define CONTEXT_X20                    0xB0
#define CONTEXT_X21                    0xB8
#define CONTEXT_X22                    0xC0
#define CONTEXT_X23                    0xC8
#define CONTEXT_X24                    0xD0
#define CONTEXT_X25                    0xD8
#define CONTEXT_X26                    0xE0
#define CONTEXT_X27                    0xE8
#define CONTEXT_X28                    0xF0
#define CONTEXT_X29                    0xF8  /* Frame pointer */
#define CONTEXT_LR                     0x100 /* x30 - Link Register */
#define CONTEXT_SP                     0x108 /* Stack pointer */
#define CONTEXT_PC                     0x110 /* Program counter */
#define CONTEXT_FPCR                   0x118 /* FP control register */
#define CONTEXT_FPSR                   0x11C /* FP status register */
#define CONTEXT_V                      0x120 /* Start of NEON registers (v0-v31) */

/* Context flags */
#define CONTEXT_FULL                   0x00000007
#define CONTEXT_FLOATING_POINT         0x00000004

.text

/*
 * VOID
 * BaseFiberStartup(VOID)
 */
PUBLIC BaseFiberStartup
FUNC BaseFiberStartup
    /* Get the current fiber from TEB */
    /* On ARM64, TEB is in TPIDR_EL0 register */
    mrs x9, tpidr_el0                          /* Get TEB pointer */
    ldr x10, [x9, #TEB_FIBER_DATA]            /* Get current fiber */

    /* Load the fiber start address and parameter */
    /* FiberContext contains the startup info in x0 (parameter) and x1 (start address) */
    add x11, x10, #FIBER_CONTEXT              /* Point to context */
    ldr x0, [x11, #CONTEXT_X0]                /* Load parameter */
    ldr x1, [x11, #CONTEXT_X1]                /* Load start address */

    /* Clear frame pointer for proper stack traces */
    mov x29, #0

    /* Call BaseThreadStartup with fiber's parameter and start routine */
    /* BaseThreadStartup expects: x0 = start routine, x1 = parameter, x2 = 0 */
    mov x2, #0
    mov x3, x1                                 /* Save start routine */
    mov x1, x0                                 /* Move parameter to x1 */
    mov x0, x3                                 /* Move start routine to x0 */
    bl BaseThreadStartup

    /* We should never get here, but if we do, exit the thread */
    bl ExitThread

    /* Should never reach here */
1:  b 1b

ENDFUNC

/*
 * VOID
 * SwitchToFiber(LPVOID lpFiber)
 *
 * x0 = lpFiber (new fiber to switch to)
 */
PUBLIC SwitchToFiber
FUNC SwitchToFiber
    /* Get the TEB */
    mrs x1, tpidr_el0                          /* x1 = TEB pointer */

    /* Get current fiber */
    ldr x2, [x1, #TEB_FIBER_DATA]             /* x2 = current fiber */

    /* Save callee-saved general purpose registers (x19-x30) */
    add x3, x2, #FIBER_CONTEXT                /* x3 = &current_fiber->Context */
    add x3, x3, #CONTEXT_X19                  /* Point to x19 in context */
    stp x19, x20, [x3, #0x00]
    stp x21, x22, [x3, #0x10]
    stp x23, x24, [x3, #0x20]
    stp x25, x26, [x3, #0x30]
    stp x27, x28, [x3, #0x40]
    stp x29, x30, [x3, #0x50]                 /* x29 = FP, x30 = LR */

    /* Save stack pointer */
    mov x4, sp
    add x3, x2, #FIBER_CONTEXT
    str x4, [x3, #CONTEXT_SP]

    /* Check if we need to save floating point state */
    ldr w4, [x3, #CONTEXT_FLAGS]
    mov w5, #(CONTEXT_FULL | CONTEXT_FLOATING_POINT)
    cmp w4, w5
    b.ne .LNoFpuSave

    /* Save floating point control and status registers */
    mrs x4, fpcr
    mrs x5, fpsr
    add x6, x3, #CONTEXT_FPCR
    str w4, [x6]
    str w5, [x6, #4]

    /* Save NEON/FP registers (v8-v15 are callee-saved) */
    add x4, x3, #CONTEXT_V
    add x4, x4, #(8 * 16)                     /* Point to v8 */
    stp q8,  q9,  [x4, #0x00]
    stp q10, q11, [x4, #0x20]
    stp q12, q13, [x4, #0x40]
    stp q14, q15, [x4, #0x60]

.LNoFpuSave:
    /* Save TEB fiber-related data to current fiber */
    ldr x4, [x1, #TEB_FLS_DATA]
    str x4, [x2, #FIBER_FLS_DATA]
    ldr x4, [x1, #TEB_ACTIVATION_CONTEXT_PTR]
    str x4, [x2, #FIBER_ACTIVATION_CONTEXT_STACK]

    /* Save stack information */
    ldr x4, [x1, #TEB_EXCEPTION_LIST]
    str x4, [x2, #FIBER_EXCEPTION_LIST]
    ldr x4, [x1, #TEB_STACK_LIMIT]
    str x4, [x2, #FIBER_STACK_LIMIT]
    ldr x4, [x1, #TEB_GUARANTEED_STACK_BYTES]
    str x4, [x2, #FIBER_GUARANTEED_STACK_BYTES]

    /* Switch to new fiber */
    str x0, [x1, #TEB_FIBER_DATA]             /* Set new fiber in TEB */

    /* Load new fiber's TEB data */
    ldr x4, [x0, #FIBER_EXCEPTION_LIST]
    str x4, [x1, #TEB_EXCEPTION_LIST]
    ldr x4, [x0, #FIBER_STACK_BASE]
    str x4, [x1, #TEB_STACK_BASE]
    ldr x4, [x0, #FIBER_STACK_LIMIT]
    str x4, [x1, #TEB_STACK_LIMIT]
    ldr x4, [x0, #FIBER_DEALLOCATION_STACK]
    str x4, [x1, #TEB_DEALLOCATION_STACK]
    ldr x4, [x0, #FIBER_GUARANTEED_STACK_BYTES]
    str x4, [x1, #TEB_GUARANTEED_STACK_BYTES]
    ldr x4, [x0, #FIBER_ACTIVATION_CONTEXT_STACK]
    str x4, [x1, #TEB_ACTIVATION_CONTEXT_PTR]

    /* Get new fiber's context */
    add x3, x0, #FIBER_CONTEXT                /* x3 = &new_fiber->Context */

    /* Check if we need to restore floating point state */
    ldr w4, [x3, #CONTEXT_FLAGS]
    mov w5, #(CONTEXT_FULL | CONTEXT_FLOATING_POINT)
    cmp w4, w5
    b.ne .LNoFpuRestore

    /* Restore floating point control and status registers */
    add x6, x3, #CONTEXT_FPCR
    ldr w4, [x6]
    ldr w5, [x6, #4]
    msr fpcr, x4
    msr fpsr, x5

    /* Restore NEON/FP registers (v8-v15 are callee-saved) */
    add x4, x3, #CONTEXT_V
    add x4, x4, #(8 * 16)                     /* Point to v8 */
    ldp q8,  q9,  [x4, #0x00]
    ldp q10, q11, [x4, #0x20]
    ldp q12, q13, [x4, #0x40]
    ldp q14, q15, [x4, #0x60]

.LNoFpuRestore:
    /* Restore FLS data */
    ldr x4, [x0, #FIBER_FLS_DATA]
    str x4, [x1, #TEB_FLS_DATA]

    /* Restore callee-saved general purpose registers */
    add x3, x3, #CONTEXT_X19                  /* Point to x19 in context */
    ldp x19, x20, [x3, #0x00]
    ldp x21, x22, [x3, #0x10]
    ldp x23, x24, [x3, #0x20]
    ldp x25, x26, [x3, #0x30]
    ldp x27, x28, [x3, #0x40]
    ldp x29, x30, [x3, #0x50]                 /* x29 = FP, x30 = LR */

    /* Restore stack pointer */
    add x3, x0, #FIBER_CONTEXT
    ldr x4, [x3, #CONTEXT_SP]
    mov sp, x4

    /* Return to the new fiber */
    ret

ENDFUNC

END