/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Context Switching
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#include <arm64/arm64.h>

/* Thread context offsets (must match CONTEXT structure) */
#define CTX_X0      0x000
#define CTX_X1      0x008
#define CTX_X2      0x010
#define CTX_X3      0x018
#define CTX_X4      0x020
#define CTX_X5      0x028
#define CTX_X6      0x030
#define CTX_X7      0x038
#define CTX_X8      0x040
#define CTX_X9      0x048
#define CTX_X10     0x050
#define CTX_X11     0x058
#define CTX_X12     0x060
#define CTX_X13     0x068
#define CTX_X14     0x070
#define CTX_X15     0x078
#define CTX_X16     0x080
#define CTX_X17     0x088
#define CTX_X18     0x090
#define CTX_X19     0x098
#define CTX_X20     0x0A0
#define CTX_X21     0x0A8
#define CTX_X22     0x0B0
#define CTX_X23     0x0B8
#define CTX_X24     0x0C0
#define CTX_X25     0x0C8
#define CTX_X26     0x0D0
#define CTX_X27     0x0D8
#define CTX_X28     0x0E0
#define CTX_X29     0x0E8    /* Frame pointer */
#define CTX_X30     0x0F0    /* Link register */
#define CTX_SP      0x0F8    /* Stack pointer */
#define CTX_PC      0x100    /* Program counter */
#define CTX_PSTATE  0x108    /* Processor state */

/* Thread structure offsets (simplified) */
/* #define THREAD_CONTEXT_FRAME    0x200 */  /* Offset to saved context - now in ksarm64.h */
#define THREAD_INITIAL_STACK    0x018   /* InitialStack field */
#define THREAD_KERNEL_STACK     0x020   /* KernelStack field */
/* #define THREAD_APCSTATE_PROCESS 0x050 */  /* ApcState.Process field - now in ksarm64.h */

/* Process structure offsets */
/* #define PROCESS_DIRECTORYTABLEBASE  0x018 */  /* DirectoryTableBase field - now in ksarm64.h */

.text

/**
 * ARM64 Context Switch Implementation
 * 
 * This function switches from the current thread to a new thread.
 * It saves the current thread's context and restores the new thread's context.
 * 
 * Parameters:
 *   x0 = PKTHREAD OldThread
 *   x1 = PKTHREAD NewThread
 */
.globl KiSwapContext
/* .type KiSwapContext, %function - not supported by MinGW */
KiSwapContext:
    /* Disable interrupts during context switch */
    mrs x2, daif
    msr daifset, #0xF
    
    /* Save current thread's callee-saved registers */
    add x4, x0, #THREAD_CONTEXT_FRAME
    stp x19, x20, [x4, #CTX_X19]
    stp x21, x22, [x4, #CTX_X21]
    stp x23, x24, [x4, #CTX_X23]
    stp x25, x26, [x4, #CTX_X25]
    stp x27, x28, [x4, #CTX_X27]
    stp x29, x30, [x4, #CTX_X29]
    
    /* Save stack pointer and DAIF state */
    mov x3, sp
    str x3, [x4, #CTX_SP]
    str x2, [x4, #CTX_PSTATE]
    
    /* Check if we're switching between different processes */
    ldr x3, [x0, #THREAD_APCSTATE_PROCESS]  /* Old process */
    ldr x4, [x1, #THREAD_APCSTATE_PROCESS]  /* New process */
    cmp x3, x4
    b.eq switch_context  /* Same process, skip page table switch */
    
    /* Switch page tables for different process */
    ldr x5, [x4, #PROCESS_DIRECTORYTABLEBASE]
    msr ttbr1_el1, x5
    isb
    
    /* Invalidate TLB entries for the new address space */
    tlbi vmalle1
    dsb sy
    isb

switch_context:
    /* Restore new thread's callee-saved registers */
    add x5, x1, #THREAD_CONTEXT_FRAME
    ldp x19, x20, [x5, #CTX_X19]
    ldp x21, x22, [x5, #CTX_X21]
    ldp x23, x24, [x5, #CTX_X23]
    ldp x25, x26, [x5, #CTX_X25]
    ldp x27, x28, [x5, #CTX_X27]
    ldp x29, x30, [x5, #CTX_X29]
    
    /* Restore stack pointer */
    ldr x3, [x5, #CTX_SP]
    mov sp, x3
    
    /* Restore interrupt state */
    ldr x2, [x5, #CTX_PSTATE]
    msr daif, x2
    
    /* Update current thread in PRCB (if PCR is accessible) */
    /* TODO: Implement PCR access for ARM64 */
    
    ret

/**
 * Initialize a new thread's context
 * 
 * Parameters:
 *   x0 = PKTHREAD Thread
 *   x1 = PVOID KernelStack
 *   x2 = PKSTART_ROUTINE StartRoutine
 *   x3 = PVOID StartContext
 */
.globl KiInitializeContextFrame
/* .type KiInitializeContextFrame, %function - not supported by MinGW */
KiInitializeContextFrame:
    /* Clear the context frame */
    add x4, x0, #THREAD_CONTEXT_FRAME
    mov x5, #(CTX_PSTATE + 8)
    mov x6, #0
1:  str x6, [x4], #8
    subs x5, x5, #8
    b.ne 1b
    
    /* Set up initial stack pointer (aligned to 16 bytes) */
    and x1, x1, #~0xF
    str x1, [x0, #THREAD_CONTEXT_FRAME + CTX_SP]
    
    /* Set up frame pointer */
    str x1, [x0, #THREAD_CONTEXT_FRAME + CTX_X29]
    
    /* Set up start routine and context */
    str x2, [x0, #THREAD_CONTEXT_FRAME + CTX_PC]    /* Start routine as PC */
    str x3, [x0, #THREAD_CONTEXT_FRAME + CTX_X0]    /* Start context in X0 */
    
    /* Set up link register to point to thread termination */
    adrp x4, KiThreadExit
    add x4, x4, #:lo12:KiThreadExit
    str x4, [x0, #THREAD_CONTEXT_FRAME + CTX_X30]
    
    /* Set up initial processor state */
    mov x4, #0    /* Clear DAIF (enable all interrupts) */
    str x4, [x0, #THREAD_CONTEXT_FRAME + CTX_PSTATE]
    
    ret

/**
 * Thread exit routine
 * Called when a thread returns from its start routine
 */
.globl KiThreadExit
/* .type KiThreadExit, %function - not supported by MinGW */
KiThreadExit:
    /* Call kernel thread termination */
    bl PsTerminateSystemThread
    
    /* Should never return, but just in case... */
    b KeBugCheck

/**
 * Switch to idle thread
 * Used by the scheduler when no threads are ready to run
 */
.globl KiIdleLoop
/* .type KiIdleLoop, %function - not supported by MinGW */
KiIdleLoop:
    /* Enable interrupts and wait for interrupt */
    msr daifclr, #0xF
    
idle_wait:
    /* Wait for interrupt */
    wfi
    
    /* Check if we should reschedule */
    bl KiCheckForReschedule
    cbz x0, idle_wait
    
    /* Return to scheduler */
    ret

/**
 * Save floating point context
 * 
 * Parameters:
 *   x0 = PFLOATING_SAVE_AREA FloatSave
 */
.globl KiSaveFloatingPointState
/* .type KiSaveFloatingPointState, %function - not supported by MinGW */
KiSaveFloatingPointState:
    /* Save FPCR and FPSR */
    mrs x1, fpcr
    mrs x2, fpsr
    stp x1, x2, [x0], #16
    
    /* Save all SIMD/FP registers */
    stp q0, q1, [x0], #32
    stp q2, q3, [x0], #32
    stp q4, q5, [x0], #32
    stp q6, q7, [x0], #32
    stp q8, q9, [x0], #32
    stp q10, q11, [x0], #32
    stp q12, q13, [x0], #32
    stp q14, q15, [x0], #32
    stp q16, q17, [x0], #32
    stp q18, q19, [x0], #32
    stp q20, q21, [x0], #32
    stp q22, q23, [x0], #32
    stp q24, q25, [x0], #32
    stp q26, q27, [x0], #32
    stp q28, q29, [x0], #32
    stp q30, q31, [x0], #32
    
    ret

/**
 * Restore floating point context
 * 
 * Parameters:
 *   x0 = PFLOATING_SAVE_AREA FloatSave
 */
.globl KiRestoreFloatingPointState
/* .type KiRestoreFloatingPointState, %function - not supported by MinGW */
KiRestoreFloatingPointState:
    /* Restore FPCR and FPSR */
    ldp x1, x2, [x0], #16
    msr fpcr, x1
    msr fpsr, x2
    
    /* Restore all SIMD/FP registers */
    ldp q0, q1, [x0], #32
    ldp q2, q3, [x0], #32
    ldp q4, q5, [x0], #32
    ldp q6, q7, [x0], #32
    ldp q8, q9, [x0], #32
    ldp q10, q11, [x0], #32
    ldp q12, q13, [x0], #32
    ldp q14, q15, [x0], #32
    ldp q16, q17, [x0], #32
    ldp q18, q19, [x0], #32
    ldp q20, q21, [x0], #32
    ldp q22, q23, [x0], #32
    ldp q24, q25, [x0], #32
    ldp q26, q27, [x0], #32
    ldp q28, q29, [x0], #32
    ldp q30, q31, [x0], #32
    
    ret

/**
 * Atomic context switch with interrupts disabled
 * Used by the scheduler to atomically switch threads
 * 
 * Parameters:
 *   x0 = PKTHREAD OldThread  
 *   x1 = PKTHREAD NewThread
 *   x2 = PKIRQL OldIrql (saved DAIF state)
 */
.globl KiAtomicSwapContext
/* .type KiAtomicSwapContext, %function - not supported by MinGW */
KiAtomicSwapContext:
    /* Save the old IRQL/DAIF state */
    str x2, [sp, #-16]!
    
    /* Perform the context switch */
    bl KiSwapContext
    
    /* Restore and return the old IRQL */
    ldr x0, [sp], #16
    ret

/**
 * Fast system call return to user mode
 * Used to return from system calls efficiently
 */
.globl KiFastSystemCallReturn  
/* .type KiFastSystemCallReturn, %function - not supported by MinGW */
KiFastSystemCallReturn:
    /* TODO: Implement fast system call return */
    /* For now, use normal exception return */
    b KiExceptionExit

/**
 * Dispatch interrupt to handle thread switching
 * Called by the timer interrupt to handle thread quantum expiration
 */
.globl KiDispatchInterrupt
/* .type KiDispatchInterrupt, %function - not supported by MinGW */
KiDispatchInterrupt:
    /* Save volatile registers */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x30, xzr, [sp, #-16]!
    
    /* Call the C dispatch routine */
    bl KiDispatchInterruptHandler
    
    /* Restore volatile registers */
    ldp x30, xzr, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    
    ret

/**
 * Zero a page of memory
 * Optimized ARM64 version using SIMD
 * 
 * Parameters:
 *   x0 = PVOID Address (must be page-aligned)
 */
.globl KiZeroPage
/* .type KiZeroPage, %function - not supported by MinGW */
KiZeroPage:
    /* Clear SIMD register */
    movi v0.16b, #0
    
    /* Zero 4KB page using 128-bit stores */
    mov x1, #(4096 / 64)  /* 64 bytes per iteration */
    
1:  stp q0, q0, [x0], #32
    stp q0, q0, [x0], #32
    subs x1, x1, #1
    b.ne 1b
    
    ret

/**
 * Copy a page of memory
 * Optimized ARM64 version
 * 
 * Parameters:
 *   x0 = PVOID Destination
 *   x1 = PVOID Source  
 */
.globl KiCopyPage
/* .type KiCopyPage, %function - not supported by MinGW */
KiCopyPage:
    mov x2, #(4096 / 64)  /* 64 bytes per iteration */
    
1:  ldp q0, q1, [x1], #32
    ldp q2, q3, [x1], #32
    stp q0, q1, [x0], #32
    stp q2, q3, [x0], #32
    subs x2, x2, #1
    b.ne 1b
    
    ret

/*
 * KeSwitchKernelStack
 *
 * Switches the current thread's kernel stack to a new stack.
 * Returns the old stack base.
 *
 * Parameters:
 *   x0 = PVOID StackBase (new stack base)
 *   x1 = PVOID StackLimit (new stack limit)
 *
 * Returns:
 *   x0 = PVOID (old stack base)
 */
.globl KeSwitchKernelStack
/* .type KeSwitchKernelStack, %function - not supported by MinGW */
KeSwitchKernelStack:
    /* Save current SP as the old stack to return */
    mov x2, sp

    /* Get current thread from TPIDR_EL1 (or from PCR) */
    /* For now, assume we can get the thread from a global or PCR */
    /* TODO: Implement proper thread pointer retrieval */

    /* Switch to the new stack */
    /* The new stack base is in x0, limit is in x1 */
    mov sp, x0

    /* TODO: Update thread's stack information */
    /* This would typically involve updating:
     * - Thread->KernelStack
     * - Thread->StackBase
     * - Thread->StackLimit
     * - PCR stack information
     */

    /* Return the old stack pointer in x0 */
    mov x0, x2
    ret

.end