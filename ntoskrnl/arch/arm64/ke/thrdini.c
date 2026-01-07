/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/thrdini.c
 * PURPOSE:         Thread startup stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Stack layout used during context acquisition:
 *
 *   +-------------------------------+
 *   |   KTRAP_FRAME (user threads)  |
 *   +-------------------------------+
 *   | KEXCEPTION_FRAME (user only)  |
 *   +-------------------------------+
 *   |        KSTART_FRAME           |
 *   +-------------------------------+
 *   |       KSWITCH_FRAME           |  <- Thread->KernelStack
 *   +-------------------------------+
 */

typedef struct _KUINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KUINIT_FRAME, *PKUINIT_FRAME;

typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

VOID
NTAPI
KiThreadStartup(VOID);

static
VOID
KiArm64SetupStartFrame(
    _Inout_ PKSTART_FRAME StartFrame,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext)
{
    RtlZeroMemory(StartFrame, sizeof(*StartFrame));
    StartFrame->StartRoutine = (ULONG64)StartRoutine;
    StartFrame->StartContext = (ULONG64)StartContext;
    StartFrame->SystemRoutine = (ULONG64)SystemRoutine;
    StartFrame->Return = 0;
}

VOID
NTAPI
KiInitializeContextThread(_Inout_ PKTHREAD Thread,
                          _In_ PKSYSTEM_ROUTINE SystemRoutine,
                          _In_ PKSTART_ROUTINE StartRoutine,
                          _In_opt_ PVOID StartContext,
                          _In_opt_ PCONTEXT ContextPointer)
{
    ULONG_PTR StackTop;
    PKSWITCH_FRAME SwitchFrame;
    PKSTART_FRAME StartFrame;

    ASSERT(Thread != NULL);
    ASSERT(SystemRoutine != NULL);

    StackTop = (ULONG_PTR)ALIGN_DOWN_POINTER_BY(Thread->InitialStack, 16);
    Thread->InitialStack = (PVOID)StackTop;

    if (ContextPointer != NULL)
    {
        PKUINIT_FRAME InitFrame;
        PKEXCEPTION_FRAME ExceptionFrame;
        PKTRAP_FRAME TrapFrame;

        {
            SIZE_T FrameSize = ALIGN_UP_BY(sizeof(*InitFrame), 16);
            InitFrame = (PKUINIT_FRAME)(StackTop - FrameSize);
        }
        RtlZeroMemory(InitFrame, sizeof(*InitFrame));

        SwitchFrame = &InitFrame->SwitchFrame;
        StartFrame = &InitFrame->StartFrame;
        ExceptionFrame = &InitFrame->ExceptionFrame;
        TrapFrame = &InitFrame->TrapFrame;

        KiArm64SetupStartFrame(StartFrame,
                               SystemRoutine,
                               StartRoutine,
                               StartContext);

        Thread->PreviousMode = UserMode;
        Thread->KernelStack = SwitchFrame;
        Thread->TrapFrame = TrapFrame;

        KeContextToTrapFrame(ContextPointer,
                             ExceptionFrame,
                             TrapFrame,
                             ContextPointer->ContextFlags | CONTEXT_CONTROL,
                             UserMode);

        TrapFrame->PreviousMode = UserMode;
        TrapFrame->ContextFromKFramesUnwound = FALSE;
        TrapFrame->DebugRegistersValid = FALSE;

        ExceptionFrame->Return = (ULONG64)KiThreadStartup;
    }
    else
    {
        PKKINIT_FRAME InitFrame;

        {
            SIZE_T FrameSize = ALIGN_UP_BY(sizeof(*InitFrame), 16);
            InitFrame = (PKKINIT_FRAME)(StackTop - FrameSize);
        }
        RtlZeroMemory(InitFrame, sizeof(*InitFrame));

        SwitchFrame = &InitFrame->SwitchFrame;
        StartFrame = &InitFrame->StartFrame;

        KiArm64SetupStartFrame(StartFrame,
                               SystemRoutine,
                               StartRoutine,
                               StartContext);

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = SwitchFrame;
        Thread->TrapFrame = NULL;
    }

    SwitchFrame->ReturnAddress = (ULONG64)KiThreadStartup;
    SwitchFrame->ApcBypass = APC_LEVEL;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    for (;;)
    {
        /*
         * Check for pending DPC work. On ARM64, also check DpcInterruptRequested
         * which is set by HalRequestSoftwareInterrupt when a DPC is queued.
         */
        if (Prcb->DpcData[0].DpcQueueDepth ||
            Prcb->TimerRequest ||
            Prcb->DeferredReadyListHead.Next ||
            Prcb->DpcInterruptRequested)
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            Prcb->DpcInterruptRequested = FALSE;
            KiRetireDpcList(Prcb);
            continue;
        }

        if (Prcb->NextThread)
        {
            PKTHREAD OldThread = Prcb->CurrentThread;
            PKTHREAD NewThread = Prcb->NextThread;

            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;

            KiSwapContext(APC_LEVEL, OldThread);
            continue;
        }

        KeStallExecutionProcessor(50);
        __asm__ __volatile__("wfe" ::: "memory");
    }
}

BOOLEAN
FASTCALL
KiSwapContextResume(
    _In_ KIRQL WaitIrql,
    _Inout_ PKTHREAD OldThread,
    _Inout_ PKTHREAD NewThread)
{
    PKPRCB Prcb;

    ASSERT(OldThread != NULL);
    ASSERT(NewThread != NULL);

    Prcb = KeGetCurrentPrcb();

    NewThread->ContextSwitches++;
    KeArm64CurrentThread = NewThread;

    OldThread->SwapBusy = FALSE;
    if (Prcb != NULL)
    {
        Prcb->KeContextSwitches++;
        Prcb->CurrentThread = NewThread;
    }

    /* Skip address space switch during bring-up to avoid TLB issues */
    /* TODO: Implement proper TTBR switch when user-mode is supported */

    if (NewThread->ApcState.KernelApcPending &&
        !NewThread->SpecialApcDisable &&
        !WaitIrql)
    {
        return TRUE;
    }

    if (NewThread->ApcState.KernelApcPending)
    {
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }

    return FALSE;
}

VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD NewThread, OldThread;
    KIRQL OldIrql;

    /*
     * ARM64 CRITICAL FIX: KiDispatchInterrupt must manage IRQL correctly.
     *
     * This function is called from two contexts:
     * 1. Hardware IRQ handler (interrupt.c) - already at HIGH_LEVEL
     * 2. KfLowerIrql (irql.c line 225) - at the NEW (lowered) IRQL
     *
     * In case (2), we need to raise IRQL to DISPATCH_LEVEL before processing DPCs,
     * then restore it afterwards. KiRetireDpcList expects to run at DISPATCH_LEVEL
     * and will ASSERT if IRQL is wrong.
     *
     * The bug was that when called from KfLowerIrql, IRQL had already been lowered
     * to PASSIVE or APC level, then we called KiRetireDpcList which processes DPCs.
     * DPCs execute at DISPATCH_LEVEL, so calling them at PASSIVE_LEVEL violates
     * IRQL semantics and causes ASSERTs to fire.
     *
     * IMPORTANT: We use direct IRQL manipulation here to avoid recursion.
     * We cannot call KfLowerIrql() to restore IRQL because KfLowerIrql() calls
     * KiDispatchInterrupt(), creating infinite recursion. Instead, we directly
     * set IRQL and apply IRQ masks.
     */

    /* Save current IRQL and raise to DISPATCH_LEVEL for DPC processing */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
    {
        KfRaiseIrql(DISPATCH_LEVEL);
    }

    _disable();

    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcList(Prcb);
    }

    _enable();

    /* Restore original IRQL if we raised it - use direct manipulation to avoid recursion */
    if (OldIrql < DISPATCH_LEVEL)
    {
        extern VOID KiApplyIrqMaskForIrqlTransition(KIRQL OldIrql, KIRQL NewIrql);
        extern VOID KiSetCurrentIrql(KIRQL Irql);

        KiApplyIrqMaskForIrqlTransition(DISPATCH_LEVEL, OldIrql);
        KiSetCurrentIrql(OldIrql);
    }

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if (Prcb->NextThread)
    {
        KiAcquirePrcbLock(Prcb);

        OldThread = Prcb->CurrentThread;
        NewThread = Prcb->NextThread;

        Prcb->NextThread = NULL;
        Prcb->CurrentThread = NewThread;

        NewThread->State = Running;
        OldThread->WaitReason = WrDispatchInt;

        KxQueueReadyThread(OldThread, Prcb);

        KiSwapContext(APC_LEVEL, OldThread);
    }
}
