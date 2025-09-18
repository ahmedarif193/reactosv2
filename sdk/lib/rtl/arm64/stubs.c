/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 stubs
 * FILE:            lib/rtl/arm64/stubs.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
RtlInitializeContext(
    _Reserved_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    DPRINT1("RtlInitializeContext: stub for ARM64\n");

    /* Zero the context */
    RtlZeroMemory(ThreadContext, sizeof(CONTEXT));

    /* Setup basic context flags */
    ThreadContext->ContextFlags = CONTEXT_FULL;

    /* Set the thread start address */
    ThreadContext->Pc = (DWORD64)ThreadStartAddress;

    /* Set the first parameter */
    ThreadContext->X0 = (DWORD64)ThreadStartParam;

    /* Set the stack pointer */
    ThreadContext->Sp = (DWORD64)StackBase->StackLimit;

    /* TODO: Implement proper context initialization */
    ASSERT(FALSE);
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    DPRINT1("RtlDispatchException: stub for ARM64\n");
    /* TODO: Implement exception dispatch */
    return FALSE;
}

VOID
NTAPI
RtlRaiseException(
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    DPRINT1("RtlRaiseException: stub for ARM64\n");
    /* TODO: Implement raise exception */
    ASSERT(FALSE);
}

VOID
NTAPI
RtlCaptureContext(
    _Out_ PCONTEXT Context)
{
    DPRINT1("RtlCaptureContext: stub for ARM64\n");
    /* TODO: Implement context capture */
    RtlZeroMemory(Context, sizeof(CONTEXT));
    Context->ContextFlags = CONTEXT_FULL;
    ASSERT(FALSE);
}