/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         ARM64 Thread Scheduling and Queue Management
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* Priority bitmasks for fast priority scanning */
extern KAFFINITY KiIdleSummary;
extern ULONG KiReadySummary;

/* Maximum priorities (0-31, with 32 total levels) */
#define PRIORITY_LEVELS 32
#define LOW_PRIORITY 0
#define HIGH_PRIORITY 31
#define LOW_REALTIME_PRIORITY 16
#define HIGH_REALTIME_PRIORITY 31

/* Quantum constants for ARM64 */
#define THREAD_QUANTUM 6        /* Default quantum */
#define THREAD_QUANTUM_MIN 2    /* Minimum quantum */
/* THREAD_QUANTUM_MAX now defined in ke.h */

/* ARM64-specific constants */
#define ARM64_CACHE_LINE_SIZE 64

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Find the highest priority thread in ready queues
 *
 * Uses ARM64 optimized bit scanning to quickly locate the highest priority
 * thread ready to run. ARM64's CLZ (Count Leading Zeros) instruction provides
 * efficient priority scanning.
 *
 * @param Prcb - Processor control block
 * @return PKTHREAD - Highest priority ready thread, or NULL if none
 */
FORCEINLINE
PKTHREAD
KiFindHighestPriorityThread(
    IN PKPRCB Prcb)
{
    ULONG ReadySummary;
    ULONG HighestPriority;
    PLIST_ENTRY ListEntry;
    PKTHREAD Thread;

    /* Get the ready summary with proper memory barriers for ARM64 */
    ARM64_DMB_SY();
    ReadySummary = Prcb->ReadySummary;

    if (ReadySummary == 0)
    {
        /* No threads ready */
        return NULL;
    }

    /* Use ARM64 CLZ instruction for efficient bit scanning */
    /* CLZ counts leading zeros, so 31-CLZ gives us highest set bit */
    __asm__ volatile(
        "clz %w0, %w1\n\t"
        "mov %w0, #31\n\t"
        "sub %w0, %w0, %w0"
        : "=r" (HighestPriority)
        : "r" (ReadySummary)
        : "memory"
    );

    /* Alternative implementation if CLZ assembly fails */
    if (HighestPriority > 31)
    {
        /* Fallback: scan from highest priority down */
        for (HighestPriority = 31; HighestPriority != (ULONG)-1; HighestPriority--)
        {
            if (ReadySummary & (1UL << HighestPriority))
                break;
        }

        if (HighestPriority == (ULONG)-1)
            return NULL;
    }

    /* Get the ready list for this priority */
    ListEntry = Prcb->DispatcherReadyListHead[HighestPriority].Flink;

    if (ListEntry == &Prcb->DispatcherReadyListHead[HighestPriority])
    {
        /* List is empty, clear the bit and try again */
        InterlockedAnd((PLONG)&Prcb->ReadySummary, ~(1UL << HighestPriority));
        return KiFindHighestPriorityThread(Prcb);
    }

    /* Get the thread from the list entry */
    Thread = CONTAINING_RECORD(ListEntry, KTHREAD, WaitListEntry);

    return Thread;
}

/**
 * @brief Remove a thread from ready queues
 *
 * @param Thread - Thread to remove from ready queue
 * @param Prcb - Processor control block
 */
FORCEINLINE
VOID
KiRemoveThreadFromReadyQueue(
    IN PKTHREAD Thread,
    IN PKPRCB Prcb)
{
    ULONG Priority;
    PLIST_ENTRY ReadyListHead;

    ASSERT(Thread->State == Ready);
    ASSERT(Thread->Priority >= 0 && Thread->Priority <= HIGH_PRIORITY);

    Priority = Thread->Priority;
    ReadyListHead = &Prcb->DispatcherReadyListHead[Priority];

    /* Remove from the ready list */
    RemoveEntryList(&Thread->WaitListEntry);

    /* If list is now empty, clear the priority bit */
    if (IsListEmpty(ReadyListHead))
    {
        InterlockedAnd((PLONG)&Prcb->ReadySummary, ~(1UL << Priority));
    }

    /* Ensure memory ordering for ARM64 weak memory model */
    ARM64_DMB_SY();
}

/**
 * @brief Insert thread into ready queue at specified priority
 *
 * @param Thread - Thread to insert
 * @param Priority - Priority level (0-31)
 * @param Prcb - Processor control block
 * @param Head - TRUE to insert at head (preempt), FALSE for tail (fair)
 */
FORCEINLINE
VOID
KiInsertThreadInReadyQueue(
    IN PKTHREAD Thread,
    IN KPRIORITY Priority,
    IN PKPRCB Prcb,
    IN BOOLEAN Head)
{
    PLIST_ENTRY ReadyListHead;

    ASSERT(Priority >= 0 && Priority <= HIGH_PRIORITY);

    ReadyListHead = &Prcb->DispatcherReadyListHead[Priority];

    /* Set thread state and priority */
    Thread->State = Ready;
    Thread->Priority = (SCHAR)Priority;

    /* Insert into ready queue */
    if (Head)
    {
        /* Insert at head for immediate scheduling */
        InsertHeadList(ReadyListHead, &Thread->WaitListEntry);
    }
    else
    {
        /* Insert at tail for round-robin fairness */
        InsertTailList(ReadyListHead, &Thread->WaitListEntry);
    }

    /* Set the priority bit in summary */
    InterlockedOr((PLONG)&Prcb->ReadySummary, (1UL << Priority));

    /* Memory barrier to ensure visibility on ARM64 */
    ARM64_DMB_SY();
}

/**
 * @brief Calculate thread quantum based on priority and system load
 *
 * ARM64-specific quantum calculation that considers:
 * - Thread priority (higher priority gets longer quantum)
 * - Real-time vs variable priority classes
 * - System responsiveness requirements
 *
 * @param Thread - Thread to calculate quantum for
 * @return UCHAR - Quantum value
 */
FORCEINLINE
UCHAR
KiCalculateQuantum(
    IN PKTHREAD Thread)
{
    UCHAR Quantum;
    KPRIORITY Priority = Thread->Priority;

    /* Real-time threads get longer quantum */
    if (Priority >= LOW_REALTIME_PRIORITY)
    {
        /* Real-time priority: base quantum + priority boost */
        Quantum = THREAD_QUANTUM + (Priority - LOW_REALTIME_PRIORITY) / 2;
    }
    else
    {
        /* Variable priority: shorter quantum for better responsiveness */
        Quantum = THREAD_QUANTUM - (LOW_REALTIME_PRIORITY - Priority) / 4;
    }

    /* Clamp to valid range */
    if (Quantum < THREAD_QUANTUM_MIN)
        Quantum = THREAD_QUANTUM_MIN;
    else if (Quantum > THREAD_QUANTUM_MAX)
        Quantum = THREAD_QUANTUM_MAX;

    return Quantum;
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Make a thread ready for execution
 *
 * This is the main entry point for making threads ready. Handles priority
 * boosts, quantum assignment, and proper queue insertion.
 *
 * @param Thread - Thread to make ready
 */
VOID
FASTCALL
KiReadyThread(
    IN PKTHREAD Thread)
{
    PKPRCB Prcb;
    KIRQL OldIrql;
    KPRIORITY Priority;
    BOOLEAN Preempt = FALSE;

    ASSERT(Thread != NULL);
    ASSERT(Thread->State != Ready);

    /* Get current PRCB - for now assume UP, later extend for SMP */
    Prcb = KeGetCurrentPrcb();

    /* Raise IRQL to DISPATCH_LEVEL for scheduler operations */
    OldIrql = KeRaiseIrqlToDpcLevel();

    /* Acquire thread lock for safe manipulation */
    KiAcquireThreadLock(Thread);

    /* Determine target priority */
    Priority = Thread->Priority;

    /* Apply priority boosts if needed */
    if (Thread->AdjustReason == AdjustBoost)
    {
        KPRIORITY NewPriority;

        /* Only boost variable priority threads */
        if (Priority < LOW_REALTIME_PRIORITY && !Thread->DisableBoost)
        {
            /* Calculate boosted priority */
            NewPriority = min(Priority + Thread->AdjustIncrement,
                             LOW_REALTIME_PRIORITY - 1);

            if (NewPriority > Priority)
            {
                Thread->PriorityDecrement += (SCHAR)(NewPriority - Priority);
                Priority = NewPriority;
                Thread->Priority = (SCHAR)Priority;
                Preempt = TRUE;
            }
        }

        Thread->AdjustReason = AdjustNone;
    }

    /* Calculate new quantum */
    Thread->Quantum = KiCalculateQuantum(Thread);

    /* Insert into appropriate ready queue */
    if (Thread->State == DeferredReady)
    {
        /* Handle deferred ready threads */
        Thread->State = Ready;
        /* TODO: Add to deferred ready list for DPC-level processing */
    }
    else
    {
        /* Direct insertion into ready queue */
        KiInsertThreadInReadyQueue(Thread, Priority, Prcb, Preempt);
    }

    /* Check if we should preempt current thread */
    if (Prcb->CurrentThread &&
        Priority > Prcb->CurrentThread->Priority)
    {
        /* Request reschedule */
        Prcb->CurrentThread->Preempted = TRUE;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    /* Release locks and restore IRQL */
    KiReleaseThreadLock(Thread);
    KeLowerIrql(OldIrql);
}

/**
 * @brief Select the next thread to run
 *
 * ARM64 optimized thread selection with support for:
 * - Priority-based scheduling
 * - Round-robin within priority levels
 * - Idle thread selection
 * - Future SMP load balancing
 *
 * @param Prcb - Current processor control block
 * @return PKTHREAD - Next thread to run
 */
PKTHREAD
FASTCALL
KiSelectNextThread(
    IN PKPRCB Prcb)
{
    PKTHREAD NextThread;
    PKTHREAD CurrentThread;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    CurrentThread = Prcb->CurrentThread;

    /* First, try to find a ready thread */
    NextThread = KiFindHighestPriorityThread(Prcb);

    if (NextThread)
    {
        /* Remove from ready queue */
        KiRemoveThreadFromReadyQueue(NextThread, Prcb);
        NextThread->State = Running;
        return NextThread;
    }

    /* No ready threads, return idle thread */
    NextThread = Prcb->IdleThread;
    if (NextThread)
    {
        NextThread->State = Running;
        return NextThread;
    }

    /* Should never happen - we must have an idle thread */
    KeBugCheckEx(NO_MORE_IRP_STACK_LOCATIONS,
                 (ULONG_PTR)Prcb,
                 (ULONG_PTR)CurrentThread,
                 0, 0);

    /* Not reached */
    return NULL;
}

/**
 * @brief Handle quantum expiration for current thread
 *
 * Called by timer interrupt when thread's quantum expires.
 * Implements round-robin scheduling within same priority level.
 *
 * @param Prcb - Current processor control block
 * @return BOOLEAN - TRUE if reschedule needed
 */
BOOLEAN
FASTCALL
KiQuantumEnd(
    IN PKPRCB Prcb)
{
    PKTHREAD CurrentThread;
    KPRIORITY Priority;
    PLIST_ENTRY ReadyListHead;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    CurrentThread = Prcb->CurrentThread;
    if (!CurrentThread || CurrentThread == Prcb->IdleThread)
    {
        /* No quantum management for idle thread */
        return FALSE;
    }

    Priority = CurrentThread->Priority;
    ReadyListHead = &Prcb->DispatcherReadyListHead[Priority];

    /* Check if there are other threads at same priority */
    if (IsListEmpty(ReadyListHead))
    {
        /* No other threads at same priority, reset quantum and continue */
        CurrentThread->Quantum = KiCalculateQuantum(CurrentThread);
        return FALSE;
    }

    /* Apply priority decay for variable priority threads */
    if (Priority < LOW_REALTIME_PRIORITY && CurrentThread->PriorityDecrement > 0)
    {
        KPRIORITY NewPriority = Priority - 1;

        /* Don't decay below base priority */
        if (NewPriority >= CurrentThread->BasePriority)
        {
            CurrentThread->Priority = (SCHAR)NewPriority;
            CurrentThread->PriorityDecrement--;
        }
    }

    /* Reset quantum for next execution */
    CurrentThread->Quantum = KiCalculateQuantum(CurrentThread);

    /* Move to end of ready queue for round-robin */
    CurrentThread->State = Ready;
    KiInsertThreadInReadyQueue(CurrentThread, CurrentThread->Priority, Prcb, FALSE);

    /* Request reschedule */
    return TRUE;
}

/**
 * @brief Initialize processor ready queues
 *
 * Sets up the ready queue data structures for a processor.
 * Called during processor initialization.
 *
 * @param Prcb - Processor control block to initialize
 */
VOID
FASTCALL
KiInitializeReadyQueues(
    IN PKPRCB Prcb)
{
    ULONG Priority;

    DPRINT("KiInitializeReadyQueues: Initializing ARM64 ready queues for PRCB %p\n", Prcb);

    /* Initialize all 32 priority levels */
    for (Priority = 0; Priority < PRIORITY_LEVELS; Priority++)
    {
        InitializeListHead(&Prcb->DispatcherReadyListHead[Priority]);
    }

    /* Clear ready summary */
    Prcb->ReadySummary = 0;
    Prcb->QueueIndex = 0;

    /* Initialize deferred ready list */
    Prcb->DeferredReadyListHead.Next = NULL;

    /* Ensure ARM64 memory ordering */
    ARM64_DSB_SY();
    ARM64_ISB();

    DPRINT("KiInitializeReadyQueues: ARM64 ready queues initialized\n");
}

/**
 * @brief Process deferred ready threads at DPC level
 *
 * ARM64-specific implementation that processes threads marked as DeferredReady.
 * This is called at DPC level to avoid holding locks too long.
 *
 * @param Prcb - Current processor control block
 */
VOID
FASTCALL
KiProcessDeferredReadyList(
    IN PKPRCB Prcb)
{
    PSINGLE_LIST_ENTRY ListEntry;
    PKTHREAD Thread;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Check if deferred list has entries */
    if (Prcb->DeferredReadyListHead.Next == NULL)
    {
        return;
    }

    /* Process all deferred ready threads */
    while ((ListEntry = PopEntryList(&Prcb->DeferredReadyListHead)) != NULL)
    {
        /* Get thread from list entry */
        Thread = CONTAINING_RECORD(ListEntry, KTHREAD, SwapListEntry);

        ASSERT(Thread->State == DeferredReady);

        /* Make thread ready */
        KiReadyThread(Thread);
    }

    /* Ensure ARM64 memory consistency */
    ARM64_DMB_SY();
}

/**
 * @brief Check if reschedule is needed
 *
 * ARM64-optimized check for determining if the scheduler should run.
 * Called from various points including interrupt handlers.
 *
 * @return BOOLEAN - TRUE if reschedule is needed
 */
BOOLEAN
FASTCALL
KiCheckForReschedule(VOID)
{
    PKPRCB Prcb;
    PKTHREAD CurrentThread;
    ULONG HighestPriority;

    Prcb = KeGetCurrentPrcb();
    CurrentThread = Prcb->CurrentThread;

    /* Always reschedule if requested */
    if (CurrentThread && CurrentThread->Preempted)
    {
        return TRUE;
    }

    /* Check if there are higher priority threads ready */
    if (Prcb->ReadySummary == 0)
    {
        return FALSE;
    }

    /* Find highest ready priority using ARM64 CLZ */
    __asm__ volatile(
        "clz %w0, %w1\n\t"
        "mov %w0, #31\n\t"
        "sub %w0, %w0, %w0"
        : "=r" (HighestPriority)
        : "r" (Prcb->ReadySummary)
        : "memory"
    );

    /* Compare with current thread priority */
    if (CurrentThread && HighestPriority > CurrentThread->Priority)
    {
        return TRUE;
    }

    return FALSE;
}

/**
 * @brief ARM64 scheduler dispatcher
 *
 * Main scheduler entry point. Handles thread switching with proper
 * ARM64 context management, memory barriers, and cache coherency.
 *
 * @param OldThread - Currently running thread
 * @return PKTHREAD - New thread to run
 */
PKTHREAD
FASTCALL
KiDispatchThread(
    IN PKTHREAD OldThread)
{
    PKPRCB Prcb;
    PKTHREAD NewThread;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    Prcb = KeGetCurrentPrcb();

    /* Clear preemption flag */
    if (OldThread)
    {
        OldThread->Preempted = FALSE;
    }

    /* Select next thread to run */
    NewThread = KiSelectNextThread(Prcb);

    if (NewThread == OldThread)
    {
        /* Same thread continues running */
        return NewThread;
    }

    /* Update PRCB thread pointers */
    Prcb->CurrentThread = NewThread;

    /* Update context switch count for statistics */
    Prcb->KeContextSwitches++;

    /* Ensure ARM64 memory ordering before context switch */
    ARM64_DSB_SY();

    DPRINT("KiDispatchThread: ARM64 switching from %p to %p\n", OldThread, NewThread);

    return NewThread;
}

/**
 * @brief Set thread priority with ARM64 optimizations
 *
 * @param Thread - Thread to modify
 * @param Priority - New priority (0-31)
 * @param Boost - TRUE if this is a temporary boost
 */
VOID
FASTCALL
KiSetPriority(
    IN PKTHREAD Thread,
    IN KPRIORITY Priority,
    IN BOOLEAN Boost)
{
    PKPRCB Prcb;
    KIRQL OldIrql;
    KPRIORITY OldPriority;

    ASSERT(Priority >= LOW_PRIORITY && Priority <= HIGH_PRIORITY);
    ASSERT(Thread != NULL);

    Prcb = KeGetCurrentPrcb();
    OldIrql = KeRaiseIrqlToDpcLevel();
    KiAcquireThreadLock(Thread);

    OldPriority = Thread->Priority;

    if (Priority == OldPriority)
    {
        goto Exit;
    }

    /* Remove from current ready queue if ready */
    if (Thread->State == Ready)
    {
        KiRemoveThreadFromReadyQueue(Thread, Prcb);
    }

    /* Set new priority */
    Thread->Priority = (SCHAR)Priority;

    /* Handle priority boost tracking */
    if (Boost && Priority > Thread->BasePriority)
    {
        Thread->PriorityDecrement = (SCHAR)(Priority - Thread->BasePriority);
    }

    /* Re-insert into ready queue if ready */
    if (Thread->State == Ready)
    {
        KiInsertThreadInReadyQueue(Thread, Priority, Prcb, Priority > OldPriority);
    }

    /* Check for preemption */
    if (Thread->State == Running || Thread->State == Ready)
    {
        if (Priority > Prcb->CurrentThread->Priority)
        {
            Prcb->CurrentThread->Preempted = TRUE;
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
    }

Exit:
    KiReleaseThreadLock(Thread);
    KeLowerIrql(OldIrql);
}