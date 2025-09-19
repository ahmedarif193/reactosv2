/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0)
 * FILE:            ntoskrnl/ke/arm64/context.c
 * PURPOSE:         ARM64 Context Switching & Capture
 * PROGRAMMERS:     ReactOS ARM64 Team
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    /* ARM64 trap frame to context conversion */
    UNIMPLEMENTED;
}

VOID
NTAPI
KeContextToTrapFrame(
    _In_ PCONTEXT Context,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    /* ARM64 context to trap frame conversion */
    UNIMPLEMENTED;
}