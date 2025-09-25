/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Debugger Trap Handling
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Exception Syndrome Register (ESR) fields */
#define ESR_EC_SHIFT        26
#define ESR_EC_MASK         0x3F
#define ESR_IL_BIT          (1 << 25)
#define ESR_ISS_MASK        0x1FFFFFF

/* Exception Class (EC) values for debugging */
#define ESR_EC_UNKNOWN      0x00
#define ESR_EC_WFI_WFE      0x01
#define ESR_EC_CP15_32      0x03
#define ESR_EC_CP15_64      0x04
#define ESR_EC_CP14_MR      0x05
#define ESR_EC_CP14_LS      0x06
#define ESR_EC_FP_ASIMD     0x07
#define ESR_EC_CP10_ID      0x08
#define ESR_EC_CP14_64      0x0C
#define ESR_EC_ILL_EXEC     0x0E
#define ESR_EC_SVC_32       0x11
#define ESR_EC_HVC_32       0x12
#define ESR_EC_SMC_32       0x13
#define ESR_EC_SVC_64       0x15
#define ESR_EC_HVC_64       0x16
#define ESR_EC_SMC_64       0x17
#define ESR_EC_SYS_REG      0x18
#define ESR_EC_INST_ABORT_L 0x20
#define ESR_EC_INST_ABORT   0x21
#define ESR_EC_ALIGN_FAULT  0x22
#define ESR_EC_DATA_ABORT_L 0x24
#define ESR_EC_DATA_ABORT   0x25
#define ESR_EC_SP_ALIGN     0x26
#define ESR_EC_FP_32        0x28
#define ESR_EC_FP_64        0x2C
#define ESR_EC_SERROR       0x2F
#define ESR_EC_BREAKPOINT_L 0x30
#define ESR_EC_BREAKPOINT   0x31
#define ESR_EC_SW_STEP_L    0x32
#define ESR_EC_SW_STEP      0x33
#define ESR_EC_WATCHPOINT_L 0x34
#define ESR_EC_WATCHPOINT   0x35
#define ESR_EC_BKPT_32      0x38
#define ESR_EC_BRK_64       0x3C

/* Breakpoint ISS fields */
#define BRK_IMM_MASK        0xFFFF
#define BRK_IMM_KD          0x0001  /* Kernel debugger breakpoint */
#define BRK_IMM_ASSERT      0x0002  /* Assertion failure */
#define BRK_IMM_USER        0x0003  /* User breakpoint */

/* Single-step ISS fields */
#define SS_ISV_BIT          (1 << 24)
#define SS_EX_BIT           (1 << 6)

/* Debugger states */
#define KD_STATE_DISCONNECTED   0
#define KD_STATE_CONNECTED      1
#define KD_STATE_ACTIVE         2
#define KD_STATE_BREAKING       3
#define KD_STATE_SINGLE_STEP    4

/* DATA STRUCTURES ************************************************************/

/* Kernel debugger context */
typedef struct _KD_CONTEXT
{
    ULONG State;
    BOOLEAN Enabled;
    BOOLEAN SingleStepEnabled;
    BOOLEAN BreakOnModuleLoad;
    BOOLEAN BreakOnException;
    PKTHREAD CurrentThread;
    PKTRAP_FRAME CurrentTrapFrame;
    PKEXCEPTION_FRAME CurrentExceptionFrame;
    ULONG LastException;
    PVOID LastBreakpoint;
    ULONG64 SavedMdscr;
    ULONG ActiveProcessorMask;
    KSPIN_LOCK DebuggerLock;
} KD_CONTEXT, *PKD_CONTEXT;

/* Exception record for kernel debugger */
typedef struct _KD_EXCEPTION_RECORD
{
    EXCEPTION_RECORD ExceptionRecord;
    ULONG FirstChance;
    ULONG ProcessorNumber;
    PKTHREAD Thread;
    PVOID ExceptionAddress;
} KD_EXCEPTION_RECORD, *PKD_EXCEPTION_RECORD;

/* GLOBALS ********************************************************************/

static KD_CONTEXT KdContext = {0};
static BOOLEAN KdInitialized = FALSE;
static BOOLEAN KdDebuggerPresent = FALSE;
static ULONG KdEntryCount = 0;

/* Exception handlers table */
static PVOID KdExceptionHandlers[64] = {0};

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Save debugger state before entering
 */
static
VOID
KdpSaveDebuggerState(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    /* Save current context */
    KdContext.CurrentThread = KeGetCurrentThread();
    KdContext.CurrentTrapFrame = TrapFrame;
    KdContext.CurrentExceptionFrame = ExceptionFrame;

    /* Save MDSCR_EL1 */
    __asm__ __volatile__ ("mrs %0, MDSCR_EL1" : "=r"(KdContext.SavedMdscr));

    /* Disable interrupts */
    _disable();
}

/*
 * @brief Restore debugger state after exiting
 */
static
VOID
KdpRestoreDebuggerState(VOID)
{
    /* Restore MDSCR_EL1 */
    __asm__ __volatile__ ("msr MDSCR_EL1, %0" :: "r"(KdContext.SavedMdscr));
    __asm__ __volatile__ ("isb" ::: "memory");

    /* Clear current context */
    KdContext.CurrentTrapFrame = NULL;
    KdContext.CurrentExceptionFrame = NULL;

    /* Re-enable interrupts if appropriate */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        _enable();
    }
}

/*
 * @brief Freeze other processors
 */
static
VOID
KdpFreezeProcessors(VOID)
{
    /* TODO: Send IPI to freeze other processors
     * This requires:
     * 1. Send IPI to all other processors
     * 2. Have them spin in a loop checking a flag
     * 3. Save their state for inspection
     */
    DPRINT("Freezing other processors\n");
}

/*
 * @brief Thaw other processors
 */
static
VOID
KdpThawProcessors(VOID)
{
    /* TODO: Release other processors from freeze
     * This requires:
     * 1. Set release flag
     * 2. Send IPI to wake them if needed
     * 3. Wait for acknowledgment
     */
    DPRINT("Thawing other processors\n");
}

/*
 * @brief Report exception to debugger
 */
static
BOOLEAN
KdpReportException(
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN BOOLEAN FirstChance)
{
    KD_EXCEPTION_RECORD KdException;

    DPRINT("Reporting exception: Code=0x%08lx, Address=%p, FirstChance=%d\n",
           ExceptionRecord->ExceptionCode,
           ExceptionRecord->ExceptionAddress,
           FirstChance);

    /* Build kernel debugger exception record */
    RtlCopyMemory(&KdException.ExceptionRecord, ExceptionRecord, sizeof(EXCEPTION_RECORD));
    KdException.FirstChance = FirstChance;
    KdException.ProcessorNumber = KeGetCurrentProcessorNumber();
    KdException.Thread = KeGetCurrentThread();
    KdException.ExceptionAddress = ExceptionRecord->ExceptionAddress;

    /* TODO: Send exception to remote debugger
     * This involves:
     * 1. Formatting exception packet
     * 2. Sending over debug transport
     * 3. Waiting for debugger response
     * 4. Processing debugger commands
     */

    /* For now, just break into local debugger */
    if (KdDebuggerPresent)
    {
        return TRUE;
    }

    return FALSE;
}

/*
 * @brief Handle specific exception class
 */
static
BOOLEAN
KdpHandleExceptionClass(
    IN ULONG ExceptionClass,
    IN ULONG Syndrome,
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Handled = FALSE;

    switch (ExceptionClass)
    {
        case ESR_EC_BREAKPOINT:
        case ESR_EC_BREAKPOINT_L:
        case ESR_EC_BRK_64:
        {
            ULONG BrkImm = Syndrome & BRK_IMM_MASK;

            DPRINT("Breakpoint exception: BRK #%lu at %p\n",
                   BrkImm, (PVOID)TrapFrame->Pc);

            /* Handle different breakpoint types */
            switch (BrkImm)
            {
                case 0:  /* Default breakpoint */
                case BRK_IMM_KD:
                    Handled = KdHandleBreakpointException(TrapFrame, ExceptionFrame);
                    break;

                case BRK_IMM_ASSERT:
                    DPRINT1("Assertion failure at %p\n", (PVOID)TrapFrame->Pc);
                    Handled = KdHandleBreakpointException(TrapFrame, ExceptionFrame);
                    break;

                case BRK_IMM_USER:
                    DPRINT("User breakpoint at %p\n", (PVOID)TrapFrame->Pc);
                    /* Let user-mode debugger handle it */
                    Handled = FALSE;
                    break;

                default:
                    DPRINT("Unknown breakpoint type %lu\n", BrkImm);
                    Handled = KdHandleBreakpointException(TrapFrame, ExceptionFrame);
                    break;
            }
            break;
        }

        case ESR_EC_SW_STEP:
        case ESR_EC_SW_STEP_L:
        {
            DPRINT("Single step exception at %p\n", (PVOID)TrapFrame->Pc);

            /* Disable single step */
            KdSetSingleStep(TrapFrame, FALSE);

            /* Enter debugger */
            Handled = KdEnterDebugger(TrapFrame, ExceptionFrame);
            break;
        }

        case ESR_EC_WATCHPOINT:
        case ESR_EC_WATCHPOINT_L:
        {
            DPRINT("Watchpoint exception at %p\n", (PVOID)TrapFrame->Pc);

            /* TODO: Handle hardware watchpoint
             * 1. Identify which watchpoint triggered
             * 2. Report to debugger
             * 3. Optionally disable watchpoint
             */
            Handled = KdEnterDebugger(TrapFrame, ExceptionFrame);
            break;
        }

        default:
            DPRINT("Unhandled exception class 0x%02lx\n", ExceptionClass);
            Handled = FALSE;
            break;
    }

    return Handled;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize kernel debugger trap handling
 * @implemented
 */
VOID
NTAPI
KdInitializeTrapHandling(VOID)
{
    if (KdInitialized)
        return;

    DPRINT("Initializing ARM64 kernel debugger trap handling\n");

    /* Initialize debugger context */
    RtlZeroMemory(&KdContext, sizeof(KD_CONTEXT));
    KdContext.State = KD_STATE_DISCONNECTED;
    KdContext.Enabled = FALSE;
    KeInitializeSpinLock(&KdContext.DebuggerLock);

    /* Initialize breakpoint system */
    KdInitializeBreakpoints();

    /* TODO: Initialize debug transport (serial, 1394, USB, network) */

    /* Check for debugger presence */
    KdDebuggerPresent = KdPollBreakIn();

    if (KdDebuggerPresent)
    {
        KdContext.State = KD_STATE_CONNECTED;
        KdContext.Enabled = TRUE;
        DPRINT("Kernel debugger connected\n");
    }

    KdInitialized = TRUE;
    DPRINT("ARM64 trap handling initialized\n");
}

/*
 * @brief Main kernel debugger trap handler
 * @implemented
 */
BOOLEAN
NTAPI
KdTrap(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN BOOLEAN FirstChance)
{
    ULONG64 Esr;
    ULONG ExceptionClass;
    ULONG Syndrome;
    BOOLEAN Handled = FALSE;
    KIRQL OldIrql;

    /* Check if debugger is enabled */
    if (!KdContext.Enabled && !KdDebuggerPresent)
    {
        return FALSE;
    }

    /* Read Exception Syndrome Register */
    __asm__ __volatile__ ("mrs %0, ESR_EL1" : "=r"(Esr));

    ExceptionClass = (Esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    Syndrome = Esr & ESR_ISS_MASK;

    DPRINT("KdTrap: Exception class 0x%02lx, syndrome 0x%07lx at PC=%p\n",
           ExceptionClass, Syndrome, (PVOID)TrapFrame->Pc);

    /* Raise IRQL and acquire lock */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KdContext.DebuggerLock);

    /* Save debugger state */
    KdpSaveDebuggerState(TrapFrame, ExceptionFrame);

    /* Update debugger state */
    KdContext.State = KD_STATE_ACTIVE;
    KdContext.LastException = ExceptionRecord ? ExceptionRecord->ExceptionCode : 0;

    /* Freeze other processors */
    if (KeNumberProcessors > 1)
    {
        KdpFreezeProcessors();
    }

    /* Handle based on exception class */
    if (ExceptionRecord)
    {
        /* Report exception to debugger */
        Handled = KdpReportException(ExceptionRecord, TrapFrame,
                                     ExceptionFrame, FirstChance);
    }
    else
    {
        /* Handle specific exception class */
        Handled = KdpHandleExceptionClass(ExceptionClass, Syndrome,
                                          TrapFrame, ExceptionFrame);
    }

    /* Thaw other processors */
    if (KeNumberProcessors > 1)
    {
        KdpThawProcessors();
    }

    /* Restore debugger state */
    KdpRestoreDebuggerState();
    KdContext.State = KD_STATE_CONNECTED;

    /* Release lock and lower IRQL */
    KeReleaseSpinLockFromDpcLevel(&KdContext.DebuggerLock);
    KeLowerIrql(OldIrql);

    DPRINT("KdTrap: Exception %s\n", Handled ? "handled" : "not handled");
    return Handled;
}

/*
 * @brief Enter kernel debugger
 * @implemented
 */
BOOLEAN
NTAPI
KdEnterDebugger(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    KIRQL OldIrql;
    BOOLEAN Entered;

    DPRINT("Entering kernel debugger at PC=%p\n", (PVOID)TrapFrame->Pc);

    /* Check for recursive entry */
    if (InterlockedIncrement(&KdEntryCount) > 1)
    {
        DPRINT1("Recursive debugger entry detected\n");
        InterlockedDecrement(&KdEntryCount);
        return FALSE;
    }

    /* Raise IRQL */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Save state */
    KdpSaveDebuggerState(TrapFrame, ExceptionFrame);

    /* Update state */
    KdContext.State = KD_STATE_BREAKING;

    /* Freeze other processors */
    if (KeNumberProcessors > 1)
    {
        KdpFreezeProcessors();
    }

    /* TODO: Main debugger loop
     * This should:
     * 1. Send state to remote debugger
     * 2. Wait for commands
     * 3. Process commands (read/write memory, registers, etc.)
     * 4. Continue when requested
     */

    /* For now, just print state */
    DPRINT("=== ARM64 Kernel Debugger ===\n");
    DPRINT("PC:  %016llx  SP:  %016llx\n", TrapFrame->Pc, TrapFrame->Sp);
    DPRINT("X0:  %016llx  X1:  %016llx\n", TrapFrame->X[0], TrapFrame->X[1]);
    DPRINT("X2:  %016llx  X3:  %016llx\n", TrapFrame->X[2], TrapFrame->X[3]);
    DPRINT("X29: %016llx  X30: %016llx\n", TrapFrame->X[29], TrapFrame->Lr);
    DPRINT("SPSR: %08lx\n", TrapFrame->Spsr);

    /* Check for break-in request */
    if (KdPollBreakIn())
    {
        DPRINT("Break-in detected\n");
        Entered = TRUE;
    }
    else
    {
        Entered = FALSE;
    }

    /* Thaw other processors */
    if (KeNumberProcessors > 1)
    {
        KdpThawProcessors();
    }

    /* Restore state */
    KdpRestoreDebuggerState();
    KdContext.State = KD_STATE_CONNECTED;

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    /* Decrement entry count */
    InterlockedDecrement(&KdEntryCount);

    return Entered;
}

/*
 * @brief Exit from kernel debugger
 * @implemented
 */
VOID
NTAPI
KdExitDebugger(
    IN BOOLEAN Continue)
{
    DPRINT("Exiting kernel debugger (continue=%d)\n", Continue);

    if (Continue)
    {
        /* Resume normal execution */
        KdContext.State = KD_STATE_CONNECTED;
    }
    else
    {
        /* Disconnect debugger */
        KdContext.State = KD_STATE_DISCONNECTED;
        KdContext.Enabled = FALSE;
        KdDebuggerPresent = FALSE;
    }
}

/*
 * @brief Poll for break-in request
 * @implemented
 */
BOOLEAN
NTAPI
KdPollBreakIn(VOID)
{
    /* TODO: Check debug transport for break-in character
     * This typically involves:
     * 1. Checking serial port for CTRL+C
     * 2. Checking other debug transports
     * 3. Checking for hardware debugger signals
     */

    /* For now, return FALSE */
    return FALSE;
}

/*
 * @brief Print string to kernel debugger
 * @implemented
 */
VOID
NTAPI
KdpPrintString(
    IN PSTRING String)
{
    /* TODO: Send string over debug transport
     * This involves:
     * 1. Formatting as debug output packet
     * 2. Sending over active transport
     * 3. Handling flow control
     */

    /* For now, just use DPRINT */
    if (String && String->Buffer && String->Length > 0)
    {
        DPRINT("%.*s", String->Length, String->Buffer);
    }
}

/*
 * @brief Prompt for input from kernel debugger
 * @implemented
 */
ULONG
NTAPI
KdpPromptString(
    IN PSTRING InputString,
    IN PSTRING OutputString)
{
    /* TODO: Interactive prompt via debug transport
     * This involves:
     * 1. Sending prompt string
     * 2. Reading input from debugger
     * 3. Echoing if appropriate
     * 4. Processing special characters
     */

    /* For now, return no input */
    if (InputString)
    {
        InputString->Length = 0;
    }

    return 0;
}

/*
 * @brief Check if kernel debugger is enabled
 * @implemented
 */
BOOLEAN
NTAPI
KdDebuggerEnabled(VOID)
{
    return KdContext.Enabled;
}

/*
 * @brief Check if kernel debugger is present/connected
 * @implemented
 */
BOOLEAN
NTAPI
KdDebuggerNotPresent(VOID)
{
    return !KdDebuggerPresent;
}

/*
 * @brief Refresh debugger presence
 * @implemented
 */
BOOLEAN
NTAPI
KdRefreshDebuggerPresent(VOID)
{
    BOOLEAN OldPresent = KdDebuggerPresent;

    /* Poll for debugger */
    KdDebuggerPresent = KdPollBreakIn();

    if (!OldPresent && KdDebuggerPresent)
    {
        /* Debugger just connected */
        KdContext.State = KD_STATE_CONNECTED;
        KdContext.Enabled = TRUE;
        DPRINT("Kernel debugger connected\n");
    }
    else if (OldPresent && !KdDebuggerPresent)
    {
        /* Debugger disconnected */
        KdContext.State = KD_STATE_DISCONNECTED;
        DPRINT("Kernel debugger disconnected\n");
    }

    return KdDebuggerPresent;
}

/*
 * @brief Query performance counter for profiling
 * @implemented
 */
LARGE_INTEGER
NTAPI
KdQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Counter;
    ULONG64 CounterValue;
    ULONG64 Frequency;

    /* Read ARM64 physical counter */
    __asm__ __volatile__ ("mrs %0, CNTPCT_EL0" : "=r"(CounterValue));
    Counter.QuadPart = CounterValue;

    if (PerformanceFrequency)
    {
        /* Read counter frequency */
        __asm__ __volatile__ ("mrs %0, CNTFRQ_EL0" : "=r"(Frequency));
        PerformanceFrequency->QuadPart = Frequency;
    }

    return Counter;
}

/*
 * @brief System debugger break service
 * @implemented
 */
VOID
NTAPI
KdSystemDebugControl(
    IN SYSDBG_COMMAND Command,
    IN PVOID InputBuffer,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer,
    IN ULONG OutputBufferLength,
    IN OUT PULONG ReturnLength,
    IN KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(Command);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ReturnLength);
    UNREFERENCED_PARAMETER(PreviousMode);

    /* TODO: Implement system debug control commands
     * This includes:
     * - Reading/writing kernel memory
     * - Reading/writing I/O ports
     * - Reading/writing MSRs
     * - Getting system information
     * - Controlling kernel debugger
     */

    DPRINT1("KdSystemDebugControl not fully implemented\n");
}

/* EOF */