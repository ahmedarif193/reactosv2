/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Boot Code
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

    .text

/* INCLUDES ******************************************************************/
#include <arm64/arm64.h>

/* EXTERN FUNCTIONS **********************************************************/
    .extern KiSystemStartupBootStack
    .extern KiDisplayUnexpectedInterrupt

/* FUNCTIONS *****************************************************************/

/*
 * ARM64 Kernel Entry Point from FreeLDR
 * This is just a trampoline to the C function
 * x0 - LoaderBlock pointer
 */
    .globl KiSystemStartup
KiSystemStartup:
    /* Mask all exceptions */
    msr     daifset, #0xF
    isb

    /* Use SP_EL1 */
    msr     SPSel, #1
    isb

    /* Validate LoaderBlock parameter */
    cbz     x0, KiSystemStartup_InvalidParams

    /* Load kernel stack from LoaderBlock->KernelStack (offset 0x30) */
    ldr     x1, [x0, #0x30]
    cbz     x1, KiSystemStartup_InvalidParams

    /* Set up stack */
    mov     sp, x1

    /* Align stack to 16 bytes */
    mov     x2, sp
    mov     x3, #0xFFFFFFFFFFFFFFF0
    and     x2, x2, x3
    mov     sp, x2

    /* Preserve LoaderBlock */
    mov     x19, x0

    /* Print debug message */
    bl      KiEarlyDebugPrint

    /* Install exception vectors */
    bl      KiInitializeExceptionVectors

    /* Call kernel startup */
    mov     x0, x19
    bl      KiSystemStartupBootStack

    /* Should never reach here */
1:  wfi
    b       1b

KiSystemStartup_InvalidParams:
    bl      KiEarlyDebugPrintError
1:  wfi
    b       1b

/* Legacy entry point - also points to KiSystemStartup */
    .globl _start
_start:
    b       KiSystemStartup

/* Switch to boot stack */
    .globl KiSwitchToBootStack
KiSwitchToBootStack:
    msr     SPSel, #1
    mov     sp, x0
    mov     x1, sp
    mov     x2, #0xFFFFFFFFFFFFFFF0
    and     x1, x1, x2
    mov     sp, x1
    b       KiSystemStartupBootStack

/* Exception Vector Table */
    .align  11
    .globl  KiExceptionVectors
KiExceptionVectors:
    /* Current EL with SP0 - 0x000 */
    .align 7
    b       KiUnexpectedInterrupt
    /* IRQ SP0 - 0x080 */
    .align 7
    b       KiUnexpectedInterrupt
    /* FIQ SP0 - 0x100 */
    .align 7
    b       KiUnexpectedInterrupt
    /* SError SP0 - 0x180 */
    .align 7
    b       KiUnexpectedInterrupt

    /* Current EL with SPx - 0x200 */
    .align 7
    b       KiUnexpectedInterrupt
    /* IRQ SPx - 0x280 */
    .align 7
    b       KiUnexpectedInterrupt
    /* FIQ SPx - 0x300 */
    .align 7
    b       KiUnexpectedInterrupt
    /* SError SPx - 0x380 */
    .align 7
    b       KiUnexpectedInterrupt

    /* Lower EL AArch64 - 0x400 */
    .align 7
    b       KiUnexpectedInterrupt
    /* IRQ Lower64 - 0x480 */
    .align 7
    b       KiUnexpectedInterrupt
    /* FIQ Lower64 - 0x500 */
    .align 7
    b       KiUnexpectedInterrupt
    /* SError Lower64 - 0x580 */
    .align 7
    b       KiUnexpectedInterrupt

    /* Lower EL AArch32 - 0x600 */
    .align 7
    b       KiUnexpectedInterrupt
    /* IRQ Lower32 - 0x680 */
    .align 7
    b       KiUnexpectedInterrupt
    /* FIQ Lower32 - 0x700 */
    .align 7
    b       KiUnexpectedInterrupt
    /* SError Lower32 - 0x780 */
    .align 7
    b       KiUnexpectedInterrupt

/* Initialize exception vectors */
    .globl KiInitializeExceptionVectors
KiInitializeExceptionVectors:
    /* Get virtual address of exception vectors for TTBR1 mapping */
    adrp    x0, KiExceptionVectors
    add     x0, x0, :lo12:KiExceptionVectors

    /* Convert to kernel virtual address if in physical space */
    mov     x1, #0xFFFF800000000000  /* ARM64_KSEG0_BASE */
    cmp     x0, x1
    b.hs    1f                      /* Already virtual, skip conversion */
    add     x0, x0, x1              /* Convert PA to VA */
1:
    msr     vbar_el1, x0
    isb
    ret

/* Early debug print */
    .globl KiEarlyDebugPrint
KiEarlyDebugPrint:
    /* Save registers */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!

    /* UART base address */
    mov     x2, #0x09000000

    /* Print startup message */
    adr     x0, KiStartupMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    /* Restore registers */
    ldp     x30, xzr, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print error message */
    .globl KiEarlyDebugPrintError
KiEarlyDebugPrintError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiErrorMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Unexpected interrupt handler */
KiUnexpectedInterrupt:
    stp     x0, x1, [sp, #-16]!
    mov     x0, #0
    bl      KiDisplayUnexpectedInterrupt
    ldp     x0, x1, [sp], #16
1:  wfi
    b       1b

/* Debug messages */
KiStartupMsg:
    .asciz "ARM64 ReactOS Kernel Entry\r\n"
KiErrorMsg:
    .asciz "ARM64 Kernel: Invalid LoaderBlock or KernelStack\r\n"

    .end