/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 User Mode System Calls
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64 CONTEXT definitions */
#ifndef CONTEXT_ARM64
#define CONTEXT_ARM64                   0x00400000L
#define CONTEXT_CONTROL                 (CONTEXT_ARM64 | 0x1L)
#define CONTEXT_INTEGER                 (CONTEXT_ARM64 | 0x2L)
#define CONTEXT_FLOATING_POINT          (CONTEXT_ARM64 | 0x4L)
#define CONTEXT_DEBUG_REGISTERS         (CONTEXT_ARM64 | 0x8L)
#define CONTEXT_X18                     (CONTEXT_ARM64 | 0x10L)
#define CONTEXT_FULL                    (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#endif

/* Function prototypes */
VOID
NTAPI
PspGetContext(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN OUT PCONTEXT Context
);

/* GLOBALS *******************************************************************/

/* ARM64 system call dispatch table */
extern PVOID KiServiceTable[];
extern ULONG KiServiceLimit;

/* FUNCTIONS *****************************************************************/

/**
 * @brief Forward declaration for generic system call dispatcher
 */
NTSTATUS
NTAPI
KiSystemCallGenericDispatch(
    IN PVOID ServiceRoutine,
    IN PULONG64 Arguments,
    IN ULONG ArgumentCount
);

/**
 * @brief ARM64 system call dispatcher
 */
NTSTATUS
NTAPI  
KiSystemCallDispatch(
    IN ULONG SystemCallNumber,
    IN PKTRAP_FRAME TrapFrame
)
{
    PVOID ServiceRoutine;
    NTSTATUS Status;
    ULONG ArgumentCount;
    ULONG64 Arguments[16];  /* Maximum 16 arguments for ARM64 */
    
    DPRINT("KiSystemCallDispatch: Call %u from PC=0x%llX\n", 
           SystemCallNumber, TrapFrame->Pc);
    
    /* Validate system call number */
    if (SystemCallNumber >= KiServiceLimit)
    {
        DPRINT1("ARM64: Invalid system call number %u (limit %u)\n",
                SystemCallNumber, KiServiceLimit);
        return STATUS_INVALID_SYSTEM_SERVICE;
    }
    
    /* Get service routine */
    ServiceRoutine = KiServiceTable[SystemCallNumber];
    if (!ServiceRoutine)
    {
        DPRINT1("ARM64: System call %u not implemented\n", SystemCallNumber);
        return STATUS_NOT_IMPLEMENTED;
    }
    
    /* Extract arguments from registers and stack */
    /* ARM64 calling convention: x0-x7 for first 8 arguments */
    Arguments[0] = TrapFrame->X0;
    Arguments[1] = TrapFrame->X1;
    Arguments[2] = TrapFrame->X2;
    Arguments[3] = TrapFrame->X3;
    Arguments[4] = TrapFrame->X4;
    Arguments[5] = TrapFrame->X5;
    Arguments[6] = TrapFrame->X6;
    Arguments[7] = TrapFrame->X7;
    
    /* Additional arguments would be on the user stack */
    /* For now, limit to 8 arguments */
    ArgumentCount = 8;
    
    /* TODO: Implement actual system call dispatch */
    /* This is a simplified version - real implementation would need */
    /* proper argument parsing based on system call signature */

    /* TODO: ARM64 SEH support not yet implemented */
#if 0
    __try
    {
#endif
        /* Call the system service */
        switch (ArgumentCount)
        {
            case 0:
                Status = ((NTSTATUS(NTAPI *)(VOID))ServiceRoutine)();
                break;

            case 1:
                Status = ((NTSTATUS(NTAPI *)(ULONG64))ServiceRoutine)(Arguments[0]);
                break;

            case 2:
                Status = ((NTSTATUS(NTAPI *)(ULONG64, ULONG64))ServiceRoutine)
                         (Arguments[0], Arguments[1]);
                break;

            case 3:
                Status = ((NTSTATUS(NTAPI *)(ULONG64, ULONG64, ULONG64))ServiceRoutine)
                         (Arguments[0], Arguments[1], Arguments[2]);
                break;

            case 4:
                Status = ((NTSTATUS(NTAPI *)(ULONG64, ULONG64, ULONG64, ULONG64))ServiceRoutine)
                         (Arguments[0], Arguments[1], Arguments[2], Arguments[3]);
                break;

            default:
                /* For more arguments, use a generic dispatcher */
                Status = KiSystemCallGenericDispatch(ServiceRoutine, Arguments, ArgumentCount);
                break;
        }
#if 0
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = GetExceptionCode();
        DPRINT1("ARM64: Exception in system call %u: 0x%08X\n", SystemCallNumber, Status);
    }
#endif
    
    DPRINT("KiSystemCallDispatch: Call %u returned 0x%08X\n", SystemCallNumber, Status);
    return Status;
}

/**
 * @brief Generic system call dispatcher for many arguments
 */
NTSTATUS
NTAPI
KiSystemCallGenericDispatch(
    IN PVOID ServiceRoutine,
    IN PULONG64 Arguments,
    IN ULONG ArgumentCount
)
{
    /* This would need assembly implementation for variable arguments */
    /* For now, return not implemented */
    UNREFERENCED_PARAMETER(ServiceRoutine);
    UNREFERENCED_PARAMETER(Arguments);
    UNREFERENCED_PARAMETER(ArgumentCount);
    
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Handle ARM64 system call from user mode
 */
VOID
NTAPI
KiSystemCall64(
    IN PKTRAP_FRAME TrapFrame
)
{
    ULONG SystemCallNumber;
    NTSTATUS Status;
    PKTHREAD Thread;
    BOOLEAN PreviousMode;
    
    /* Get system call number from x8 register */
    SystemCallNumber = (ULONG)TrapFrame->X8;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Save previous mode */
    PreviousMode = KeGetPreviousMode();
    
    /* Set kernel mode for system call execution */
    Thread->PreviousMode = UserMode;
    
    DPRINT("KiSystemCall64: Call %u from user mode, PC=0x%llX\n",
           SystemCallNumber, TrapFrame->Pc);
    
    /* Enable interrupts for system call execution */
    _enable();
    
    /* TODO: ARM64 SEH support not yet implemented */
#if 0
    __try
    {
#endif
        /* Probe user mode parameters if needed */
        /* TODO: Add parameter probing */

        /* Dispatch the system call */
        Status = KiSystemCallDispatch(SystemCallNumber, TrapFrame);
#if 0
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = GetExceptionCode();
        DPRINT1("ARM64: Exception during system call %u: 0x%08X\n",
                SystemCallNumber, Status);
    }
#endif
    
    /* Disable interrupts before returning */
    _disable();
    
    /* Restore previous mode */
    Thread->PreviousMode = PreviousMode;
    
    /* Set return value in x0 register */
    TrapFrame->X0 = (ULONG64)Status;
    
    /* Check for pending APCs */
    if (Thread->ApcState.UserApcPending)
    {
        DPRINT("KiSystemCall64: User APC pending\n");
        /* TODO: Deliver user APC */
    }
    
    DPRINT("KiSystemCall64: Returning 0x%08X\n", Status);
}

/**
 * @brief Handle ARM64 system call from 32-bit user mode (compatibility)
 */
VOID
NTAPI
KiSystemCall32(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("KiSystemCall32: 32-bit system call not implemented\n");
    
    /* For now, return error */
    TrapFrame->X0 = STATUS_NOT_SUPPORTED;
}

/**
 * @brief Initialize ARM64 system call support
 */
VOID
NTAPI
KiInitializeSystemCalls(VOID)
{
    DPRINT("KiInitializeSystemCalls: ARM64 system call support initialized\n");

    /* TODO: Initialize system call dispatch table */
    /* TODO: Set up system call entry points */
}

/**
 * @brief Handle user mode callback
 */
NTSTATUS
NTAPI
KiCallUserMode(
    IN OUT PVOID *OutputBuffer,
    IN OUT PULONG OutputLength
)
{
    DPRINT("KiCallUserMode: Not implemented for ARM64\n");

    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputLength);

    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Test if we're in user mode context
 */
BOOLEAN
NTAPI
KiIsUserModeThread(VOID)
{
    PKTRAP_FRAME TrapFrame;
    PKTHREAD Thread;

    Thread = KeGetCurrentThread();
    if (!Thread)
        return FALSE;

    TrapFrame = Thread->TrapFrame;
    if (!TrapFrame)
        return FALSE;

    /* Check if previous mode was user mode */
    return (TrapFrame->PreviousMode == UserMode);
}

/**
 * @brief Validate user mode address
 */
BOOLEAN
NTAPI
KiValidateUserAddress(
    IN PVOID Address,
    IN SIZE_T Length
)
{
    ULONG_PTR StartAddress = (ULONG_PTR)Address;
    ULONG_PTR EndAddress = StartAddress + Length;
    
    /* Check if address is in user space */
    if (StartAddress >= USER_SPACE_END)
        return FALSE;
        
    /* Check for overflow */
    if (EndAddress < StartAddress)
        return FALSE;
        
    /* Check if end address is still in user space */
    if (EndAddress > USER_SPACE_END)
        return FALSE;
    
    return TRUE;
}

/**
 * @brief Copy data from user mode safely
 */
NTSTATUS
NTAPI
KiCopyFromUser(
    OUT PVOID Destination,
    IN PVOID UserSource,
    IN SIZE_T Length
)
{
    if (!KiValidateUserAddress(UserSource, Length))
        return STATUS_ACCESS_VIOLATION;
    
    /* TODO: ARM64 SEH support not yet implemented */
#if 0
    __try
    {
#endif
        RtlCopyMemory(Destination, UserSource, Length);
        return STATUS_SUCCESS;
#if 0
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }
#endif
}

/**
 * @brief Copy data to user mode safely
 */
NTSTATUS
NTAPI
KiCopyToUser(
    OUT PVOID UserDestination,
    IN PVOID Source,
    IN SIZE_T Length
)
{
    if (!KiValidateUserAddress(UserDestination, Length))
        return STATUS_ACCESS_VIOLATION;
    
    /* TODO: ARM64 SEH support not yet implemented */
#if 0
    __try
    {
#endif
        RtlCopyMemory(UserDestination, Source, Length);
        return STATUS_SUCCESS;
#if 0
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }
#endif
}

/**
 * @brief Return from user mode callback
 *
 * This function is called when user mode returns from a callback
 * that was initiated by the kernel.
 *
 * @param Result - Result buffer from user mode
 * @param ResultLength - Length of result buffer
 * @param CallbackStatus - Status code from callback
 * @return NTSTATUS
 */
NTSTATUS
NTAPI
NtCallbackReturn(
    _In_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD CurrentThread;
    PVOID CallbackStack;

    DPRINT("NtCallbackReturn: Result=%p, Length=%u, Status=0x%08X\n",
           Result, ResultLength, CallbackStatus);

    /* Get the current thread and make sure we have a callback stack */
    CurrentThread = KeGetCurrentThread();
    CallbackStack = CurrentThread->CallbackStack;
    if (CallbackStack == NULL)
    {
        DPRINT1("NtCallbackReturn: No active callback!\n");
        return STATUS_NO_CALLBACK_ACTIVE;
    }

    /* TODO: ARM64 implementation of callback return
     * This needs to:
     * 1. Store the result and result length for the kernel caller
     * 2. Restore the previous callback stack
     * 3. Switch back to the kernel stack
     * 4. Restore all saved registers
     * 5. Return to the kernel code that initiated the callback
     *
     * The actual implementation requires assembly code to properly
     * restore the kernel context and cannot be done purely in C.
     */

    /* For now, clear the callback stack to prevent reuse */
    CurrentThread->CallbackStack = NULL;

    /* This is a stub - the real implementation would not return */
    DPRINT1("NtCallbackReturn: ARM64 implementation incomplete\n");

    UNREFERENCED_PARAMETER(Result);
    UNREFERENCED_PARAMETER(ResultLength);

    return CallbackStatus;
}

/**
 * @brief Initialize a user APC
 *
 * This function sets up the trap frame to execute a user APC when
 * returning to user mode.
 *
 * @param ExceptionFrame - Exception frame (currently unused on ARM64)
 * @param TrapFrame - Trap frame to modify
 * @param NormalRoutine - APC routine to execute
 * @param NormalContext - Context parameter for APC
 * @param SystemArgument1 - First system argument
 * @param SystemArgument2 - Second system argument
 * @return VOID
 */
VOID
NTAPI
KiInitializeUserApc(
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PKTRAP_FRAME TrapFrame,
    IN PKNORMAL_ROUTINE NormalRoutine,
    IN PVOID NormalContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    CONTEXT Context = { 0 };
    ULONG_PTR Stack;
    ULONG ContextLength;

    DPRINT("KiInitializeUserApc: Routine=%p, Context=%p, Arg1=%p, Arg2=%p\n",
           NormalRoutine, NormalContext, SystemArgument1, SystemArgument2);

    /* Build the user mode context from current trap frame */
    Context.ContextFlags = CONTEXT_FULL;
    PspGetContext(TrapFrame, ExceptionFrame, &Context);

    /* Setup the context on the user stack (16-byte aligned for ARM64) */
    ContextLength = sizeof(CONTEXT);
    Stack = (Context.Sp & ~15ULL) - ContextLength;

    /* TODO: ARM64 SEH support not yet implemented */
#if 0
    __try
    {
#endif
        /* Make sure the stack is valid, and copy the context */
        /* TODO: Add proper stack validation for ARM64 */
        RtlMoveMemory((PVOID)Stack, &Context, sizeof(CONTEXT));
#if 0
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("KiInitializeUserApc: Exception copying context to user stack\n");
        return;
    }
#endif

    /* Setup the trap frame for APC execution when returning to user mode */
    /* ARM64 calling convention: x0-x7 for parameters */
    TrapFrame->X0 = (ULONG_PTR)NormalContext;      /* First parameter */
    TrapFrame->X1 = (ULONG_PTR)SystemArgument1;    /* Second parameter */
    TrapFrame->X2 = (ULONG_PTR)SystemArgument2;    /* Third parameter */
    TrapFrame->X3 = (ULONG_PTR)NormalRoutine;      /* Fourth parameter */

    /* Set the stack pointer to our allocated space */
    TrapFrame->Sp = Stack;

    /* Set the program counter to the APC dispatcher */
    /* TODO: KeUserApcDispatcher needs to be implemented for ARM64 */
    extern PVOID KeUserApcDispatcher;
    TrapFrame->Pc = (ULONG_PTR)KeUserApcDispatcher;

    /* Ensure we return to user mode (EL0) */
    TrapFrame->Pstate &= ~0xF;  /* Clear exception level bits */
    /* EL0t (user mode with SP_EL0) = 0x0 */

    DPRINT("KiInitializeUserApc: Set PC=0x%llX, SP=0x%llX\n",
           TrapFrame->Pc, TrapFrame->Sp);
}