/*
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         Exception handling for ARM64
 * FILE:            lib/rtl/arm64/except.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    DPRINT1("RtlUnwind: stub for ARM64\n");
    /* TODO: Implement unwind */
    ASSERT(FALSE);
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *CallersAddress,
    _Out_ PVOID *CallersCaller)
{
    DPRINT1("RtlGetCallersAddress: stub for ARM64\n");

    /* Return NULL for now */
    *CallersAddress = NULL;
    *CallersCaller = NULL;

    /* TODO: Implement stack walking */
}

BOOLEAN
NTAPI
RtlpCaptureStackLimits(
    IN ULONG_PTR Fp,
    IN ULONG_PTR *StackBegin,
    IN ULONG_PTR *StackEnd)
{
    DPRINT1("RtlpCaptureStackLimits: stub for ARM64\n");
    /* TODO: Implement stack limits */
    return FALSE;
}

ULONG
NTAPI
RtlWalkFrameChain(
    _Out_ PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    DPRINT1("RtlWalkFrameChain: stub for ARM64\n");
    /* TODO: Implement frame chain walking */
    return 0;
}