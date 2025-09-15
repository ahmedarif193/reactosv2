/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/ke/amd64/fpuexcept_simple.c
 * PURPOSE:         AMD64 Floating Point Exception Handling (Simplified)
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Handles floating point exceptions
 * @param TrapFrame - Trap frame from exception
 * @return None
 */
VOID
NTAPI
KiHandleFloatingError(
    _In_ PKTRAP_FRAME TrapFrame)
{
    EXCEPTION_RECORD ExceptionRecord;

    /* Build exception record */
    RtlZeroMemory(&ExceptionRecord, sizeof(EXCEPTION_RECORD));
    ExceptionRecord.ExceptionCode = STATUS_FLOAT_INVALID_OPERATION;
    ExceptionRecord.ExceptionFlags = 0;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Rip;
    ExceptionRecord.NumberParameters = 0;

    /* Clear FPU exceptions */
    __asm__ volatile ("fnclex");

    /* Dispatch the exception */
    KiDispatchException(&ExceptionRecord,
                        NULL,
                        TrapFrame,
                        UserMode,
                        TRUE);
}

/*
 * @implemented
 * @brief Initializes FPU exception handling
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializeFpuExceptionHandling(VOID)
{
    /* Set default x87 control word */
    USHORT ControlWord = 0x037F;  /* All exceptions masked, 64-bit precision */
    __asm__ volatile ("fldcw %0" : : "m" (ControlWord));

    DPRINT1("FPU exception handling initialized\n");
}

/*
 * @implemented
 * @brief Clears FPU exceptions
 * @return None
 */
VOID
NTAPI
KiClearFpuExceptions(VOID)
{
    /* Clear x87 exceptions */
    __asm__ volatile ("fnclex");

    /* Clear SSE exceptions if available */
    if (KeFeatureBits & KF_XMMI)
    {
        ULONG MxCsr;
        __asm__ volatile ("stmxcsr %0" : "=m" (MxCsr));
        MxCsr &= ~0x3F;  /* Clear exception flags */
        __asm__ volatile ("ldmxcsr %0" : : "m" (MxCsr));
    }
}

/*
 * @implemented
 * @brief Sets FPU exception mask
 * @param x87Mask - x87 exception mask
 * @param SseMask - SSE exception mask
 * @return Previous mask values
 */
ULONG
NTAPI
KeSetFpuExceptionMask(
    _In_ USHORT x87Mask,
    _In_ USHORT SseMask)
{
    USHORT OldControlWord;
    ULONG OldMxCsr = 0;
    ULONG OldMasks;

    /* Get current x87 control word */
    __asm__ volatile ("fnstcw %0" : "=m" (OldControlWord));

    /* Set new x87 mask */
    USHORT NewControlWord = (OldControlWord & ~0x3F) | (x87Mask & 0x3F);
    __asm__ volatile ("fldcw %0" : : "m" (NewControlWord));

    /* Handle SSE if available */
    if (KeFeatureBits & KF_XMMI)
    {
        /* Get current MXCSR */
        __asm__ volatile ("stmxcsr %0" : "=m" (OldMxCsr));

        /* Set new SSE mask */
        ULONG NewMxCsr = (OldMxCsr & ~0x1F80) | ((SseMask & 0x3F) << 7);
        __asm__ volatile ("ldmxcsr %0" : : "m" (NewMxCsr));
    }

    /* Return old masks */
    OldMasks = (OldControlWord & 0x3F) | ((OldMxCsr & 0x1F80) << 10);
    return OldMasks;
}

/* END OF FILE */