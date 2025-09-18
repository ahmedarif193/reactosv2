/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 unwind support
 * FILE:            lib/rtl/arm64/unwind.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_opt_ PVOID HistoryTable)
{
    DPRINT1("RtlLookupFunctionEntry: stub for ARM64\n");
    /* TODO: Implement function entry lookup */
    *ImageBase = 0;
    return NULL;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ DWORD64 ImageBase,
    _In_ DWORD64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PDWORD64 EstablisherFrame,
    _Inout_opt_ PVOID ContextPointers)
{
    DPRINT1("RtlVirtualUnwind: stub for ARM64\n");
    /* TODO: Implement virtual unwind */
    *HandlerData = NULL;
    *EstablisherFrame = 0;
    return NULL;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PVOID HistoryTable)
{
    DPRINT1("RtlUnwindEx: stub for ARM64\n");
    /* TODO: Implement extended unwind */
    ASSERT(FALSE);
}

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    DPRINT1("RtlRestoreContext: stub for ARM64\n");
    /* TODO: Implement context restore */
    ASSERT(FALSE);
}