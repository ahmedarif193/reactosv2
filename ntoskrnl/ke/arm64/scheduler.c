/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         ARM64 Thread Scheduler Support Functions
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* Scheduler statistics */
ULONG KiSchedulerStatistics[32] = {0}; /* Per-priority scheduling counts */
LARGE_INTEGER KiTotalContextSwitches = {0};

/* Thread lock for SMP safety */
KSPIN_LOCK KiThreadLock;

/* Priority boost settings */
#define PRIORITY_BOOST_MAXIMUM 15
#define PRIORITY_BOOST_IO 8
#define PRIORITY_BOOST_CPU 2

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Apply CPU boost to thread after CPU-intensive operation
 *
 * @param Thread - Thread to boost
 * @param Increment - Boost amount
 */
FORCEINLINE
VOID
KiApplyCpuBoost(
    IN PKTHREAD Thread,
    IN KPRIORITY Increment)
{
    KPRIORITY NewPriority;

    if (Thread->DisableBoost || Thread->Priority >= LOW_REALTIME_PRIORITY)
        return;

    NewPriority = min(Thread->Priority + Increment, LOW_REALTIME_PRIORITY - 1);
    if (NewPriority > Thread->Priority)
    {
        Thread->PriorityDecrement = (SCHAR)(NewPriority - Thread->BasePriority);
        Thread->Priority = (SCHAR)NewPriority;
        Thread->AdjustReason = AdjustBoost;
    }
}

/**
 * @brief Apply I/O completion boost to thread
 *
 * @param Thread - Thread completing I/O
 * @param IoBoost - I/O boost amount
 */
FORCEINLINE
VOID
KiApplyIoBoost(
    IN PKTHREAD Thread,
    IN KPRIORITY IoBoost)
{
    KPRIORITY NewPriority;

    if (Thread->DisableBoost || Thread->Priority >= LOW_REALTIME_PRIORITY)
        return;

    NewPriority = min(Thread->Priority + IoBoost, LOW_REALTIME_PRIORITY - 1);
    if (NewPriority > Thread->Priority)
    {
        Thread->PriorityDecrement = (SCHAR)(NewPriority - Thread->BasePriority);
        Thread->Priority = (SCHAR)NewPriority;
        Thread->AdjustReason = AdjustBoost;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Initialize ARM64 scheduler subsystem
 *
 * Called during kernel initialization to set up scheduler data structures
 * and ARM64-specific optimizations.
 */
VOID
NTAPI
KiInitializeScheduler(VOID)
{
    ULONG i;

    DPRINT("KiInitializeScheduler: Initializing ARM64 scheduler\n");

    /* Initialize thread lock for SMP support */
    KeInitializeSpinLock(&KiThreadLock);

    /* Clear scheduler statistics */
    for (i = 0; i < 32; i++)
    {
        KiSchedulerStatistics[i] = 0;
    }

    KiTotalContextSwitches.QuadPart = 0;

    /* ARM64-specific scheduler initialization */
    /* Set up any ARM64 performance counters for scheduler profiling */
    /* TODO: Configure ARM64 PMU for scheduler metrics */

    DPRINT("KiInitializeScheduler: ARM64 scheduler initialized\n");
}

/**
 * @brief Acquire thread lock for safe manipulation
 *
 * ARM64-specific implementation using proper memory barriers
 *
 * @param Thread - Thread to lock
 */
VOID
FASTCALL
KiAcquireThreadLock(
    IN PKTHREAD Thread)
{
    KIRQL OldIrql;

    /* For UP builds, just disable interrupts */
    /* For SMP builds, acquire the thread's lock */
#ifdef CONFIG_SMP
    /* TODO: Implement per-thread locking for SMP */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&KiThreadLock);
    KeGetCurrentPrcb()->ThreadLockIrql = OldIrql;
#else
    OldIrql = KeRaiseIrql(SYNCH_LEVEL);
    KeGetCurrentPrcb()->ThreadLockIrql = OldIrql;
#endif

    /* ARM64 memory barrier to ensure lock acquisition visibility */
    ARM64_DMB_SY();
}

/**
 * @brief Release thread lock
 *
 * @param Thread - Thread to unlock
 */
VOID
FASTCALL
KiReleaseThreadLock(
    IN PKTHREAD Thread)
{
    KIRQL OldIrql = KeGetCurrentPrcb()->ThreadLockIrql;

    /* Ensure memory ordering before releasing lock */
    ARM64_DMB_SY();

#ifdef CONFIG_SMP
    /* TODO: Release per-thread lock for SMP */
    KeReleaseSpinLock(&KiThreadLock, OldIrql);
#else
    KeLowerIrql(OldIrql);
#endif
}

/**
 * @brief Make a deferred ready thread ready immediately
 *
 * Called from DPC level to convert deferred ready threads to ready state
 *
 * @param Thread - Thread in DeferredReady state
 */
VOID
FASTCALL
KiDeferredReadyThread(
    IN PKTHREAD Thread)
{
    ASSERT(Thread->State == DeferredReady);
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Apply any pending boosts */
    if (Thread->AdjustReason != AdjustNone)
    {
        switch (Thread->AdjustReason)
        {
        case AdjustBoost:
            /* Priority boost already calculated */
            break;

        case AdjustUnwait:
            /* Apply I/O completion boost */
            KiApplyIoBoost(Thread, PRIORITY_BOOST_IO);
            break;

        default:
            Thread->AdjustReason = AdjustNone;
            break;
        }
    }

    /* Make thread ready */
    KiReadyThread(Thread);
}

/**
 * @brief Boost thread priority for I/O completion
 *
 * @param Thread - Thread completing I/O operation
 * @param Increment - Boost increment
 */
VOID
FASTCALL
KiBoostPriorityThread(
    IN PKTHREAD Thread,
    IN KPRIORITY Increment)
{
    PKPRCB Prcb;
    KIRQL OldIrql;

    if (!Thread || Thread->DisableBoost || Thread->Priority >= LOW_REALTIME_PRIORITY)
        return;

    Prcb = KeGetCurrentPrcb();
    OldIrql = KeRaiseIrqlToDpcLevel();
    KiAcquireThreadLock(Thread);

    /* Apply boost */
    Thread->AdjustIncrement = (SCHAR)min(Increment, PRIORITY_BOOST_MAXIMUM);
    Thread->AdjustReason = AdjustBoost;

    /* If thread is waiting, it will get the boost when it becomes ready */
    if (Thread->State == Ready || Thread->State == Running)
    {
        KiApplyCpuBoost(Thread, Thread->AdjustIncrement);
    }

    KiReleaseThreadLock(Thread);
    KeLowerIrql(OldIrql);
}

/**
 * @brief Handle thread preemption request
 *
 * ARM64-specific preemption handling with proper context save/restore
 *
 * @param CurrentThread - Thread being preempted
 * @param NewThread - Thread to switch to
 * @return BOOLEAN - TRUE if context switch occurred
 */
BOOLEAN
FASTCALL
KiPreemptThread(
    IN PKTHREAD CurrentThread,
    IN PKTHREAD NewThread)
{
    PKPRCB Prcb;

    if (!CurrentThread || !NewThread || CurrentThread == NewThread)
        return FALSE;

    Prcb = KeGetCurrentPrcb();

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Update scheduler statistics */
    InterlockedIncrement((PLONG)&KiSchedulerStatistics[NewThread->Priority]);
    InterlockedIncrement64(&KiTotalContextSwitches.QuadPart);

    /* ARM64: Ensure cache coherency before context switch */
    ARM64_DSB_SY();

    /* Update PRCB current thread */
    Prcb->CurrentThread = NewThread;
    Prcb->NextThread = NULL;

    /* Call the low-level ARM64 context switch routine */
    /* This will save current context and restore new thread context */
    KiSwapContext(CurrentThread, NewThread);

    /* ARM64: Ensure instruction coherency after context switch */
    ARM64_ISB();

    return TRUE;
}

/**
 * @brief Handle idle thread scheduling
 *
 * Called when no other threads are ready to run.
 * Implements ARM64 power-saving features.
 *
 * @param Prcb - Current processor control block
 * @return PKTHREAD - Always returns idle thread
 */
PKTHREAD
FASTCALL
KiIdleSchedule(
    IN PKPRCB Prcb)
{
    PKTHREAD IdleThread = Prcb->IdleThread;

    ASSERT(IdleThread != NULL);

    /* ARM64 power management: Enable power-saving features */
    /* TODO: Implement ARM64 CPU power states (C-states) */

    /* Mark processor as idle for SMP load balancing */
    InterlockedOr((PLONG)&KiIdleSummary, AFFINITY_MASK(Prcb->Number));

    /* Update idle thread state */
    IdleThread->State = Running;

    return IdleThread;
}

/**
 * @brief Calculate thread affinity for SMP systems
 *
 * @param Thread - Thread to calculate affinity for
 * @return KAFFINITY - Calculated processor affinity
 */
KAFFINITY
FASTCALL
KiCalculateThreadAffinity(
    IN PKTHREAD Thread)
{
    KAFFINITY ProcessAffinity;
    KAFFINITY ThreadAffinity;

    /* Get process affinity */
    ProcessAffinity = Thread->ApcState.Process->Affinity;

    /* Start with thread's affinity */
    ThreadAffinity = Thread->Affinity;

    /* Mask with process affinity */
    ThreadAffinity &= ProcessAffinity;

    /* Ensure at least one processor is available */
    if (ThreadAffinity == 0)
    {
        ThreadAffinity = ProcessAffinity;
    }

    /* Mask with active processors */
    ThreadAffinity &= KeActiveProcessors;

    return ThreadAffinity;
}

/**
 * @brief Set thread affinity
 *
 * @param Thread - Thread to set affinity for
 * @param Affinity - New processor affinity mask
 * @return KAFFINITY - Previous affinity
 */
KAFFINITY
FASTCALL
KiSetAffinityThread(
    IN PKTHREAD Thread,
    IN KAFFINITY Affinity)
{
    KAFFINITY OldAffinity;
    PKPRCB Prcb;
    KIRQL OldIrql;

    Prcb = KeGetCurrentPrcb();
    OldIrql = KeRaiseIrqlToDpcLevel();
    KiAcquireThreadLock(Thread);

    /* Save old affinity */
    OldAffinity = Thread->Affinity;

    /* Set new affinity */
    Thread->Affinity = Affinity & KeActiveProcessors;

    /* Ensure at least one processor */
    if (Thread->Affinity == 0)
    {
        Thread->Affinity = AFFINITY_MASK(0); /* Default to processor 0 */
    }

    /* If thread is running on incompatible processor, reschedule */
    if (Thread->State == Running &&
        !(Thread->Affinity & AFFINITY_MASK(Prcb->Number)))
    {
        Thread->Preempted = TRUE;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    KiReleaseThreadLock(Thread);
    KeLowerIrql(OldIrql);

    return OldAffinity;
}

/**
 * @brief Update thread quantum after I/O completion
 *
 * @param Thread - Thread that completed I/O
 * @param IoTime - Time spent in I/O wait
 */
VOID
FASTCALL
KiUpdateQuantumAfterIo(
    IN PKTHREAD Thread,
    IN ULONG IoTime)
{
    /* Threads that waited for I/O get bonus quantum */
    if (IoTime > 0)
    {
        UCHAR BonusQuantum = (UCHAR)min(IoTime / 1000, 3); /* Up to 3 extra ticks */
        Thread->Quantum += BonusQuantum;

        if (Thread->Quantum > THREAD_QUANTUM_MAX)
            Thread->Quantum = THREAD_QUANTUM_MAX;
    }
}

/**
 * @brief ARM64 scheduler main dispatch interrupt handler
 *
 * Called by the dispatcher interrupt to handle thread scheduling decisions.
 * This is the main entry point from the interrupt system.
 */
VOID
FASTCALL
KiDispatchInterruptHandler(VOID)
{
    PKPRCB Prcb;
    PKTHREAD OldThread, NewThread;
    KIRQL OldIrql;

    /* We should be at DISPATCH_LEVEL */
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Prcb = KeGetCurrentPrcb();
    OldThread = Prcb->CurrentThread;

    /* Process any deferred ready threads first */
    if (Prcb->DeferredReadyListHead.Next != NULL)
    {
        KiProcessDeferredReadyList(Prcb);
    }

    /* Select next thread to run */
    NewThread = KiDispatchThread(OldThread);

    if (NewThread != OldThread)
    {
        /* Perform context switch */
        KiPreemptThread(OldThread, NewThread);
    }
}

/**
 * @brief Get scheduler statistics for debugging
 *
 * @param Statistics - Buffer to receive statistics
 * @param Size - Size of statistics buffer
 * @return NTSTATUS - Status code
 */
NTSTATUS
NTAPI
KiGetSchedulerStatistics(
    OUT PVOID Statistics,
    IN ULONG Size)
{
    if (Size < sizeof(KiSchedulerStatistics))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Statistics, KiSchedulerStatistics, sizeof(KiSchedulerStatistics));
    return STATUS_SUCCESS;
}