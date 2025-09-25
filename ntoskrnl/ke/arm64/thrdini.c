/*
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Kernel
 * PURPOSE:         ARM64 Thread Management
 * FILE:            ntoskrnl/ke/arm64/thrdini.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/**
 * @brief Initialize an ARM64 thread's context
 */
VOID
NTAPI
KiInitializeThread(
    IN PKPROCESS Process,
    IN OUT PKTHREAD Thread,
    IN PKSYSTEM_ROUTINE SystemRoutine,
    IN PKSTART_ROUTINE StartRoutine,
    IN PVOID StartContext,
    IN PCONTEXT ContextFrame,
    IN PVOID Teb,
    IN PVOID KernelStack
)
{
    PKTRAP_FRAME TrapFrame;
    PKEXCEPTION_FRAME ExceptionFrame;
    PULONG64 InitialStack;

    DPRINT("KiInitializeThread: Thread=%p, Stack=%p, StartRoutine=%p\n",
           Thread, KernelStack, StartRoutine);

    /* Set up the Initial Stack */
    InitialStack = (PULONG64)KernelStack;
    Thread->InitialStack = KernelStack;
    Thread->StackBase = KernelStack;
    Thread->StackLimit = (ULONG_PTR)KernelStack - KERNEL_STACK_SIZE + PAGE_SIZE;
    Thread->KernelStack = KernelStack;

    /* Calculate trap frame and exception frame positions */
    TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)InitialStack -
                               ALIGN_UP(sizeof(KTRAP_FRAME), STACK_ALIGN));
    ExceptionFrame = (PKEXCEPTION_FRAME)((ULONG_PTR)TrapFrame -
                                         ALIGN_UP(sizeof(KEXCEPTION_FRAME), STACK_ALIGN));

    /* Initialize trap frame */
    RtlZeroMemory(TrapFrame, sizeof(KTRAP_FRAME));

    /* Initialize exception frame (minimal for ARM64) */
    RtlZeroMemory(ExceptionFrame, sizeof(KEXCEPTION_FRAME));

    /* Set up based on whether we have a context frame (user mode) or not (kernel mode) */
    if (ContextFrame)
    {
        /* User mode thread initialization */

        /* Copy general-purpose registers (X0-X28, all available in KTRAP_FRAME) */
        TrapFrame->X0 = ContextFrame->X0;
        TrapFrame->X1 = ContextFrame->X1;
        TrapFrame->X2 = ContextFrame->X2;
        TrapFrame->X3 = ContextFrame->X3;
        TrapFrame->X4 = ContextFrame->X4;
        TrapFrame->X5 = ContextFrame->X5;
        TrapFrame->X6 = ContextFrame->X6;
        TrapFrame->X7 = ContextFrame->X7;
        TrapFrame->X8 = ContextFrame->X8;
        TrapFrame->X9 = ContextFrame->X9;
        TrapFrame->X10 = ContextFrame->X10;
        TrapFrame->X11 = ContextFrame->X11;
        TrapFrame->X12 = ContextFrame->X12;
        TrapFrame->X13 = ContextFrame->X13;
        TrapFrame->X14 = ContextFrame->X14;
        TrapFrame->X15 = ContextFrame->X15;
        TrapFrame->X16 = ContextFrame->X16;
        TrapFrame->X17 = ContextFrame->X17;
        TrapFrame->X18 = ContextFrame->X18;
        TrapFrame->X19 = ContextFrame->X19;
        TrapFrame->X20 = ContextFrame->X20;
        TrapFrame->X21 = ContextFrame->X21;
        TrapFrame->X22 = ContextFrame->X22;
        TrapFrame->X23 = ContextFrame->X23;
        TrapFrame->X24 = ContextFrame->X24;
        TrapFrame->X25 = ContextFrame->X25;
        TrapFrame->X26 = ContextFrame->X26;
        TrapFrame->X27 = ContextFrame->X27;
        TrapFrame->X28 = ContextFrame->X28;

        /* Copy frame pointer, link register, stack pointer, program counter */
        TrapFrame->Fp = ContextFrame->Fp;    /* X29 */
        TrapFrame->Lr = ContextFrame->Lr;    /* X30 */
        TrapFrame->Sp = ContextFrame->Sp;
        TrapFrame->Pc = ContextFrame->Pc;
        /* Copy processor state register */
        TrapFrame->Pstate = ContextFrame->Pstate;

        /* Set up user mode */
        TrapFrame->PreviousMode = UserMode;

        /* Set up TEB if provided */
        if (Teb)
        {
            TrapFrame->X18 = (ULONG64)Teb;  /* ARM64 TEB pointer convention */
        }

        DPRINT("KiInitializeThread: User thread, PC=0x%llX, SP=0x%llX\n",
               TrapFrame->Pc, TrapFrame->Sp);
    }
    else
    {
        /* Kernel mode thread initialization */

        /* Set up kernel mode registers */
        TrapFrame->X0 = (ULONG64)StartContext;   /* First argument */
        TrapFrame->Lr = (ULONG64)StartRoutine;   /* Return address (start routine) */
        TrapFrame->Sp = (ULONG_PTR)ExceptionFrame; /* Stack pointer */
        TrapFrame->Pc = (ULONG64)SystemRoutine;  /* Program counter */
        TrapFrame->Pstate = 0;                     /* Enable interrupts, EL1 */

        /* Set up kernel mode */
        TrapFrame->PreviousMode = KernelMode;

        DPRINT("KiInitializeThread: Kernel thread, PC=0x%llX, SP=0x%llX\n",
               TrapFrame->Pc, TrapFrame->Sp);
    }

    /* Set up thread fields */
    Thread->TrapFrame = TrapFrame;
    Thread->Teb = Teb;
    Thread->Process = Process;

    DPRINT("KiInitializeThread: Thread initialization completed\n");
}

/**
 * @brief Initialize thread context for ARM64 (stub)
 */
VOID
NTAPI
KiInitializeThreadContext(
    IN PKTHREAD Thread,
    IN PKSYSTEM_ROUTINE SystemRoutine,
    IN PKSTART_ROUTINE StartRoutine,
    IN PVOID StartContext,
    IN PCONTEXT ContextFrame
)
{
    /* This is a simplified stub for ARM64 */
    DPRINT("KiInitializeThreadContext: Thread=%p\n", Thread);

    /* Basic implementation - just ensure the thread can be scheduled */
    if (!ContextFrame)
    {
        /* Kernel thread - minimal setup */
        PKTRAP_FRAME TrapFrame = Thread->TrapFrame;
        if (TrapFrame)
        {
            TrapFrame->X0 = (ULONG64)StartContext;
            TrapFrame->Pc = (ULONG64)StartRoutine;
            TrapFrame->PreviousMode = KernelMode;
        }
    }
}

/**
 * @brief Set up user thread startup for ARM64 (stub)
 */
VOID
NTAPI
KiSetupUserThreadStartup(
    IN PKTHREAD Thread,
    IN PKSTART_ROUTINE StartRoutine,
    IN PVOID StartContext
)
{
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;

    DPRINT("KiSetupUserThreadStartup: Thread=%p, StartRoutine=%p\n",
           Thread, StartRoutine);

    if (TrapFrame)
    {
        TrapFrame->Pc = (ULONG64)StartRoutine;
        TrapFrame->X0 = (ULONG64)StartContext;
        TrapFrame->Pstate = 0;                   /* EL0 (user mode), interrupts enabled */
        TrapFrame->PreviousMode = UserMode;

        /* Set up user stack if not already set */
        if (TrapFrame->Sp == 0)
        {
            /* Use a default user stack location - this should be set properly by caller */
            TrapFrame->Sp = 0x7FFEF000;  /* Temporary default */
        }
    }
}

/**
 * @brief Initialize idle thread for ARM64
 */
VOID
NTAPI
KiInitializeIdleThread(
    IN PKTHREAD Thread,
    IN PVOID IdleStack,
    IN PKPROCESS Process
)
{
    DPRINT("KiInitializeIdleThread: Thread=%p, Stack=%p\n", Thread, IdleStack);

    /* Call the main initialization function with proper parameters */
    KiInitializeThread(Process,
                       Thread,
                       NULL,  /* No system routine for idle thread */
                       (PKSTART_ROUTINE)KiIdleLoop,
                       NULL,  /* No start context */
                       NULL,  /* No context frame - kernel mode */
                       NULL,  /* No TEB */
                       IdleStack);

    /* Mark as idle thread */
    Thread->State = Ready;
    Thread->Priority = 0;  /* Lowest priority */

    DPRINT("KiInitializeIdleThread: Idle thread initialization completed\n");
}