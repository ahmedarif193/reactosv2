/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Process Context Support
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/**
 * @brief Get the current process context for ARM64
 */
VOID
NTAPI
PspGetContext(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN OUT PCONTEXT Context
)
{
    /* Clear the context */
    RtlZeroMemory(Context, sizeof(CONTEXT));
    
    /* Set context flags */
    Context->ContextFlags = 0x00400001L | 0x00400002L | 0x00400004L;  /* CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT for ARM64 */
    
    if (TrapFrame)
    {
        /* Copy general purpose registers */
        Context->X0 = TrapFrame->X0;
        Context->X1 = TrapFrame->X1;
        Context->X2 = TrapFrame->X2;
        Context->X3 = TrapFrame->X3;
        Context->X4 = TrapFrame->X4;
        Context->X5 = TrapFrame->X5;
        Context->X6 = TrapFrame->X6;
        Context->X7 = TrapFrame->X7;
        Context->X8 = TrapFrame->X8;
        Context->X9 = TrapFrame->X9;
        Context->X10 = TrapFrame->X10;
        Context->X11 = TrapFrame->X11;
        Context->X12 = TrapFrame->X12;
        Context->X13 = TrapFrame->X13;
        Context->X14 = TrapFrame->X14;
        Context->X15 = TrapFrame->X15;
        Context->X16 = TrapFrame->X16;
        Context->X17 = TrapFrame->X17;
        Context->X18 = TrapFrame->X18;
        Context->X19 = TrapFrame->X19;
        Context->X20 = TrapFrame->X20;
        Context->X21 = TrapFrame->X21;
        Context->X22 = TrapFrame->X22;
        Context->X23 = TrapFrame->X23;
        Context->X24 = TrapFrame->X24;
        Context->X25 = TrapFrame->X25;
        Context->X26 = TrapFrame->X26;
        Context->X27 = TrapFrame->X27;
        Context->X28 = TrapFrame->X28;
        
        /* Copy frame pointer and link register */
        Context->Fp = TrapFrame->Fp;    /* X29 */
        Context->Lr = TrapFrame->Lr;    /* X30 */
        
        /* Copy stack pointer, program counter, and processor state */
        Context->Sp = TrapFrame->Sp;
        Context->Pc = TrapFrame->Pc;
        /* Copy processor state register */
        Context->Pstate = TrapFrame->Pstate;
    }
    
    /* TODO: Exception frame is not yet implemented for ARM64 */
    /* All registers are currently stored in the KTRAP_FRAME */
    UNREFERENCED_PARAMETER(ExceptionFrame);
}

/**
 * @brief Set the current process context for ARM64
 */
VOID
NTAPI
PspSetContext(
    OUT PKTRAP_FRAME TrapFrame,
    OUT PKEXCEPTION_FRAME ExceptionFrame,
    IN PCONTEXT Context,
    IN KPROCESSOR_MODE PreviousMode
)
{
    if (TrapFrame)
    {
        /* Validate and copy general purpose registers */
        TrapFrame->X0 = Context->X0;
        TrapFrame->X1 = Context->X1;
        TrapFrame->X2 = Context->X2;
        TrapFrame->X3 = Context->X3;
        TrapFrame->X4 = Context->X4;
        TrapFrame->X5 = Context->X5;
        TrapFrame->X6 = Context->X6;
        TrapFrame->X7 = Context->X7;
        TrapFrame->X8 = Context->X8;
        TrapFrame->X9 = Context->X9;
        TrapFrame->X10 = Context->X10;
        TrapFrame->X11 = Context->X11;
        TrapFrame->X12 = Context->X12;
        TrapFrame->X13 = Context->X13;
        TrapFrame->X14 = Context->X14;
        TrapFrame->X15 = Context->X15;
        TrapFrame->X16 = Context->X16;
        TrapFrame->X17 = Context->X17;
        TrapFrame->X18 = Context->X18;
        TrapFrame->X19 = Context->X19;
        TrapFrame->X20 = Context->X20;
        TrapFrame->X21 = Context->X21;
        TrapFrame->X22 = Context->X22;
        TrapFrame->X23 = Context->X23;
        TrapFrame->X24 = Context->X24;
        TrapFrame->X25 = Context->X25;
        TrapFrame->X26 = Context->X26;
        TrapFrame->X27 = Context->X27;
        TrapFrame->X28 = Context->X28;
        
        /* Copy frame pointer and link register */
        TrapFrame->Fp = Context->Fp;    /* X29 */
        TrapFrame->Lr = Context->Lr;    /* X30 */
        
        /* Copy stack pointer and program counter */
        TrapFrame->Sp = Context->Sp;
        TrapFrame->Pc = Context->Pc;
        
        /* Validate and set processor state */
        if (PreviousMode == UserMode)
        {
            /* Ensure user mode processor state */
            TrapFrame->Pstate = (Context->Pstate & ~0xF) | 0x0;  /* EL0 */
        }
        else
        {
            /* Keep kernel mode processor state */
            TrapFrame->Pstate = (Context->Pstate & ~0xF) | 0x4;  /* EL1h */
        }
    }
    
    /* TODO: Exception frame is not yet implemented for ARM64 */
    /* All registers are currently stored in the KTRAP_FRAME */
    UNREFERENCED_PARAMETER(ExceptionFrame);
}

/**
 * @brief Get the current thread context
 */
VOID
NTAPI
PspGetSetContextSpecialApc(
    IN PKAPC Apc,
    IN PKNORMAL_ROUTINE *NormalRoutine,
    IN PVOID *NormalContext,
    IN PVOID *SystemArgument1,
    IN PVOID *SystemArgument2
)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame;
    PKEXCEPTION_FRAME ExceptionFrame;
    
    /* Get the context */
    GetSetContext = (PGET_SET_CTX_CONTEXT)*NormalContext;
    Thread = Apc->Thread;
    
    /* Get trap and exception frames */
    TrapFrame = Thread->TrapFrame;
    ExceptionFrame = (PKEXCEPTION_FRAME)((ULONG_PTR)Thread->InitialStack -
                                         ALIGN_UP(sizeof(KTRAP_FRAME), STACK_ALIGN) -
                                         ALIGN_UP(sizeof(KEXCEPTION_FRAME), STACK_ALIGN));
    
    /* Check if it's a set or get */
    if (Apc->SystemArgument1 == 0)
    {
        /* Get the context */
        PspGetContext(TrapFrame, ExceptionFrame, &GetSetContext->Context);
    }
    else
    {
        /* Set the context */
        PspSetContext(TrapFrame, ExceptionFrame, &GetSetContext->Context, Thread->PreviousMode);
    }
    
    /* Free the context structure */
    ExFreePoolWithTag(GetSetContext, TAG_PS_APC);
}

/**
 * @brief Kernel routine for get/set context operations
 */
VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
    _Inout_ PVOID* NormalContext,
    _Inout_ PVOID* SystemArgument1,
    _Inout_ PVOID* SystemArgument2)
{
    PGET_SET_CTX_CONTEXT GetSetContext;
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame = NULL;
    PKEXCEPTION_FRAME ExceptionFrame = NULL;

    PAGED_CODE();

    /* Get the Context Structure */
    GetSetContext = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    Thread = Apc->SystemArgument2;
    NT_ASSERT(KeGetCurrentThread() == Thread);

    /* If this is a kernel-mode request, grab the saved trap frame */
    if (GetSetContext->Mode == KernelMode)
    {
        TrapFrame = Thread->TrapFrame;
    }

    /* If we don't have one, grab it from the stack */
    if (TrapFrame == NULL)
    {
        /* Get the thread's base trap frame */
        TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)Thread->InitialStack -
                                   ALIGN_UP(sizeof(KTRAP_FRAME), STACK_ALIGN));
    }

    /* ARM64 currently stores everything in the trap frame */
    ExceptionFrame = NULL;

    /* Check if it's a set or get */
    if (Apc->SystemArgument1 != 0)
    {
        /* Set the context */
        PspSetContext(TrapFrame, ExceptionFrame, &GetSetContext->Context, GetSetContext->Mode);
    }
    else
    {
        /* Get the context */
        PspGetContext(TrapFrame, ExceptionFrame, &GetSetContext->Context);
    }

    /* Notify the Native API that we are done */
    KeSetEvent(&GetSetContext->Event, IO_NO_INCREMENT, FALSE);
}