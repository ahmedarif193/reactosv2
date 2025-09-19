/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Assembly Header
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#ifndef __ASM_ARM64_H
#define __ASM_ARM64_H

/* ARM64 specific constants */
#define ARM64_CACHE_LINE_SIZE 64

/* System register definitions */
#define SCTLR_EL1_M     (1 << 0)   /* MMU enable */
#define SCTLR_EL1_A     (1 << 1)   /* Alignment check enable */
#define SCTLR_EL1_C     (1 << 2)   /* Data cache enable */
#define SCTLR_EL1_SA    (1 << 3)   /* Stack alignment check enable */
#define SCTLR_EL1_I     (1 << 12)  /* Instruction cache enable */

/* SPSR/PSTATE bits */
#define SPSR_EL0        0x0
#define SPSR_EL1        0x4
#define SPSR_EL2        0x8
#define SPSR_EL3        0xc

#define SPSR_N          (1 << 31)  /* Negative flag */
#define SPSR_Z          (1 << 30)  /* Zero flag */
#define SPSR_C          (1 << 29)  /* Carry flag */
#define SPSR_V          (1 << 28)  /* Overflow flag */

#define SPSR_D          (1 << 9)   /* Debug exception mask */
#define SPSR_A          (1 << 8)   /* SError (System Error) interrupt mask */
#define SPSR_I          (1 << 7)   /* IRQ interrupt mask */
#define SPSR_F          (1 << 6)   /* FIQ interrupt mask */

#define SPSR_DAIF       (SPSR_D | SPSR_A | SPSR_I | SPSR_F)

/* Exception Syndrome Register (ESR) bits */
#define ESR_EC_SHIFT    26
#define ESR_EC_MASK     0x3F
#define ESR_IL          (1 << 25)
#define ESR_ISS_MASK    0x1FFFFFF

/* Exception classes */
#define ESR_EC_UNKNOWN      0x00
#define ESR_EC_WFI_WFE      0x01
#define ESR_EC_SVC64        0x15
#define ESR_EC_HVC64        0x16
#define ESR_EC_SMC64        0x17
#define ESR_EC_IABT_LOW     0x20
#define ESR_EC_IABT_CUR     0x21
#define ESR_EC_PC_ALIGN     0x22
#define ESR_EC_DABT_LOW     0x24
#define ESR_EC_DABT_CUR     0x25
#define ESR_EC_SP_ALIGN     0x26
#define ESR_EC_FP_EXC32     0x28
#define ESR_EC_FP_EXC64     0x2C
#define ESR_EC_SERROR       0x2F
#define ESR_EC_BREAKPT_LOW  0x30
#define ESR_EC_BREAKPT_CUR  0x31
#define ESR_EC_SOFTSTEP_LOW 0x32
#define ESR_EC_SOFTSTEP_CUR 0x33
#define ESR_EC_WATCHPT_LOW  0x34
#define ESR_EC_WATCHPT_CUR  0x35
#define ESR_EC_BKPT32       0x38
#define ESR_EC_BRK64        0x3C

/* Current Exception Level */
#define CURRENT_EL_SHIFT    2
#define CURRENT_EL_MASK     0x3

/* Thread Information Block */
#define TEB_SELF            0x18

/* Kernel stack sizes */
#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE   0x4000  /* 16KB */
#endif
#define KERNEL_STACK_GUARD  0x1000  /* 4KB */

/* Include generated constants */
#ifdef __ASM__
#include "ksarm64.h"
#endif

/* Assembly macros */
#ifdef __ASM__

/* Function entry/exit macros */
.macro NESTED_ENTRY name
    .global \name
    .type \name, %function
    .align 4
\name:
    .cfi_startproc
.endm

.macro NESTED_END name
    .cfi_endproc
    .size \name, . - \name
.endm

.macro LEAF_ENTRY name
    .global \name
    .type \name, %function
    .align 4
\name:
    .cfi_startproc
.endm

.macro LEAF_END name
    .cfi_endproc
    .size \name, . - \name
.endm

/* Save/restore macros for general purpose registers */
.macro save_regs
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x8, x9, [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    stp     x20, x21, [sp, #-16]!
    stp     x22, x23, [sp, #-16]!
    stp     x24, x25, [sp, #-16]!
    stp     x26, x27, [sp, #-16]!
    stp     x28, x29, [sp, #-16]!
    str     x30, [sp, #-8]!
.endm

.macro restore_regs
    ldr     x30, [sp], #8
    ldp     x28, x29, [sp], #16
    ldp     x26, x27, [sp], #16
    ldp     x24, x25, [sp], #16
    ldp     x22, x23, [sp], #16
    ldp     x20, x21, [sp], #16
    ldp     x18, x19, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x8, x9, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
.endm

/* Interrupt enable/disable */
.macro enable_interrupts
    msr     daifclr, #2
.endm

.macro disable_interrupts
    msr     daifset, #2
.endm

/* Memory barrier instructions */
.macro dmb_sy
    dmb     sy
.endm

.macro dsb_sy
    dsb     sy
.endm

.macro isb_sy
    isb
.endm

#endif /* __ASM__ */

#endif /* __ASM_ARM64_H */