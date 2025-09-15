/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/ke/amd64/fpuexcept.c
 * PURPOSE:         AMD64 Floating Point Exception Handling
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

/* x87 Status Word bits */
#define FSW_IE          0x0001  /* Invalid Operation */
#define FSW_DE          0x0002  /* Denormalized Operand */
#define FSW_ZE          0x0004  /* Zero Divide */
#define FSW_OE          0x0008  /* Overflow */
#define FSW_UE          0x0010  /* Underflow */
#define FSW_PE          0x0020  /* Precision */
#define FSW_SF          0x0040  /* Stack Fault */
#define FSW_ES          0x0080  /* Error Summary */
#define FSW_C0          0x0100  /* Condition Code 0 */
#define FSW_C1          0x0200  /* Condition Code 1 */
#define FSW_C2          0x0400  /* Condition Code 2 */
#define FSW_TOP         0x3800  /* Top of Stack Pointer */
#define FSW_C3          0x4000  /* Condition Code 3 */
#define FSW_B           0x8000  /* FPU Busy */

/* x87 Control Word bits */
#define FCW_IM          0x0001  /* Invalid Operation Mask */
#define FCW_DM          0x0002  /* Denormalized Operand Mask */
#define FCW_ZM          0x0004  /* Zero Divide Mask */
#define FCW_OM          0x0008  /* Overflow Mask */
#define FCW_UM          0x0010  /* Underflow Mask */
#define FCW_PM          0x0020  /* Precision Mask */
#define FCW_PC          0x0300  /* Precision Control */
#define FCW_RC          0x0C00  /* Rounding Control */

/* MXCSR bits (SSE/SSE2) */
#define MXCSR_IE        0x0001  /* Invalid Operation Flag */
#define MXCSR_DE        0x0002  /* Denormal Flag */
#define MXCSR_ZE        0x0004  /* Divide-by-Zero Flag */
#define MXCSR_OE        0x0008  /* Overflow Flag */
#define MXCSR_UE        0x0010  /* Underflow Flag */
#define MXCSR_PE        0x0020  /* Precision Flag */
#define MXCSR_DAZ       0x0040  /* Denormals Are Zeros */
#define MXCSR_IM        0x0080  /* Invalid Operation Mask */
#define MXCSR_DM        0x0100  /* Denormal Mask */
#define MXCSR_ZM        0x0200  /* Divide-by-Zero Mask */
#define MXCSR_OM        0x0400  /* Overflow Mask */
#define MXCSR_UM        0x0800  /* Underflow Mask */
#define MXCSR_PM        0x1000  /* Precision Mask */
#define MXCSR_RC        0x6000  /* Rounding Control */
#define MXCSR_FZ        0x8000  /* Flush to Zero */

/* Exception codes */
#define EXCEPTION_FLT_DENORMAL_OPERAND    STATUS_FLOAT_DENORMAL_OPERAND
#define EXCEPTION_FLT_DIVIDE_BY_ZERO      STATUS_FLOAT_DIVIDE_BY_ZERO
#define EXCEPTION_FLT_INEXACT_RESULT      STATUS_FLOAT_INEXACT_RESULT
#define EXCEPTION_FLT_INVALID_OPERATION   STATUS_FLOAT_INVALID_OPERATION
#define EXCEPTION_FLT_OVERFLOW            STATUS_FLOAT_OVERFLOW
#define EXCEPTION_FLT_STACK_CHECK         STATUS_FLOAT_STACK_CHECK
#define EXCEPTION_FLT_UNDERFLOW           STATUS_FLOAT_UNDERFLOW

/* STRUCTURES ****************************************************************/

typedef struct _FPU_SAVE_AREA
{
    USHORT ControlWord;
    USHORT StatusWord;
    USHORT TagWord;
    USHORT Reserved1;
    ULONG ErrorOffset;
    USHORT ErrorSelector;
    USHORT Reserved2;
    ULONG DataOffset;
    USHORT DataSelector;
    USHORT Reserved3;
    ULONG MxCsr;
    ULONG MxCsrMask;
    UCHAR St0[10];
    UCHAR Reserved4[6];
    UCHAR St1[10];
    UCHAR Reserved5[6];
    UCHAR St2[10];
    UCHAR Reserved6[6];
    UCHAR St3[10];
    UCHAR Reserved7[6];
    UCHAR St4[10];
    UCHAR Reserved8[6];
    UCHAR St5[10];
    UCHAR Reserved9[6];
    UCHAR St6[10];
    UCHAR Reserved10[6];
    UCHAR St7[10];
    UCHAR Reserved11[6];
    UCHAR Xmm0[16];
    UCHAR Xmm1[16];
    UCHAR Xmm2[16];
    UCHAR Xmm3[16];
    UCHAR Xmm4[16];
    UCHAR Xmm5[16];
    UCHAR Xmm6[16];
    UCHAR Xmm7[16];
    UCHAR Xmm8[16];
    UCHAR Xmm9[16];
    UCHAR Xmm10[16];
    UCHAR Xmm11[16];
    UCHAR Xmm12[16];
    UCHAR Xmm13[16];
    UCHAR Xmm14[16];
    UCHAR Xmm15[16];
} FPU_SAVE_AREA, *PFPU_SAVE_AREA;

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Saves the current FPU state
 * @param SaveArea - Buffer to save FPU state
 * @return None
 */
static VOID
KiSaveFpuState(
    _Out_ PFPU_SAVE_AREA SaveArea)
{
    /* Use FXSAVE to save FPU/MMX/SSE state */
    __asm__ volatile ("fxsave %0" : "=m" (*SaveArea));
}

/*
 * @brief Restores FPU state
 * @param SaveArea - Buffer containing FPU state
 * @return None
 */
static VOID
KiRestoreFpuState(
    _In_ PFPU_SAVE_AREA SaveArea)
{
    /* Use FXRSTOR to restore FPU/MMX/SSE state */
    __asm__ volatile ("fxrstor %0" : : "m" (*SaveArea));
}

/*
 * @brief Clears FPU exception flags
 * @return None
 */
static VOID
KiClearFpuExceptions(VOID)
{
    /* Clear x87 exceptions */
    __asm__ volatile ("fnclex");

    /* Clear SSE exceptions by writing to MXCSR */
    ULONG MxCsr = _mm_getcsr();
    MxCsr &= ~(MXCSR_IE | MXCSR_DE | MXCSR_ZE | MXCSR_OE | MXCSR_UE | MXCSR_PE);
    _mm_setcsr(MxCsr);
}

/*
 * @brief Determines the floating point exception code
 * @param StatusWord - x87 status word
 * @param MxCsr - SSE MXCSR register
 * @return Exception code
 */
static NTSTATUS
KiGetFpuExceptionCode(
    _In_ USHORT StatusWord,
    _In_ ULONG MxCsr)
{
    /* Check x87 exceptions first */
    if (StatusWord & FSW_IE)
        return EXCEPTION_FLT_INVALID_OPERATION;
    if (StatusWord & FSW_ZE)
        return EXCEPTION_FLT_DIVIDE_BY_ZERO;
    if (StatusWord & FSW_DE)
        return EXCEPTION_FLT_DENORMAL_OPERAND;
    if (StatusWord & FSW_OE)
        return EXCEPTION_FLT_OVERFLOW;
    if (StatusWord & FSW_UE)
        return EXCEPTION_FLT_UNDERFLOW;
    if (StatusWord & FSW_PE)
        return EXCEPTION_FLT_INEXACT_RESULT;
    if (StatusWord & FSW_SF)
        return EXCEPTION_FLT_STACK_CHECK;

    /* Check SSE exceptions */
    if (MxCsr & MXCSR_IE)
        return EXCEPTION_FLT_INVALID_OPERATION;
    if (MxCsr & MXCSR_ZE)
        return EXCEPTION_FLT_DIVIDE_BY_ZERO;
    if (MxCsr & MXCSR_DE)
        return EXCEPTION_FLT_DENORMAL_OPERAND;
    if (MxCsr & MXCSR_OE)
        return EXCEPTION_FLT_OVERFLOW;
    if (MxCsr & MXCSR_UE)
        return EXCEPTION_FLT_UNDERFLOW;
    if (MxCsr & MXCSR_PE)
        return EXCEPTION_FLT_INEXACT_RESULT;

    /* Unknown exception */
    return EXCEPTION_FLT_INVALID_OPERATION;
}

/*
 * @brief Emulates FPU instruction for exception recovery
 * @param TrapFrame - Trap frame
 * @param FpuState - FPU state
 * @return TRUE if handled, FALSE otherwise
 */
static BOOLEAN
KiEmulateFpuInstruction(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PFPU_SAVE_AREA FpuState)
{
    PUCHAR InstructionPointer;
    UCHAR Opcode;

    /* Get instruction pointer */
    InstructionPointer = (PUCHAR)TrapFrame->Rip;

    /* Read opcode */
    __try
    {
        Opcode = *InstructionPointer;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }

    /* Check for common FPU instructions that might need emulation */
    switch (Opcode)
    {
        case 0xD8: /* FADD, FMUL, FCOM, FCOMP, FSUB, FSUBR, FDIV, FDIVR */
        case 0xD9: /* FLD, FST, FSTP, FLDENV, FSTENV, etc. */
        case 0xDA: /* FIADD, FIMUL, FICOM, FICOMP, FISUB, FISUBR, FIDIV, FIDIVR */
        case 0xDB: /* FILD, FIST, FISTP, etc. */
        case 0xDC: /* FADD, FMUL, FCOM, FCOMP, FSUB, FSUBR, FDIV, FDIVR (double) */
        case 0xDD: /* FLD, FST, FSTP (double), etc. */
        case 0xDE: /* FIADD, FIMUL, FICOM, FICOMP, FISUB, FISUBR, FIDIV, FIDIVR (word) */
        case 0xDF: /* FILD, FIST, FISTP (word), etc. */
            /* For now, we don't emulate - just skip the instruction */
            DPRINT1("FPU instruction emulation not implemented for opcode 0x%02X\n", Opcode);
            return FALSE;

        case 0x0F: /* Two-byte opcodes (SSE/SSE2) */
            /* Check for SSE instructions */
            __try
            {
                Opcode = *(InstructionPointer + 1);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                return FALSE;
            }

            /* Common SSE opcodes that might fault */
            switch (Opcode)
            {
                case 0x10: /* MOVUPS */
                case 0x11: /* MOVUPS */
                case 0x28: /* MOVAPS */
                case 0x29: /* MOVAPS */
                case 0x57: /* XORPS */
                case 0x58: /* ADDPS */
                case 0x59: /* MULPS */
                case 0x5C: /* SUBPS */
                case 0x5E: /* DIVPS */
                    DPRINT1("SSE instruction emulation not implemented for opcode 0F %02X\n", Opcode);
                    return FALSE;
            }
            break;
    }

    return FALSE;
}

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
    FPU_SAVE_AREA FpuState;
    PKTHREAD Thread;
    NTSTATUS ExceptionCode;
    EXCEPTION_RECORD ExceptionRecord;
    BOOLEAN UserMode;

    /* Get current thread */
    Thread = KeGetCurrentThread();

    /* Save current FPU state */
    KiSaveFpuState(&FpuState);

    /* Determine if we're in user mode */
    UserMode = (TrapFrame->SegCs & MODE_MASK) != 0;

    DPRINT("Floating point exception: StatusWord=%04X, MxCsr=%08X, RIP=%p\n",
           FpuState.StatusWord, FpuState.MxCsr, TrapFrame->Rip);

    /* Get the exception code */
    ExceptionCode = KiGetFpuExceptionCode(FpuState.StatusWord, FpuState.MxCsr);

    /* Try to emulate the instruction if possible */
    if (KiEmulateFpuInstruction(TrapFrame, &FpuState))
    {
        /* Emulation successful, clear exceptions and continue */
        KiClearFpuExceptions();
        return;
    }

    /* Clear FPU exceptions to prevent re-triggering */
    KiClearFpuExceptions();

    /* Build exception record */
    RtlZeroMemory(&ExceptionRecord, sizeof(EXCEPTION_RECORD));
    ExceptionRecord.ExceptionCode = ExceptionCode;
    ExceptionRecord.ExceptionFlags = 0;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Rip;
    ExceptionRecord.NumberParameters = 0;

    /* Dispatch the exception */
    if (UserMode)
    {
        /* User mode exception */
        KiDispatchException(&ExceptionRecord,
                            NULL,
                            TrapFrame,
                            UserMode,
                            TRUE);
    }
    else
    {
        /* Kernel mode exception */
        if (!KiHandleNmi() &&
            !KdDebuggerEnabled)
        {
            /* Fatal kernel exception */
            KeBugCheckEx(TRAP_CAUSE_UNKNOWN,
                        16,  /* Floating point error */
                        (ULONG_PTR)TrapFrame->Rip,
                        FpuState.StatusWord,
                        FpuState.MxCsr);
        }

        /* Try to dispatch to kernel debugger */
        KiDispatchException(&ExceptionRecord,
                           NULL,
                           TrapFrame,
                           FALSE,
                           TRUE);
    }
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
    ULONG MxCsr;

    /* Set default x87 control word */
    USHORT ControlWord = 0x037F;  /* All exceptions masked, 64-bit precision */
    __asm__ volatile ("fldcw %0" : : "m" (ControlWord));

    /* Set default MXCSR */
    MxCsr = MXCSR_IM | MXCSR_DM | MXCSR_ZM | MXCSR_OM | MXCSR_UM | MXCSR_PM;
    MxCsr |= MXCSR_RC;  /* Round to nearest */
    _mm_setcsr(MxCsr);

    DPRINT1("FPU exception handling initialized\n");
}

/*
 * @implemented
 * @brief Gets current FPU exception state
 * @param StatusWord - Receives x87 status word
 * @param MxCsr - Receives MXCSR
 * @return None
 */
VOID
NTAPI
KeGetFpuExceptionState(
    _Out_opt_ PUSHORT StatusWord,
    _Out_opt_ PULONG MxCsr)
{
    FPU_SAVE_AREA FpuState;

    /* Save current state */
    KiSaveFpuState(&FpuState);

    /* Return requested values */
    if (StatusWord)
        *StatusWord = FpuState.StatusWord;

    if (MxCsr)
        *MxCsr = FpuState.MxCsr;
}

/*
 * @implemented
 * @brief Sets FPU exception masks
 * @param x87Mask - x87 exception mask
 * @param SseMask - SSE exception mask
 * @return Previous masks
 */
ULONG
NTAPI
KeSetFpuExceptionMask(
    _In_ USHORT x87Mask,
    _In_ USHORT SseMask)
{
    FPU_SAVE_AREA FpuState;
    ULONG OldMasks;

    /* Save current state */
    KiSaveFpuState(&FpuState);

    /* Save old masks */
    OldMasks = (FpuState.ControlWord & 0x3F) | ((FpuState.MxCsr & 0x1F80) << 10);

    /* Update x87 control word */
    FpuState.ControlWord = (FpuState.ControlWord & ~0x3F) | (x87Mask & 0x3F);

    /* Update MXCSR */
    FpuState.MxCsr = (FpuState.MxCsr & ~0x1F80) | ((SseMask & 0x3F) << 7);

    /* Restore state with new masks */
    KiRestoreFpuState(&FpuState);

    return OldMasks;
}

/* END OF FILE */