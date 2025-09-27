/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Boot Code
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
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
    /* Mask all exceptions immediately */
    msr     daifset, #0xF
    isb

    /* Verify we're running at EL1 */
    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #1
    b.ne    KiSystemStartup_WrongEL

    /* Check if this is the boot CPU (CPU 0) */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xFF           /* Extract Aff0 (CPU ID within cluster) */
    cbnz    x1, KiSecondaryProcessor

    /* Use SP_EL1 (kernel stack) */
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

    /* Initialize system registers */
    bl      KiInitializeSystemRegisters

    /* Install exception vectors */
    bl      KiInitializeExceptionVectors

    /* Enable floating point/SIMD */
    bl      KiInitializeFpu

    /* Verify UEFI handoff state compatibility */
    bl      KiVerifyUEFIHandoffState

    /* Initialize caches and memory barriers */
    bl      KiInitializeCachesAndMMU

    /* Call kernel startup */
    mov     x0, x19
    bl      KiSystemStartupBootStack

    /* Should never reach here */
1:  wfi
    b       1b

KiSystemStartup_InvalidParams:
    bl      KiEarlyDebugPrintError
    b       KiSystemStartup_Halt

KiSecondaryProcessor:
    /* Secondary processors should not boot from this entry point */
    /* They should use a different entry point for SMP initialization */
    bl      KiEarlyDebugPrintSecondaryError
    b       KiSystemStartup_Halt

KiSystemStartup_WrongEL:
    bl      KiEarlyDebugPrintWrongEL
    b       KiSystemStartup_Halt

KiSystemStartup_Halt:
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

/* Exception Vector Table - Must be 2KB (0x800) aligned */
    .align  11  /* 2^11 = 2048 bytes alignment */
    .globl  KiExceptionVectors
KiExceptionVectors:
    /* Current EL with SP0 - 0x000 */
    .align 7    /* Each entry must be 128 bytes (2^7) aligned */
vec_sync_sp0:
    b       KiTrapHandler
    .balign 128 /* Ensure exact 128-byte spacing */

    /* IRQ Current EL SP0 - 0x080 */
vec_irq_sp0:
    b       KiInterruptHandler
    .balign 128

    /* FIQ Current EL SP0 - 0x100 */
vec_fiq_sp0:
    b       KiFiqHandler
    .balign 128

    /* SError Current EL SP0 - 0x180 */
vec_serror_sp0:
    b       KiSerrorHandler
    .balign 128

    /* Current EL with SPx - 0x200 */
vec_sync_spx:
    b       KiTrapHandler
    .balign 128

    /* IRQ Current EL SPx - 0x280 */
vec_irq_spx:
    b       KiInterruptHandler
    .balign 128

    /* FIQ Current EL SPx - 0x300 */
vec_fiq_spx:
    b       KiFiqHandler
    .balign 128

    /* SError Current EL SPx - 0x380 */
vec_serror_spx:
    b       KiSerrorHandler
    .balign 128

    /* Lower EL AArch64 - 0x400 */
vec_sync_lower64:
    b       KiSystemCallHandler64
    .balign 128

    /* IRQ Lower EL AArch64 - 0x480 */
vec_irq_lower64:
    b       KiInterruptHandler
    .balign 128

    /* FIQ Lower EL AArch64 - 0x500 */
vec_fiq_lower64:
    b       KiFiqHandler
    .balign 128

    /* SError Lower EL AArch64 - 0x580 */
vec_serror_lower64:
    b       KiSerrorHandler
    .balign 128

    /* Lower EL AArch32 - 0x600 */
vec_sync_lower32:
    b       KiSystemCallHandler32
    .balign 128

    /* IRQ Lower EL AArch32 - 0x680 */
vec_irq_lower32:
    b       KiInterruptHandler
    .balign 128

    /* FIQ Lower EL AArch32 - 0x700 */
vec_fiq_lower32:
    b       KiFiqHandler
    .balign 128

    /* SError Lower EL AArch32 - 0x780 */
vec_serror_lower32:
    b       KiSerrorHandler
    .balign 128

    /* NOTE: Vector table should be exactly 2KB (0x800 bytes)
     * This is enforced by the .balign directives above */

/* Initialize system registers */
    .globl KiInitializeSystemRegisters
KiInitializeSystemRegisters:
    /* Save return address */
    str     x30, [sp, #-16]!

    /* Set up CPACR_EL1 - Enable FP/SIMD access */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)     /* FPEN = 11b (no trapping) */
    msr     cpacr_el1, x0
    isb

    /* Restore return address and return */
    ldr     x30, [sp], #16
    ret

/* Initialize floating point unit */
    .globl KiInitializeFpu
KiInitializeFpu:
    /* Save return address */
    str     x30, [sp, #-16]!

    /* Initialize FPCR (Floating Point Control Register) */
    mov     x0, #0
    msr     fpcr, x0

    /* Initialize FPSR (Floating Point Status Register) */
    msr     fpsr, x0

    isb

    /* Restore return address and return */
    ldr     x30, [sp], #16
    ret

/* Initialize caches and MMU for kernel handoff */
    .globl KiInitializeCachesAndMMU
KiInitializeCachesAndMMU:
    /* Save return address and callee-saved registers */
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    /* Ensure all previous operations complete */
    dsb     sy
    isb

    /* Check if MMU is already enabled */
    mrs     x19, sctlr_el1
    tbnz    x19, #0, .Lmmu_already_enabled  /* M bit is bit 0 */

    /* MMU is disabled - this is expected from bootloader */
    /* We will let the kernel's MM subsystem enable it properly */
    b       .Lcache_init

.Lmmu_already_enabled:
    /* MMU is already enabled by bootloader - verify basic setup */
    /* Check if we have valid TTBR1_EL1 for kernel space */
    mrs     x20, ttbr1_el1
    cbz     x20, .Lttbr_error

.Lcache_init:
    /* Enable instruction cache if not already enabled */
    orr     x19, x19, #(1 << 12)   /* I bit - Instruction cache enable */

    /* Set other important SCTLR_EL1 bits for kernel operation */
    orr     x19, x19, #(1 << 3)    /* SA bit - Stack alignment check enable */
    orr     x19, x19, #(1 << 4)    /* SA0 bit - Stack alignment check enable EL0 */
    orr     x19, x19, #(1 << 2)    /* C bit - Data cache enable (if MMU enabled) */

    /* Ensure exception endianness is little endian */
    bic     x19, x19, #(1 << 25)   /* Clear EE bit */
    bic     x19, x19, #(1 << 24)   /* Clear E0E bit */

    /* Write back SCTLR_EL1 */
    msr     sctlr_el1, x19
    isb

    /* Invalidate instruction caches to ensure clean state */
    ic      iallu
    dsb     sy
    isb

    /* Restore registers and return */
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

.Lttbr_error:
    bl      KiEarlyDebugPrintTTBRError
    b       KiSystemStartup_Halt

/* Verify UEFI handoff state compatibility */
    .globl KiVerifyUEFIHandoffState
KiVerifyUEFIHandoffState:
    /* Save return address and registers */
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    /* Check that interrupts are disabled (DAIF should be all set) */
    mrs     x19, daif
    and     x19, x19, #0x3C0        /* Check D, A, I, F bits (bits [9:6]) */
    cmp     x19, #0x3C0
    b.ne    .Linterrupts_not_disabled

    /* Check that we're using the right stack pointer */
    mrs     x19, spsel
    tbnz    x19, #0, .Lstack_ok     /* SPSel should be 1 (SP_EL1) */
    b       .Lstack_error

.Lstack_ok:
    /* Verify stack alignment (should be 16-byte aligned) */
    mov     x19, sp
    and     x19, x19, #0xF
    cbnz    x19, .Lstack_align_error

    /* All checks passed */
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

.Linterrupts_not_disabled:
    bl      KiEarlyDebugPrintInterruptError
    b       KiSystemStartup_Halt

.Lstack_error:
    bl      KiEarlyDebugPrintStackError
    b       KiSystemStartup_Halt

.Lstack_align_error:
    bl      KiEarlyDebugPrintStackAlignError
    b       KiSystemStartup_Halt

/* Initialize exception vectors */
    .globl KiInitializeExceptionVectors
KiInitializeExceptionVectors:
    /* Save return address */
    str     x30, [sp, #-16]!

    /* Verify vector table alignment - must be 2KB aligned */
    adrp    x0, KiExceptionVectors
    add     x0, x0, :lo12:KiExceptionVectors

    /* Check 2KB alignment (bits [10:0] must be zero) */
    mov     x1, #0x7FF
    tst     x0, x1
    b.ne    KiVectorAlignmentError

    /* Convert to kernel virtual address if in physical space */
    mov     x1, #0xFFFF800000000000  /* ARM64_KSEG0_BASE */
    cmp     x0, x1
    b.hs    1f                      /* Already virtual, skip conversion */
    add     x0, x0, x1              /* Convert PA to VA */
1:
    /* Install vector table */
    msr     vbar_el1, x0
    isb

    /* Restore return address and return */
    ldr     x30, [sp], #16
    ret

KiVectorAlignmentError:
    bl      KiEarlyDebugPrintVectorError
    b       KiSystemStartup_Halt

/* Early debug print - local assembly version for boot code */
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

/* Print wrong exception level error */
    .globl KiEarlyDebugPrintWrongEL
KiEarlyDebugPrintWrongEL:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiWrongELMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print vector alignment error */
    .globl KiEarlyDebugPrintVectorError
KiEarlyDebugPrintVectorError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiVectorAlignMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print TTBR error message */
    .globl KiEarlyDebugPrintTTBRError
KiEarlyDebugPrintTTBRError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiTTBRErrorMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print secondary processor error message */
    .globl KiEarlyDebugPrintSecondaryError
KiEarlyDebugPrintSecondaryError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiSecondaryErrorMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print interrupt state error message */
    .globl KiEarlyDebugPrintInterruptError
KiEarlyDebugPrintInterruptError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiInterruptErrorMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print stack error message */
    .globl KiEarlyDebugPrintStackError
KiEarlyDebugPrintStackError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiStackErrorMsg
1:  ldrb    w1, [x0], #1
    cbz     w1, 2f
    str     w1, [x2]
    b       1b
2:

    ldp     x2, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    ret

/* Print stack alignment error message */
    .globl KiEarlyDebugPrintStackAlignError
KiEarlyDebugPrintStackAlignError:
    stp     x0, x1, [sp, #-16]!
    stp     x2, xzr, [sp, #-16]!

    mov     x2, #0x09000000
    adr     x0, KiStackAlignErrorMsg
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
KiWrongELMsg:
    .asciz "ARM64 Kernel: Must run at Exception Level 1 (EL1)\r\n"
KiVectorAlignMsg:
    .asciz "ARM64 Kernel: Exception vectors not 2KB aligned\r\n"
KiTTBRErrorMsg:
    .asciz "ARM64 Kernel: Invalid TTBR1_EL1 configuration\r\n"
KiSecondaryErrorMsg:
    .asciz "ARM64 Kernel: Secondary CPU attempted primary boot path\r\n"
KiInterruptErrorMsg:
    .asciz "ARM64 Kernel: Interrupts not properly disabled at handoff\r\n"
KiStackErrorMsg:
    .asciz "ARM64 Kernel: Wrong stack pointer selection at handoff\r\n"
KiStackAlignErrorMsg:
    .asciz "ARM64 Kernel: Stack not 16-byte aligned at handoff\r\n"

    .end
