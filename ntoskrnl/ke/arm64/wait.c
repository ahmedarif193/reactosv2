/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Wait/Synchronization Primitives
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* Wait reasons for debugging */
#define WAIT_REASON_EXECUTIVE       0
#define WAIT_REASON_FREEPAGE        1
#define WAIT_REASON_PAGEIN          2
#define WAIT_REASON_POOLALLOC       3
#define WAIT_REASON_DELAYEXEC       4
#define WAIT_REASON_SUSPENDED       5

/* Maximum wait objects per wait call */
#define MAXIMUM_WAIT_OBJECTS        64

/* Wait block flags */
#define WAIT_BLOCK_ACTIVE           0x01
#define WAIT_BLOCK_ALERTABLE        0x02
#define WAIT_BLOCK_ALL_OBJECTS      0x04

/* GLOBALS ********************************************************************/

KSPIN_LOCK DispatcherLock;
LIST_ENTRY DispatcherReadyListHead[MAXIMUM_PRIORITY];
ULONG KiReadySummary = 0;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Check if wait can be satisfied
 *
 * Checks if the wait condition for an object has been met.
 */
static
BOOLEAN
FASTCALL
KiCheckWaitSatisfaction(
    IN PKWAIT_BLOCK WaitBlock,
    IN PKTHREAD Thread)
{
    DISPATCHER_HEADER *Header;
    LONG SignalState;

    Header = WaitBlock->Object;
    SignalState = Header->SignalState;

    DPRINT("Checking wait satisfaction for object %p (type=%d, signal=%ld)\n",
           Header, Header->Type, SignalState);

    /* Check based on object type */
    switch (Header->Type)
    {
        case EventNotificationObject:
            /* Notification events stay signaled */
            return (SignalState > 0);

        case EventSynchronizationObject:
            /* Synchronization events auto-reset */
            if (SignalState > 0)
            {
                Header->SignalState = 0;
                return TRUE;
            }
            return FALSE;

        case SemaphoreObject:
            /* Semaphores decrement count */
            if (SignalState > 0)
            {
                Header->SignalState--;
                return TRUE;
            }
            return FALSE;

        case MutantObject:
            /* Mutexes have special ownership rules */
            if (SignalState > 0 ||
                ((PKMUTANT)Header)->OwnerThread == Thread)
            {
                ((PKMUTANT)Header)->OwnerThread = Thread;
                Header->SignalState--;
                return TRUE;
            }
            return FALSE;

        case ProcessObject:
        case ThreadObject:
            /* Process/Thread objects signal on termination */
            return (SignalState > 0);

        default:
            DPRINT1("Unknown dispatcher object type: %d\n", Header->Type);
            return FALSE;
    }
}

/*
 * @brief Insert thread into wait queue
 */
static
VOID
FASTCALL
KiInsertWaitList(
    IN PKTHREAD Thread)
{
    PLIST_ENTRY WaitEntry;
    PKWAIT_BLOCK WaitBlock;
    DISPATCHER_HEADER *Object;

    /* Insert thread into each object's wait list */
    WaitBlock = Thread->WaitBlockList;
    while (WaitBlock)
    {
        Object = WaitBlock->Object;
        InsertTailList(&Object->WaitListHead, &WaitBlock->WaitListEntry);
        WaitBlock = WaitBlock->NextWaitBlock;
    }

    DPRINT("Thread %p inserted into wait lists\n", Thread);
}

/*
 * @brief Remove thread from wait queues
 */
static
VOID
FASTCALL
KiRemoveWaitList(
    IN PKTHREAD Thread)
{
    PKWAIT_BLOCK WaitBlock;

    /* Remove from all wait lists */
    WaitBlock = Thread->WaitBlockList;
    while (WaitBlock)
    {
        RemoveEntryList(&WaitBlock->WaitListEntry);
        WaitBlock = WaitBlock->NextWaitBlock;
    }

    DPRINT("Thread %p removed from wait lists\n", Thread);
}

/*
 * @brief Wake thread from wait state
 */
static
VOID
FASTCALL
KiWakeThread(
    IN PKTHREAD Thread,
    IN NTSTATUS WaitStatus,
    IN KPRIORITY Increment)
{
    KIRQL OldIrql;

    DPRINT("Waking thread %p with status 0x%08lx\n", Thread, WaitStatus);

    /* Set wait status */
    Thread->WaitStatus = WaitStatus;

    /* Remove from wait lists */
    KiRemoveWaitList(Thread);

    /* Clear wait state */
    Thread->State = Ready;
    Thread->WaitBlockList = NULL;
    Thread->WaitTime = 0;
    Thread->WaitReason = 0;

    /* Apply priority boost if specified */
    if (Increment != 0)
    {
        Thread->Priority = min(Thread->Priority + Increment, HIGH_PRIORITY);
    }

    /* Insert into ready queue */
    KiReadyThread(Thread);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize wait system
 * @implemented
 */
VOID
NTAPI
KiInitializeWaitSystem(VOID)
{
    ULONG i;

    DPRINT("Initializing ARM64 wait system\n");

    /* Initialize dispatcher database lock */
    KeInitializeSpinLock(&DispatcherLock);

    /* Initialize ready queues */
    for (i = 0; i < MAXIMUM_PRIORITY; i++)
    {
        InitializeListHead(&DispatcherReadyListHead[i]);
    }

    /* Clear ready summary */
    KiReadySummary = 0;

    DPRINT("ARM64 wait system initialized\n");
}

/*
 * @brief Wait for single object
 * @implemented
 */
NTSTATUS
NTAPI
KeWaitForSingleObject(
    IN PVOID Object,
    IN KWAIT_REASON WaitReason,
    IN KPROCESSOR_MODE WaitMode,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL)
{
    PKTHREAD Thread;
    DISPATCHER_HEADER *Header;
    KWAIT_BLOCK WaitBlock;
    KIRQL OldIrql;
    NTSTATUS Status;
    LARGE_INTEGER DueTime;

    Thread = KeGetCurrentThread();
    Header = (DISPATCHER_HEADER *)Object;

    DPRINT("Thread %p waiting for object %p (type=%d)\n",
           Thread, Object, Header->Type);

    /* Raise IRQL to DISPATCH_LEVEL */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Acquire dispatcher lock */
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    /* Check if object is already signaled */
    if (KiCheckWaitSatisfaction(&WaitBlock, Thread))
    {
        /* Object satisfied immediately */
        KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
        KeLowerIrql(OldIrql);
        DPRINT("Wait satisfied immediately\n");
        return STATUS_SUCCESS;
    }

    /* Check for zero timeout */
    if (Timeout && Timeout->QuadPart == 0)
    {
        /* Immediate timeout */
        KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
        KeLowerIrql(OldIrql);
        DPRINT("Wait timed out immediately\n");
        return STATUS_TIMEOUT;
    }

    /* Setup wait block */
    WaitBlock.Object = Header;
    WaitBlock.Thread = Thread;
    WaitBlock.WaitKey = STATUS_SUCCESS;
    WaitBlock.WaitType = WaitAny;
    WaitBlock.NextWaitBlock = NULL;

    /* Setup thread for wait */
    Thread->State = Waiting;
    Thread->WaitBlockList = &WaitBlock;
    Thread->WaitStatus = STATUS_SUCCESS;
    Thread->WaitMode = WaitMode;
    Thread->WaitReason = WaitReason;
    Thread->Alertable = Alertable;
    Thread->WaitTime = KeQueryInterruptTime();

    /* Insert into object's wait list */
    InsertTailList(&Header->WaitListHead, &WaitBlock.WaitListEntry);

    /* Setup timeout timer if specified */
    if (Timeout)
    {
        DueTime = *Timeout;
        /* TODO: Setup and start timeout timer
         * KeSetTimer(&Thread->Timer, DueTime, NULL); */
    }

    /* Release dispatcher lock and lower IRQL */
    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
    KeLowerIrql(OldIrql);

    /* Switch to another thread */
    KiSwapContext();

    /* Thread resumes here after being woken */
    Status = Thread->WaitStatus;

    DPRINT("Thread %p wait completed with status 0x%08lx\n", Thread, Status);

    return Status;
}

/*
 * @brief Wait for multiple objects
 * @implemented
 */
NTSTATUS
NTAPI
KeWaitForMultipleObjects(
    IN ULONG Count,
    IN PVOID Object[],
    IN WAIT_TYPE WaitType,
    IN KWAIT_REASON WaitReason,
    IN KPROCESSOR_MODE WaitMode,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL,
    IN PKWAIT_BLOCK WaitBlockArray OPTIONAL)
{
    PKTHREAD Thread;
    PKWAIT_BLOCK WaitBlock, FirstBlock;
    DISPATCHER_HEADER *Header;
    KIRQL OldIrql;
    NTSTATUS Status;
    ULONG i;
    BOOLEAN AllSatisfied, AnySatisfied;

    /* Validate parameters */
    if (Count == 0 || Count > MAXIMUM_WAIT_OBJECTS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Thread = KeGetCurrentThread();

    DPRINT("Thread %p waiting for %lu objects (type=%s)\n",
           Thread, Count, WaitType == WaitAll ? "ALL" : "ANY");

    /* Allocate wait blocks if not provided */
    if (!WaitBlockArray)
    {
        if (Count <= THREAD_WAIT_OBJECTS)
        {
            WaitBlockArray = Thread->WaitBlock;
        }
        else
        {
            /* TODO: Allocate wait blocks for large count */
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Raise IRQL and acquire lock */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    /* Initialize wait blocks */
    FirstBlock = &WaitBlockArray[0];
    for (i = 0; i < Count; i++)
    {
        WaitBlock = &WaitBlockArray[i];
        Header = (DISPATCHER_HEADER *)Object[i];

        WaitBlock->Object = Header;
        WaitBlock->Thread = Thread;
        WaitBlock->WaitKey = (USHORT)i;
        WaitBlock->WaitType = (USHORT)WaitType;
        WaitBlock->NextWaitBlock = (i < Count - 1) ? &WaitBlockArray[i + 1] : FirstBlock;
    }

    /* Check if wait can be satisfied immediately */
    AllSatisfied = TRUE;
    AnySatisfied = FALSE;

    for (i = 0; i < Count; i++)
    {
        WaitBlock = &WaitBlockArray[i];
        if (KiCheckWaitSatisfaction(WaitBlock, Thread))
        {
            AnySatisfied = TRUE;
            if (WaitType == WaitAny)
            {
                /* For WaitAny, one satisfied object is enough */
                KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
                KeLowerIrql(OldIrql);
                DPRINT("Wait satisfied for object %lu\n", i);
                return i;  /* Return index of satisfied object */
            }
        }
        else
        {
            AllSatisfied = FALSE;
        }
    }

    /* For WaitAll, check if all were satisfied */
    if (WaitType == WaitAll && AllSatisfied)
    {
        KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
        KeLowerIrql(OldIrql);
        DPRINT("All objects satisfied\n");
        return STATUS_SUCCESS;
    }

    /* Check for zero timeout */
    if (Timeout && Timeout->QuadPart == 0)
    {
        KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
        KeLowerIrql(OldIrql);
        return STATUS_TIMEOUT;
    }

    /* Setup thread for wait */
    Thread->State = Waiting;
    Thread->WaitBlockList = FirstBlock;
    Thread->WaitStatus = STATUS_SUCCESS;
    Thread->WaitMode = WaitMode;
    Thread->WaitReason = WaitReason;
    Thread->Alertable = Alertable;
    Thread->WaitTime = KeQueryInterruptTime();

    /* Insert into all wait lists */
    KiInsertWaitList(Thread);

    /* TODO: Setup timeout timer if needed */

    /* Release lock and switch context */
    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
    KeLowerIrql(OldIrql);

    KiSwapContext();

    /* Thread resumes here */
    Status = Thread->WaitStatus;

    DPRINT("Thread %p multi-wait completed with status 0x%08lx\n", Thread, Status);

    return Status;
}

/*
 * @brief Signal object and check waiters
 * @implemented
 */
LONG
NTAPI
KeSetEvent(
    IN PKEVENT Event,
    IN KPRIORITY Increment,
    IN BOOLEAN Wait)
{
    DISPATCHER_HEADER *Header;
    LONG OldState;
    PLIST_ENTRY WaitEntry;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitingThread;
    KIRQL OldIrql;
    BOOLEAN WakeThreads = FALSE;

    Header = &Event->Header;

    DPRINT("Setting event %p (type=%d)\n", Event, Header->Type);

    /* Acquire dispatcher lock */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    /* Save old state */
    OldState = Header->SignalState;

    /* Set event to signaled */
    Header->SignalState = 1;

    /* Check for waiting threads */
    if (!IsListEmpty(&Header->WaitListHead))
    {
        if (Header->Type == EventNotificationObject)
        {
            /* Notification event - wake all waiters */
            while (!IsListEmpty(&Header->WaitListHead))
            {
                WaitEntry = RemoveHeadList(&Header->WaitListHead);
                WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
                WaitingThread = WaitBlock->Thread;

                KiWakeThread(WaitingThread, WaitBlock->WaitKey, Increment);
            }
        }
        else
        {
            /* Synchronization event - wake one waiter and auto-reset */
            WaitEntry = RemoveHeadList(&Header->WaitListHead);
            WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
            WaitingThread = WaitBlock->Thread;

            Header->SignalState = 0;  /* Auto-reset */
            KiWakeThread(WaitingThread, WaitBlock->WaitKey, Increment);
        }
    }

    /* Handle wait after signal if requested */
    if (Wait)
    {
        /* TODO: Enter wait state for current thread */
    }

    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
    KeLowerIrql(OldIrql);

    DPRINT("Event %p signaled, old state was %ld\n", Event, OldState);

    return OldState;
}

/*
 * @brief Reset event to non-signaled
 * @implemented
 */
LONG
NTAPI
KeResetEvent(
    IN PKEVENT Event)
{
    DISPATCHER_HEADER *Header;
    LONG OldState;
    KIRQL OldIrql;

    Header = &Event->Header;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    OldState = Header->SignalState;
    Header->SignalState = 0;

    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
    KeLowerIrql(OldIrql);

    DPRINT("Event %p reset, old state was %ld\n", Event, OldState);

    return OldState;
}

/*
 * @brief Clear event (reset without returning old state)
 * @implemented
 */
VOID
NTAPI
KeClearEvent(
    IN PKEVENT Event)
{
    Event->Header.SignalState = 0;
}

/*
 * @brief Initialize event object
 * @implemented
 */
VOID
NTAPI
KeInitializeEvent(
    IN PKEVENT Event,
    IN EVENT_TYPE Type,
    IN BOOLEAN State)
{
    DPRINT("Initializing event %p (type=%d, state=%d)\n", Event, Type, State);

    /* Initialize dispatcher header */
    Event->Header.Type = Type;
    Event->Header.Size = sizeof(KEVENT) / sizeof(LONG);
    Event->Header.SignalState = State ? 1 : 0;
    InitializeListHead(&Event->Header.WaitListHead);
}

/*
 * @brief Delay thread execution
 * @implemented
 */
NTSTATUS
NTAPI
KeDelayExecutionThread(
    IN KPROCESSOR_MODE WaitMode,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Interval)
{
    PKTHREAD Thread;
    KTIMER Timer;
    NTSTATUS Status;

    Thread = KeGetCurrentThread();

    DPRINT("Thread %p delaying for %lld us\n", Thread, Interval->QuadPart / 10);

    /* Initialize and set timer */
    KeInitializeTimer(&Timer);
    KeSetTimer(&Timer, *Interval, NULL);

    /* Wait for timer */
    Status = KeWaitForSingleObject(&Timer,
                                   Executive,
                                   WaitMode,
                                   Alertable,
                                   NULL);

    /* Cancel timer if wait was interrupted */
    if (Status != STATUS_SUCCESS)
    {
        KeCancelTimer(&Timer);
    }

    return Status;
}

/*
 * @brief Ready thread for execution
 * @implemented
 */
VOID
FASTCALL
KiReadyThread(
    IN PKTHREAD Thread)
{
    KIRQL OldIrql;
    KPRIORITY Priority;

    DPRINT("Readying thread %p (priority=%d)\n", Thread, Thread->Priority);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    /* Set thread state */
    Thread->State = Ready;

    /* Get thread priority */
    Priority = Thread->Priority;

    /* Insert into ready queue */
    InsertTailList(&DispatcherReadyListHead[Priority], &Thread->WaitListEntry);

    /* Update ready summary */
    KiReadySummary |= (1 << Priority);

    /* Check if preemption needed */
    if (Priority > KeGetCurrentThread()->Priority)
    {
        /* TODO: Request dispatch interrupt */
        DPRINT("Preemption needed for higher priority thread\n");
    }

    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
    KeLowerIrql(OldIrql);
}

/*
 * @brief Swap thread context (placeholder)
 * @implemented
 */
VOID
FASTCALL
KiSwapContext(VOID)
{
    /* TODO: Implement actual context switching
     * This involves:
     * 1. Save current thread context
     * 2. Select next thread to run
     * 3. Load new thread context
     * 4. Switch page tables if needed
     * 5. Return to new thread
     */

    DPRINT("Context switch requested\n");

    /* For now, just return to simulate completed wait */
}

/* EOF */