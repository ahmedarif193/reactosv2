/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Context Switching
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESIGN NOTES FOR ARM64 CONTEXT SWITCHING:
 *
 * 1. Memory Ordering and Barriers:
 *    ARM64 has a weakly-ordered memory model, requiring explicit barriers.
 *    Critical sequence for TTBR switching: TTBR write -> DSB ISHST -> TLBI -> DSB ISH -> ISB
 *
 * 2. ASID-Based TLB Management:
 *    Uses Address Space Identifiers (ASIDs) stored in upper 16 bits of TTBR
 *    for more efficient TLB invalidation instead of global flushes.
 *
 * 3. Thread Local Storage (TLS):
 *    - TPIDR_EL0: User-mode thread pointer (saved/restored per thread)
 *    - TPIDR_EL1: Kernel-mode per-CPU data, stores current thread pointer
 *
 * 4. Cache Coherency:
 *    All memory barriers use Inner Shareable domain (ISH) for multi-core
 *    coherency while maintaining performance on single-core systems.
 *
 * 5. Register Usage:
 *    Follows AAPCS64 calling convention, preserving callee-saved registers
 *    (x19-x28, x29/FP, x30/LR, SP) across context switches.
 */

#include <arm64/arm64.h>

/* ARM64 ReactOS Constants - from ksarm64.h */
#define THREAD_CONTEXT_FRAME        0x1b8  /* Offset to saved context in KTHREAD */
#define THREAD_APCSTATE_PROCESS     0x130  /* ApcState.Process field in KTHREAD */
#define PROCESS_DIRECTORYTABLEBASE  0x28   /* DirectoryTableBase field in KPROCESS */

/* Additional thread structure constants */
#define THREAD_INITIAL_STACK        0x018  /* InitialStack field */
#define THREAD_KERNEL_STACK         0x020  /* KernelStack field */

/* Thread context offsets (must match CONTEXT structure) */
#define CTX_X0       0x000
#define CTX_X1       0x008
#define CTX_X2       0x010
#define CTX_X3       0x018
#define CTX_X4       0x020
#define CTX_X5       0x028
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
#define CTX_TPIDR_EL0  0x110  /* Thread Pointer (User TLS) */

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
.globl KiSwapContextARM64
/* .type KiSwapContextARM64, %function - not supported by MinGW */
KiSwapContextARM64:
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

    /* Save Thread Local Storage pointer (TPIDR_EL0) */
    mrs x3, tpidr_el0
    str x3, [x4, #CTX_TPIDR_EL0]
    
    /* Check if we're switching between different processes */
    ldr x3, [x0, #THREAD_APCSTATE_PROCESS]  /* Old process */
    ldr x4, [x1, #THREAD_APCSTATE_PROCESS]  /* New process */
    cmp x3, x4
    b.eq switch_context  /* Same process, skip page table switch */
    
    /* Switch page tables for different process */
    ldr x5, [x4, #PROCESS_DIRECTORYTABLEBASE]

    /* ARM64 Critical Memory Barrier Sequence for TTBR switching:
     * 1. Write TTBR -> DSB ISHST -> TLBI -> DSB ISH -> ISB
     * This ensures proper ordering on ARM64's weakly-ordered memory model */
    msr ttbr1_el1, x5
    dsb ishst                    /* Data Synchronization Barrier - Inner Shareable Store */

    /* Get ASID from the new process for targeted TLB invalidation */
    ldr x6, [x4, #PROCESS_DIRECTORYTABLEBASE + 8]  /* Load ASID from second DirectoryTableBase slot */
    cbz x6, global_tlb_flush     /* If no ASID, use global flush */

    /* ASID-based TLB invalidation (more efficient) */
    lsl x6, x6, #48             /* Move ASID to bits 63:48 */
    tlbi aside1is, x6           /* Invalidate by ASID, Inner Shareable */
    b tlb_flush_done

global_tlb_flush:
    /* Fall back to global TLB flush if no ASID */
    tlbi vmalle1is              /* Global flush, Inner Shareable */

tlb_flush_done:
    dsb ish                     /* Data Synchronization Barrier - Inner Shareable */
    isb                         /* Instruction Synchronization Barrier */

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

    /* Restore Thread Local Storage pointer (TPIDR_EL0) */
    ldr x3, [x5, #CTX_TPIDR_EL0]
    msr tpidr_el0, x3

    /* Update current thread in TPIDR_EL1 for kernel per-CPU data */
    /* In ARM64, TPIDR_EL1 typically points to PCR or PRCB */
    /* For now, store the current thread pointer directly */
    msr tpidr_el1, x1           /* Store new thread pointer in TPIDR_EL1 */

    /* Ensure all system register writes complete before continuing */
    dsb sy                      /* Full system data synchronization barrier */
    isb                         /* Instruction synchronization barrier */

    /* Restore interrupt state */
    ldr x2, [x5, #CTX_PSTATE]
    msr daif, x2
    
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
    mov x5, #(CTX_TPIDR_EL0 + 8)  /* Clear up to and including TPIDR_EL0 */
    mov x6, #0
1:  str x6, [x4], #8
    subs x5, x5, #8
    b.ne 1b
    
    /* Set up initial stack pointer (aligned to 16 bytes) */
    and x1, x1, #~0xF
    add x5, x0, #THREAD_CONTEXT_FRAME    /* Get context frame base */
    str x1, [x5, #CTX_SP]

    /* Set up frame pointer */
    str x1, [x5, #CTX_X29]

    /* Set up start routine and context */
    str x2, [x5, #CTX_PC]    /* Start routine as PC */
    str x3, [x5, #CTX_X0]    /* Start context in X0 */

    /* Set up link register to point to thread termination */
    adrp x4, KiThreadExit
    add x4, x4, #:lo12:KiThreadExit
    str x4, [x5, #CTX_X30]

    /* Set up initial processor state */
    mov x4, #0    /* Clear DAIF (enable all interrupts) */
    str x4, [x5, #CTX_PSTATE]
    
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
    /* Save the old IRQL/DAIF state and link register */
    stp x2, x30, [sp, #-16]!

    /* Ensure all memory operations complete before context switch */
    dsb sy

    /* Perform the context switch */
    bl KiSwapContextARM64

    /* Restore link register and return the old IRQL */
    ldp x0, x30, [sp], #16
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

    /* Get current thread from TPIDR_EL1 */
    mrs x3, tpidr_el1           /* Current thread pointer stored in TPIDR_EL1 */

    /* Switch to the new stack */
    /* The new stack base is in x0, limit is in x1 */
    mov sp, x0

    /* Update thread's stack information if we have current thread */
    cbz x3, stack_switch_done   /* Skip if no current thread */

    /* Update thread stack fields */
    str x0, [x3, #THREAD_KERNEL_STACK]     /* Update KernelStack */
    str x0, [x3, #THREAD_INITIAL_STACK]    /* Update StackBase */
    /* Note: StackLimit typically stored elsewhere in thread structure */

stack_switch_done:

    /* Return the old stack pointer in x0 */
    mov x0, x2
    ret

/**
 * Initialize ARM64-specific processor state
 * Called during processor initialization to set up TPIDR_EL1
 *
 * Parameters:
 *   x0 = PKPRCB Prcb (Processor Control Block)
 *   x1 = PKTHREAD IdleThread (Initial thread to set in TPIDR_EL1)
 */
.globl KiInitializeProcessorState
/* .type KiInitializeProcessorState, %function - not supported by MinGW */
KiInitializeProcessorState:
    /* Store the idle thread pointer in TPIDR_EL1 for this processor */
    msr tpidr_el1, x1

    /* Initialize user TLS register to zero */
    msr tpidr_el0, xzr

    /* Ensure system register writes complete */
    dsb sy
    isb

    ret

/**
 * Get current thread pointer from TPIDR_EL1
 * Utility function for kernel code to get current thread
 *
 * Returns:
 *   x0 = PKTHREAD (current thread pointer)
 */
.globl KeGetCurrentThread
/* .type KeGetCurrentThread, %function - not supported by MinGW */
KeGetCurrentThread:
    mrs x0, tpidr_el1
    ret

/**
 * Set current thread pointer in TPIDR_EL1
 * Utility function for kernel code to update current thread
 *
 * Parameters:
 *   x0 = PKTHREAD (new current thread pointer)
 */
.globl KeSetCurrentThread
/* .type KeSetCurrentThread, %function - not supported by MinGW */
KeSetCurrentThread:
    msr tpidr_el1, x0
    dsb sy
    isb
    ret

.end
