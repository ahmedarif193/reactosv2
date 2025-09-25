/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Exception and Trap Handler C routines
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define PL011_BASE   0x09000000U
#define PL011_FR     (*(volatile ULONG *)(PL011_BASE + 0x18))
#define PL011_DR     (*(volatile ULONG *)(PL011_BASE + 0x00))
#define PL011_TXFF   (1u << 5)

static VOID KiTrapUartPutc(char Ch)
{
    while (PL011_FR & PL011_TXFF)
    {
        __asm__ __volatile__("wfi");
    }
    PL011_DR = (unsigned char)Ch;
}

static VOID KiTrapUartPuts(const char *String)
{
    while (*String)
    {
        if (*String == '\n')
            KiTrapUartPutc('\r');
        KiTrapUartPutc(*String++);
    }
}

static VOID KiTrapUartPutHex(ULONGLONG Value, ULONG Nibbles)
{
    static const char HexDigits[] = "0123456789ABCDEF";

    for (LONG Index = (LONG)Nibbles - 1; Index >= 0; --Index)
    {
        ULONG Shift = (ULONG)Index * 4;
        KiTrapUartPutc(HexDigits[(Value >> Shift) & 0xFULL]);
    }
}

static VOID KiTrapUartPutDec(ULONG Value)
{
    char Buffer[10];
    ULONG Pos = 0;

    if (Value == 0)
    {
        KiTrapUartPutc('0');
        return;
    }

    while (Value && Pos < RTL_NUMBER_OF(Buffer))
    {
        Buffer[Pos++] = (char)('0' + (Value % 10));
        Value /= 10;
    }

    while (Pos)
    {
        KiTrapUartPutc(Buffer[--Pos]);
    }
}

/* TYPES *********************************************************************/

/* Use the official KTRAP_FRAME structure from ketypes.h */

/* FUNCTION PROTOTYPES *******************************************************/

/* Forward declarations for functions defined later in this file */
VOID NTAPI KiBreakpointTrapC(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiBugCheck(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiIllegalInstruction(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiKernelDataAbort(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiKernelInstructionAbort(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiAlignmentFault(IN PKTRAP_FRAME TrapFrame);
VOID NTAPI KiStackAlignmentFault(IN PKTRAP_FRAME TrapFrame);

/* ARM64 Exception Syndrome Register (ESR) definitions */
#define ESR_ELx_EC_SHIFT        26
#define ESR_ELx_EC_MASK         (0x3F << ESR_ELx_EC_SHIFT)
#define ESR_ELx_EC(esr)         (((esr) & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT)

/* Exception Classes */
#define ESR_ELx_EC_UNKNOWN      0x00
#define ESR_ELx_EC_WFx          0x01    /* WFI/WFE instruction */
#define ESR_ELx_EC_CP15_32      0x03    /* CP15 MCR/MRC (AArch32) */
#define ESR_ELx_EC_CP15_64      0x04    /* CP15 MCRR/MRRC (AArch32) */
#define ESR_ELx_EC_CP14_MR      0x05    /* CP14 MCR/MRC (AArch32) */
#define ESR_ELx_EC_CP14_LS      0x06    /* CP14 LDC/STC (AArch32) */
#define ESR_ELx_EC_FP_ASIMD     0x07    /* FP/ASIMD access */
#define ESR_ELx_EC_CP10_ID      0x08    /* CP10 MCR/MRC (AArch32) */
#define ESR_ELx_EC_PAC          0x09    /* Pointer Authentication */
#define ESR_ELx_EC_CP14_64      0x0C    /* CP14 MCRR/MRRC (AArch32) */
#define ESR_ELx_EC_BTI          0x0D    /* Branch Target Exception */
#define ESR_ELx_EC_ILL          0x0E    /* Illegal Execution State */
#define ESR_ELx_EC_SVC32        0x11    /* SVC instruction (AArch32) */
#define ESR_ELx_EC_HVC32        0x12    /* HVC instruction (AArch32) */
#define ESR_ELx_EC_SMC32        0x13    /* SMC instruction (AArch32) */
#define ESR_ELx_EC_SVC64        0x15    /* SVC instruction (AArch64) */
#define ESR_ELx_EC_HVC64        0x16    /* HVC instruction (AArch64) */
#define ESR_ELx_EC_SMC64        0x17    /* SMC instruction (AArch64) */
#define ESR_ELx_EC_SYS64        0x18    /* System register access */
#define ESR_ELx_EC_SVE          0x19    /* SVE access */
#define ESR_ELx_EC_IMP_DEF      0x1F    /* Implementation defined */
#define ESR_ELx_EC_IABT_LOW     0x20    /* Instruction Abort (lower EL) */
#define ESR_ELx_EC_IABT_CUR     0x21    /* Instruction Abort (current EL) */
#define ESR_ELx_EC_PC_ALIGN     0x22    /* PC alignment fault */
#define ESR_ELx_EC_DABT_LOW     0x24    /* Data Abort (lower EL) */
#define ESR_ELx_EC_DABT_CUR     0x25    /* Data Abort (current EL) */
#define ESR_ELx_EC_SP_ALIGN     0x26    /* SP alignment fault */
#define ESR_ELx_EC_FP_EXC32     0x28    /* FP exception (AArch32) */
#define ESR_ELx_EC_FP_EXC64     0x2C    /* FP exception (AArch64) */
#define ESR_ELx_EC_SERROR       0x2F    /* SError interrupt */
#define ESR_ELx_EC_BREAKPT_LOW  0x30    /* Breakpoint (lower EL) */
#define ESR_ELx_EC_BREAKPT_CUR  0x31    /* Breakpoint (current EL) */
#define ESR_ELx_EC_SOFTSTP_LOW  0x32    /* Software step (lower EL) */
#define ESR_ELx_EC_SOFTSTP_CUR  0x33    /* Software step (current EL) */
#define ESR_ELx_EC_WATCHPT_LOW  0x34    /* Watchpoint (lower EL) */
#define ESR_ELx_EC_WATCHPT_CUR  0x35    /* Watchpoint (current EL) */
#define ESR_ELx_EC_BKPT32       0x38    /* BKPT instruction (AArch32) */
#define ESR_ELx_EC_VECTOR32     0x3A    /* Vector catch (AArch32) */
#define ESR_ELx_EC_BRK64        0x3C    /* BRK instruction (AArch64) */

/* Data Abort ISS fields */
#define ESR_ELx_ISV             (1ULL << 24)
#define ESR_ELx_SAS_SHIFT       22
#define ESR_ELx_SAS_MASK        (3ULL << ESR_ELx_SAS_SHIFT)
#define ESR_ELx_SSE             (1ULL << 21)
#define ESR_ELx_SRT_SHIFT       16
#define ESR_ELx_SRT_MASK        (0x1F << ESR_ELx_SRT_SHIFT)
#define ESR_ELx_SF              (1ULL << 15)
#define ESR_ELx_AR              (1ULL << 14)
#define ESR_ELx_VNCR            (1ULL << 13)
#define ESR_ELx_SET_SHIFT       11
#define ESR_ELx_SET_MASK        (3ULL << ESR_ELx_SET_SHIFT)
#define ESR_ELx_FnV             (1ULL << 10)
#define ESR_ELx_EA              (1ULL << 9)
#define ESR_ELx_S1PTW           (1ULL << 7)
#define ESR_ELx_WnR             (1ULL << 6)
#define ESR_ELx_DFSC_MASK       0x3F
#define ESR_ELx_ISS_MASK        0x1FFFFFF

static const char* Arm64FaultStatusNames[] = {
    "Address size fault (level 0)", "Address size fault (level 1)", "Address size fault (level 2)", "Address size fault (level 3)",
    "Translation fault (level 0)", "Translation fault (level 1)", "Translation fault (level 2)", "Translation fault (level 3)",
    "Access flag fault (level 0)", "Access flag fault (level 1)", "Access flag fault (level 2)", "Access flag fault (level 3)",
    "Permission fault (level 0)", "Permission fault (level 1)", "Permission fault (level 2)", "Permission fault (level 3)"
};

static VOID KiDescribeAbort(ULONG ExceptionClass, ULONG ISS, ULONGLONG FaultAddr)
{
    ULONG FaultStatus = ISS & ESR_ELx_DFSC_MASK;
    BOOLEAN Stage1Walk = (ISS & ESR_ELx_S1PTW) != 0;
    BOOLEAN IsInstrAbort = (ExceptionClass == ESR_ELx_EC_IABT_CUR || ExceptionClass == ESR_ELx_EC_IABT_LOW);
    BOOLEAN IsWrite = (ISS & ESR_ELx_WnR) != 0;
    const char *FaultName = (FaultStatus < ARRAYSIZE(Arm64FaultStatusNames)) ?
                            Arm64FaultStatusNames[FaultStatus] : "Unknown fault";

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "Detail: %s abort at 0x%016llX (%s)%s [DFSC=0x%02lX]\n",
               IsInstrAbort ? "Instruction" : (IsWrite ? "Data (write)" : "Data (read)"),
               FaultAddr,
               FaultName,
               Stage1Walk ? " via table walk" : "",
               FaultStatus);

    KiTrapUartPuts("Detail: ");
    KiTrapUartPuts(IsInstrAbort ? "Instruction" : (IsWrite ? "Data (write)" : "Data (read)"));
    KiTrapUartPuts(" abort @0x");
    KiTrapUartPutHex(FaultAddr, 16);
    KiTrapUartPuts(" ");
    KiTrapUartPuts(FaultName);
    if (Stage1Walk)
        KiTrapUartPuts(" walk");
    KiTrapUartPuts(" DFSC=0x");
    KiTrapUartPutHex(FaultStatus, 2);
    KiTrapUartPuts("\n");
}

static VOID KiDumpBacktrace(PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR StackTop = Thread ? (ULONG_PTR)Thread->StackBase : TrapFrame->Sp;
    ULONG_PTR StackBottom = Thread ? (ULONG_PTR)Thread->StackLimit : (StackTop - KERNEL_STACK_SIZE);
    ULONG_PTR fp_walk = (ULONG_PTR)TrapFrame->Fp;
    ULONG frames = 0;
    const ULONG max_frames = 32;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "Backtrace (ARM64):\n");
    KiTrapUartPuts("Backtrace (ARM64)\n");

    while (frames < max_frames)
    {
        if ((fp_walk & 0xF) != 0)
            break;
        if (fp_walk < StackBottom || fp_walk + 16 > StackTop)
            break;

        ULONG_PTR *slot = (ULONG_PTR *)fp_walk;
        ULONG_PTR next_fp = slot[0];
        ULONG_PTR lr = slot[1];

        if (lr == 0 || next_fp <= fp_walk)
            break;

        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "    0x%016llX\n",
                   (unsigned long long)lr);

        KiTrapUartPuts("    0x");
        KiTrapUartPutHex(lr, 16);
        KiTrapUartPuts("\n");

        fp_walk = next_fp;
        frames++;
    }
}

static
PCSTR
KiGetExceptionClassString(
    _In_ ULONG ExceptionClass
)
{
    switch (ExceptionClass)
    {
        case ESR_ELx_EC_DABT_CUR:
            return "Data Abort (EL1)";
        case ESR_ELx_EC_DABT_LOW:
            return "Data Abort (EL0)";
        case ESR_ELx_EC_IABT_CUR:
            return "Instruction Abort (EL1)";
        case ESR_ELx_EC_IABT_LOW:
            return "Instruction Abort (EL0)";
        case ESR_ELx_EC_PC_ALIGN:
            return "PC Alignment Fault";
        case ESR_ELx_EC_SP_ALIGN:
            return "SP Alignment Fault";
        case ESR_ELx_EC_BRK64:
            return "BRK";
        case ESR_ELx_EC_SVC64:
            return "SVC64";
        case ESR_ELx_EC_SYS64:
            return "System Register";
        case ESR_ELx_EC_UNKNOWN:
            return "Unknown";
        default:
            return "Unhandled";
    }
}

static
VOID
KiDumpTrapFrameDebug(
    _In_ PCSTR Reason,
    _In_ PKTRAP_FRAME TrapFrame
)
{
    ULONGLONG *Regs = &TrapFrame->X0;
    ULONG ExceptionClass = ESR_ELx_EC((ULONG)TrapFrame->Esr);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "ARM64 Trap: %s (EC=0x%02lX %s)\n"
               "  ESR=0x%016llX FAR=0x%016llX\n"
               "  PC =0x%016llX SP =0x%016llX PSTATE=0x%016llX\n",
               Reason,
               ExceptionClass,
               KiGetExceptionClassString(ExceptionClass),
               (unsigned long long)TrapFrame->Esr,
               (unsigned long long)TrapFrame->Far,
               (unsigned long long)TrapFrame->Pc,
               (unsigned long long)TrapFrame->Sp,
               (unsigned long long)TrapFrame->Pstate);

    KiTrapUartPuts("ARM64 Trap: ");
    KiTrapUartPuts(Reason);
    KiTrapUartPuts(" (EC=0x");
    KiTrapUartPutHex(ExceptionClass, 2);
    KiTrapUartPuts(" ");
    KiTrapUartPuts(KiGetExceptionClassString(ExceptionClass));
    KiTrapUartPuts(")\n  ESR=0x");
    KiTrapUartPutHex(TrapFrame->Esr, 16);
    KiTrapUartPuts(" FAR=0x");
    KiTrapUartPutHex(TrapFrame->Far, 16);
    KiTrapUartPuts("\n  PC =0x");
    KiTrapUartPutHex(TrapFrame->Pc, 16);
    KiTrapUartPuts(" SP =0x");
    KiTrapUartPutHex(TrapFrame->Sp, 16);
    KiTrapUartPuts(" PSTATE=0x");
    KiTrapUartPutHex(TrapFrame->Pstate, 16);
    KiTrapUartPuts("\n");

    for (ULONG i = 0; i < 31; i += 2)
    {
        if (i + 1 < 31)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "    X%02lu=0x%016llX  X%02lu=0x%016llX\n",
                       i,
                       (unsigned long long)Regs[i],
                       i + 1,
                       (unsigned long long)Regs[i + 1]);

            KiTrapUartPuts("    X");
            KiTrapUartPutDec(i);
            KiTrapUartPuts("=0x");
            KiTrapUartPutHex(Regs[i], 16);
            KiTrapUartPuts("  X");
            KiTrapUartPutDec(i + 1);
            KiTrapUartPuts("=0x");
            KiTrapUartPutHex(Regs[i + 1], 16);
            KiTrapUartPuts("\n");
        }
        else
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "    X%02lu=0x%016llX\n",
                       i,
                       (unsigned long long)Regs[i]);

            KiTrapUartPuts("    X");
            KiTrapUartPutDec(i);
            KiTrapUartPuts("=0x");
            KiTrapUartPutHex(Regs[i], 16);
            KiTrapUartPuts("\n");
        }
    }

    KiDumpBacktrace(TrapFrame);
}

/* GLOBALS *******************************************************************/

/* FUNCTIONS *****************************************************************/

/**
 * @brief Handle kernel trap (synchronous exception from kernel mode)
 */
VOID
NTAPI
KiTrapHandlerC(
    IN PKTRAP_FRAME TrapFrame
)
{
    ULONG ExceptionClass = ESR_ELx_EC((ULONG)TrapFrame->Esr);

    KiDumpTrapFrameDebug("Synchronous exception", TrapFrame);
    
    DPRINT("ARM64: Kernel Trap - EC=0x%02X, ESR=0x%llX, FAR=0x%llX, PC=0x%llX\n",
           ExceptionClass, TrapFrame->Esr, TrapFrame->Far, TrapFrame->Pc);
    
    switch (ExceptionClass)
    {
        case ESR_ELx_EC_DABT_LOW:
        case ESR_ELx_EC_DABT_CUR:
        {
            ULONG Iss = (ULONG)(TrapFrame->Esr & ESR_ELx_ISS_MASK);
            KiDescribeAbort(ExceptionClass, Iss, TrapFrame->Far);
            KiKernelDataAbort(TrapFrame);
            break;
        }

        case ESR_ELx_EC_IABT_LOW:
        case ESR_ELx_EC_IABT_CUR:
        {
            ULONG Iss = (ULONG)(TrapFrame->Esr & ESR_ELx_ISS_MASK);
            KiDescribeAbort(ExceptionClass, Iss, TrapFrame->Far);
            KiKernelInstructionAbort(TrapFrame);
            break;
        }

        case ESR_ELx_EC_PC_ALIGN:
            /* PC alignment fault */
            KiAlignmentFault(TrapFrame);
            break;
            
        case ESR_ELx_EC_SP_ALIGN:
            /* Stack pointer alignment fault */
            KiStackAlignmentFault(TrapFrame);
            break;
            
        case ESR_ELx_EC_BRK64:
            /* Software breakpoint */
            KiBreakpointTrapC(TrapFrame);
            break;
            
        case ESR_ELx_EC_ILL:
            /* Illegal execution state */
            KiIllegalInstruction(TrapFrame);
            break;
            
        default:
            DPRINT1("ARM64: Unhandled kernel trap - EC=0x%02X\n", ExceptionClass);
            KiBugCheck(TrapFrame);
            break;
    }
}

/**
 * @brief Handle interrupt (IRQ)
 */
VOID
NTAPI
KiInterruptHandlerC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: IRQ at PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement GIC interrupt handling */
    /* For now, just acknowledge and return */
}

/**
 * @brief Handle fast interrupt (FIQ)
 */
VOID
NTAPI
KiFiqHandlerC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: FIQ at PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement FIQ handling */
}

/**
 * @brief Handle system error (SError)
 */
VOID
NTAPI
KiSerrorHandlerC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT1("ARM64: SError - ESR=0x%llX, FAR=0x%llX, PC=0x%llX\n",
            TrapFrame->Esr, TrapFrame->Far, TrapFrame->Pc);
    
    /* SError is typically fatal */
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle AArch64 system call
 */
VOID
NTAPI
KiSystemCallHandler64C(
    IN PKTRAP_FRAME TrapFrame
)
{
    ULONG SystemCallNumber = (ULONG)TrapFrame->X8;
    
    DPRINT("ARM64: System call %u from PC=0x%llX\n", SystemCallNumber, TrapFrame->Pc);
    
    /* TODO: Implement system call dispatch */
    /* For now, return error */
    TrapFrame->X0 = STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Handle AArch32 system call (compatibility)
 */
VOID
NTAPI
KiSystemCallHandler32C(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: AArch32 system call from PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement AArch32 system call compatibility */
    TrapFrame->X0 = STATUS_NOT_SUPPORTED;
}

/**
 * @brief Handle kernel data abort
 */
VOID
NTAPI
KiKernelDataAbort(
    IN PKTRAP_FRAME TrapFrame
)
{
    ULONG FaultStatus = TrapFrame->Esr & ESR_ELx_DFSC_MASK;
    BOOLEAN IsWrite = (TrapFrame->Esr & ESR_ELx_WnR) != 0;
    
    DPRINT1("ARM64: Kernel Data Abort - Address=0x%llX, %s, Status=0x%02X\n",
            TrapFrame->Far, IsWrite ? "Write" : "Read", FaultStatus);
    
    /* This is likely fatal in kernel mode */
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle kernel instruction abort
 */
VOID
NTAPI
KiKernelInstructionAbort(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT1("ARM64: Kernel Instruction Abort at PC=0x%llX\n", TrapFrame->Pc);
    
    /* This is fatal */
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle alignment fault
 */
VOID
NTAPI
KiAlignmentFault(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT1("ARM64: PC Alignment Fault at PC=0x%llX\n", TrapFrame->Pc);
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle stack alignment fault
 */
VOID
NTAPI
KiStackAlignmentFault(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT1("ARM64: Stack Alignment Fault - SP=0x%llX, PC=0x%llX\n",
            TrapFrame->Sp, TrapFrame->Pc);
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle illegal instruction
 */
VOID
NTAPI
KiIllegalInstruction(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT1("ARM64: Illegal Instruction at PC=0x%llX\n", TrapFrame->Pc);
    KiBugCheck(TrapFrame);
}

/**
 * @brief Handle breakpoint exception
 */
VOID
NTAPI
KiBreakpointTrapC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: Breakpoint at PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement kernel debugger integration */
    /* For now, just continue */
    TrapFrame->Pc += 4;  /* Skip BRK instruction */
}

/**
 * @brief Handle single step exception
 */
VOID
NTAPI
KiSingleStepTrapC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: Single step at PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement single stepping support */
}

/**
 * @brief Handle debug service
 */
VOID
NTAPI
KiDebugServiceC(
    IN PKTRAP_FRAME TrapFrame
)
{
    DPRINT("ARM64: Debug service at PC=0x%llX\n", TrapFrame->Pc);
    
    /* TODO: Implement debug services */
}

/**
 * @brief Handle unexpected interrupt
 */
VOID
NTAPI
KiUnexpectedInterruptC(
    IN ULONGLONG Esr,
    IN ULONGLONG Elr,
    IN ULONGLONG Far
)
{
    DPRINT1("ARM64: Unexpected Interrupt - ESR=0x%llX, ELR=0x%llX, FAR=0x%llX\n",
            Esr, Elr, Far);
}

/**
 * @brief Display unexpected interrupt information
 */
VOID
NTAPI
KiDisplayUnexpectedInterruptC(
    IN ULONG InterruptType
)
{
    DPRINT1("ARM64: Unexpected interrupt type %u\n", InterruptType);
}

/**
 * @brief Thread startup C routine
 */
VOID
NTAPI
KiThreadStartupC(VOID)
{
    /* TODO: Implement thread startup */
    DPRINT("ARM64: Thread startup\n");
}

/**
 * @brief Bug check with trap frame information
 */
VOID
NTAPI
KiBugCheck(
    IN PKTRAP_FRAME TrapFrame
)
{
    KiDumpTrapFrameDebug("BugCheck", TrapFrame);

    DPRINT1("ARM64: KERNEL BUG CHECK\n");
    DPRINT1("PC=0x%llX, SP=0x%llX, PSTATE=0x%llX\n",
            TrapFrame->Pc, TrapFrame->Sp, TrapFrame->Pstate);
    DPRINT1("ESR=0x%llX, FAR=0x%llX\n", TrapFrame->Esr, TrapFrame->Far);
    
    /* Call system bug check */
    KeBugCheck(KERNEL_SECURITY_CHECK_FAILURE);
}
