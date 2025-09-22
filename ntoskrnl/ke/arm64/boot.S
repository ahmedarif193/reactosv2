/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Boot Code and Startup Trampoline
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

    .text

/* INCLUDES ******************************************************************/
#include <arm64/arm64.h>

/* EXTERN FUNCTIONS **********************************************************/
    .extern KiSystemStartupBootStack
    .extern KiTrapHandler
    .extern KiInterruptHandler
    .extern KiFiqHandler
    .extern KiSerrorHandler
    .extern KiSystemCallHandler64
    .extern KiSystemCallHandler32
    .extern KiDisplayUnexpectedInterrupt

/* FUNCTIONS *****************************************************************/

/*
 * ARM64 Kernel Entry Point from FreeLDR
 * This is the main entry point expected by PE loader
 * x0 - LoaderBlock pointer
 * x1 - Initial stack pointer
 */
    .globl KiSystemStartup
    .type  KiSystemStartup, %function
KiSystemStartup:
    /* Mask all exceptions first */
    msr     daifset, #0xF
    /* Add instruction synchronization barrier after exception masking */
    isb

    /* Use SP_ELx (SP_EL1 at EL1) to avoid accidental SP0 */
    msr     SPSel, #1
    isb

    /* Validate parameters before proceeding */
    /* Check if LoaderBlock is NULL */
    cbz     x0, KiSystemStartup_InvalidParams
    /* Check if stack pointer is NULL */
    cbz     x1, KiSystemStartup_InvalidParams
    /* Check if stack is properly aligned (16-byte boundary) */
    tst     x1, #0xF
    b.ne    KiSystemStartup_InvalidParams

    /* Set up initial stack */
    mov     sp, x1
    /* Ensure stack alignment to 16 bytes */
    and     sp, sp, #-16

    /* Preserve LoaderBlock pointer */
    mov     x19, x0

    /* Print early debug message to track execution */
    bl      KiEarlyDebugPrint

    /* Install exception vectors early */
    bl      KiInitializeExceptionVectors

    /* Call kernel startup with LoaderBlock */
    mov     x0, x19
    bl      KiSystemStartupBootStack

    /* Should never reach here - infinite loop with low power */
1:  wfi
    b       1b

/* Handle invalid parameters */
KiSystemStartup_InvalidParams:
    /* Try to output error message if possible */
    bl      KiEarlyDebugPrintError
    /* Infinite loop */
1:  wfi
    b       1b

/*
 * Legacy entry point for compatibility
 * Forward to the new entry point
 */
    .globl _start
    .type  _start, %function
_start:
    b       KiSystemStartup

/*
 * Switch to boot stack
 * x0 - new stack pointer
 */
    .globl KiSwitchToBootStack
    .type  KiSwitchToBootStack, %function
KiSwitchToBootStack:
    msr     SPSel, #1           /* Ensure SP_EL1 selected */
    mov     sp, x0
    and     sp, sp, #-16
    b       KiSystemStartupBootStack

/* -------------------------------------------------------------------------- */
/* Exception Vector Table (2 KiB aligned)                                     */
/* -------------------------------------------------------------------------- */

    .align  11
    .globl  KiExceptionVectors
    .type   KiExceptionVectors, %object
KiExceptionVectors:

    /* Helper macro: place a branch, then pad to next 0x80 boundary */
    .macro VENTRY target
        b \target
        .balign 0x80
    .endm

    /* Current EL with SP_EL0 (SP0) */
    VENTRY  KiSynchronousExceptionSP0    /* +0x000 */
    VENTRY  KiIrqExceptionSP0            /* +0x080 */
    VENTRY  KiFiqExceptionSP0            /* +0x100 */
    VENTRY  KiSerrorExceptionSP0         /* +0x180 */

    /* Current EL with SP_ELx (SPx) */
    VENTRY  KiSynchronousExceptionSPx    /* +0x200 */
    VENTRY  KiIrqExceptionSPx            /* +0x280 */
    VENTRY  KiFiqExceptionSPx            /* +0x300 */
    VENTRY  KiSerrorExceptionSPx         /* +0x380 */

    /* Lower EL using AArch64 */
    VENTRY  KiSynchronousExceptionLowerAArch64 /* +0x400 */
    VENTRY  KiIrqExceptionLowerAArch64         /* +0x480 */
    VENTRY  KiFiqExceptionLowerAArch64         /* +0x500 */
    VENTRY  KiSerrorExceptionLowerAArch64      /* +0x580 */

    /* Lower EL using AArch32 */
    VENTRY  KiSynchronousExceptionLowerAArch32 /* +0x600 */
    VENTRY  KiIrqExceptionLowerAArch32         /* +0x680 */
    VENTRY  KiFiqExceptionLowerAArch32         /* +0x700 */
    /* Last entry ends exactly at +0x780; no extra padding needed */
    b       KiSerrorExceptionLowerAArch32      /* +0x780 */

    .size   KiExceptionVectors, . - KiExceptionVectors

/* -------------------------------------------------------------------------- */
/* Vector base setup                                                          */
/* -------------------------------------------------------------------------- */

    .globl KiInitializeExceptionVectors
    .type  KiInitializeExceptionVectors, %function
KiInitializeExceptionVectors:
    adrp    x0, KiExceptionVectors
    add     x0, x0, :lo12:KiExceptionVectors   /* NOTE: no '#' before :lo12: */
    msr     vbar_el1, x0
    isb
    ret

/* -------------------------------------------------------------------------- */
/* Early debug output functions                                               */
/* -------------------------------------------------------------------------- */

/*
 * Early debug print function - prints startup message via UART
 * Uses PL011 UART at 0x09000000 (QEMU virt default)
 * Preserves all registers
 */
    .globl KiEarlyDebugPrint
    .type  KiEarlyDebugPrint, %function
KiEarlyDebugPrint:
    /* Save registers */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!

    /* Print startup message */
    adr     x0, KiStartupMessage
    bl      KiUartPrintString

    /* Print LoaderBlock address */
    adr     x0, KiLoaderBlockMsg
    bl      KiUartPrintString
    mov     x0, x19  /* LoaderBlock address */
    bl      KiUartPrintHex64
    mov     x0, #0x0A  /* Newline */
    bl      KiUartPrintChar

    /* Print stack pointer */
    adr     x0, KiStackPtrMsg
    bl      KiUartPrintString
    mov     x0, sp
    bl      KiUartPrintHex64
    mov     x0, #0x0A  /* Newline */
    bl      KiUartPrintChar

    /* Restore registers */
    ldp     x30, xzr, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/*
 * Early debug error print function
 */
    .globl KiEarlyDebugPrintError
    .type  KiEarlyDebugPrintError, %function
KiEarlyDebugPrintError:
    /* Save minimal registers */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!

    /* Print error message */
    adr     x0, KiErrorMessage
    bl      KiUartPrintString

    /* Restore registers */
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/*
 * Print a null-terminated string via UART
 * x0 = string pointer
 */
KiUartPrintString:
    stp     x1, x2, [sp, #-16]!
    mov     x2, #0x09000000      /* PL011 UART base */
1:
    ldrb    w1, [x0], #1         /* Load next character */
    cbz     w1, 2f               /* Exit if null terminator */
    str     w1, [x2]             /* Write to UART data register */
    /* Simple delay */
    mov     x1, #1000
3:  subs    x1, x1, #1
    b.ne    3b
    b       1b                   /* Continue with next character */
2:
    ldp     x1, x2, [sp], #16
    ret

/*
 * Print a single character via UART
 * x0 = character
 */
KiUartPrintChar:
    stp     x1, x2, [sp, #-16]!
    mov     x2, #0x09000000      /* PL011 UART base */
    str     w0, [x2]             /* Write character to UART */
    /* Simple delay */
    mov     x1, #1000
1:  subs    x1, x1, #1
    b.ne    1b
    ldp     x1, x2, [sp], #16
    ret

/*
 * Print 64-bit hex value via UART
 * x0 = value to print
 */
KiUartPrintHex64:
    stp     x1, x2, [sp, #-16]!
    stp     x3, x4, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!

    mov     x3, x0               /* Save value */
    mov     x4, #16              /* 16 hex digits */

    /* Print "0x" prefix */
    mov     x0, #'0'
    bl      KiUartPrintChar
    mov     x0, #'x'
    bl      KiUartPrintChar

1:
    /* Extract top 4 bits */
    lsr     x1, x3, #60
    and     x1, x1, #0xF

    /* Convert to ASCII */
    cmp     x1, #10
    b.lt    2f
    add     x1, x1, #('A' - '0' - 10)
2:
    add     x0, x1, #'0'
    bl      KiUartPrintChar

    /* Shift value left by 4 bits */
    lsl     x3, x3, #4
    subs    x4, x4, #1
    b.ne    1b

    ldp     x30, xzr, [sp], #16
    ldp     x3, x4, [sp], #16
    ldp     x1, x2, [sp], #16
    ret

/* Debug messages */
KiStartupMessage:
    .asciz "ARM64 ReactOS Kernel Entry\r\n"
KiLoaderBlockMsg:
    .asciz "LoaderBlock: "
KiStackPtrMsg:
    .asciz "Stack: "
KiErrorMessage:
    .asciz "ARM64 Kernel: FATAL - Invalid parameters\r\n"

/* -------------------------------------------------------------------------- */
/* Minimal handler stubs                                                      */
/* -------------------------------------------------------------------------- */

KiSynchronousExceptionSP0:
    b   KiUnexpectedInterrupt
KiIrqExceptionSP0:
    b   KiUnexpectedInterrupt
KiFiqExceptionSP0:
    b   KiUnexpectedInterrupt
KiSerrorExceptionSP0:
    b   KiUnexpectedInterrupt

KiSynchronousExceptionSPx:
    b   KiTrapHandler
KiIrqExceptionSPx:
    b   KiInterruptHandler
KiFiqExceptionSPx:
    b   KiFiqHandler
KiSerrorExceptionSPx:
    b   KiSerrorHandler

KiSynchronousExceptionLowerAArch64:
    b   KiSystemCallHandler64
KiIrqExceptionLowerAArch64:
    b   KiInterruptHandler
KiFiqExceptionLowerAArch64:
    b   KiFiqHandler
KiSerrorExceptionLowerAArch64:
    b   KiSerrorHandler

KiSynchronousExceptionLowerAArch32:
    b   KiSystemCallHandler32
KiIrqExceptionLowerAArch32:
    b   KiInterruptHandler
KiFiqExceptionLowerAArch32:
    b   KiFiqHandler
KiSerrorExceptionLowerAArch32:
    b   KiSerrorHandler

/* Temporary catch-all */
    .type  KiUnexpectedInterrupt, %function
KiUnexpectedInterrupt:
    /* Preserve a few regs; stack is 16-byte aligned already */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    mov x0, #0
    bl  KiDisplayUnexpectedInterrupt
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
1:  wfi
    b   1b

    .end
