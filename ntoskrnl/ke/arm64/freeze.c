/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Processor freeze support for ARM64
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

PKPRCB KiFreezeOwner;

/* FUNCTIONS *****************************************************************/

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    /* ARM64 processor freeze handler */
    UNIMPLEMENTED;
    return FALSE;
}

VOID
KxFreezeExecution(VOID)
{
    /* ARM64 freeze execution */
    UNIMPLEMENTED;
}

VOID
KxThawExecution(VOID)
{
    /* ARM64 thaw execution */
    UNIMPLEMENTED;
}

KCONTINUE_STATUS
KxSwitchKdProcessor(
    _In_ ULONG ProcessorNumber)
{
    /* ARM64 switch KD processor */
    UNIMPLEMENTED;
    return ContinueSuccess;
}