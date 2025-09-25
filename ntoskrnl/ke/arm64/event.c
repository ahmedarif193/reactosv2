/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Event Dispatcher Object Implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64 specific intrinsics */
#include <internal/arm64/intrin_i.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 memory ordering for event operations */
#define ARM64_EVENT_ACQUIRE_BARRIER()  __dmb(ishld)
#define ARM64_EVENT_RELEASE_BARRIER()  __dmb(ishst)
#define ARM64_EVENT_FULL_BARRIER()     __dmb(ish)

/* Event signaling optimizations for ARM64 multi-core */
#define ARM64_EVENT_SIGNAL_BARRIER()   do { ARM64_EVENT_FULL_BARRIER(); __sev(); } while(0)

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeEvent(OUT PKEVENT Event,
                  IN EVENT_TYPE Type,
                  IN BOOLEAN State)
{
    ASSERT(Event != NULL);
    ASSERT((Type == NotificationEvent) || (Type == SynchronizationEvent));

    DPRINT("KeInitializeEvent: Event=%p, Type=%d, State=%d\n",
           Event, Type, State);

    /* Initialize the Dispatcher Header */
    Event->Header.Type = (Type == NotificationEvent) ? EventNotificationObject : EventSynchronizationObject;
    Event->Header.Size = sizeof(KEVENT) / sizeof(ULONG);
    Event->Header.SignalState = State ? 1 : 0;
    InitializeListHead(&(Event->Header.WaitListHead));

    /* ARM64: Ensure initialization is visible to all cores */
    ARM64_EVENT_FULL_BARRIER();

    DPRINT("KeInitializeEvent: Initialized Event=%p as %s event\n",
           Event, (Type == NotificationEvent) ? "Notification" : "Synchronization");
}

/*
 * @implemented
 */
VOID
NTAPI
KeClearEvent(IN PKEVENT Event)
{
    ASSERT_EVENT(Event);

    DPRINT("KeClearEvent: Event=%p\n", Event);

    /* ARM64: Use atomic store with release semantics */
    _InterlockedExchange(&Event->Header.SignalState, 0);

    /* ARM64: Memory barrier to ensure state change is visible */
    ARM64_EVENT_RELEASE_BARRIER();

    DPRINT("KeClearEvent: Event cleared\n");
}

/*
 * @implemented
 */
LONG
NTAPI
KeReadStateEvent(IN PKEVENT Event)
{
    LONG SignalState;

    ASSERT_EVENT(Event);

    DPRINT("KeReadStateEvent: Event=%p\n", Event);

    /* ARM64: Load with acquire semantics for proper ordering */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Read the signal state atomically */
    SignalState = Event->Header.SignalState;

    /* ARM64: Memory barrier to ensure read completes */
    ARM64_EVENT_ACQUIRE_BARRIER();

    DPRINT("KeReadStateEvent: SignalState=%ld\n", SignalState);

    return SignalState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeResetEvent(IN PKEVENT Event)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DPRINT("KeResetEvent: Event=%p\n", Event);

    /* ARM64: Memory barrier before critical section */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Lock the Dispatcher Database */
    OldIrql = KiAcquireDispatcherLock();

    /* ARM64: Additional barrier after acquiring lock for multi-core coherency */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Save the Previous State */
    PreviousState = Event->Header.SignalState;

    /* ARM64: Atomically reset the event */
    _InterlockedExchange(&Event->Header.SignalState, 0);

    /* ARM64: Memory barrier to ensure state change is visible */
    ARM64_EVENT_RELEASE_BARRIER();

    /* Release Dispatcher Database and return previous state */
    KiReleaseDispatcherLock(OldIrql);

    DPRINT("KeResetEvent: PreviousState=%ld\n", PreviousState);

    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeSetEvent(IN PKEVENT Event,
           IN KPRIORITY Increment,
           IN BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD Thread;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DPRINT("KeSetEvent: Event=%p, Increment=%d, Wait=%d\n",
           Event, Increment, Wait);

    /*
     * ARM64 Optimization: Check if this is a signaled notification event without an upcoming wait.
     * In this case, we can immediately return TRUE, without locking.
     */
    if ((Event->Header.Type == EventNotificationObject) &&
        (Event->Header.SignalState == 1) &&
        !(Wait))
    {
        /* ARM64: Memory barrier to ensure we read consistent state */
        ARM64_EVENT_ACQUIRE_BARRIER();

        /* Verify the state hasn't changed after the barrier */
        if (Event->Header.SignalState == 1)
        {
            DPRINT("KeSetEvent: Fast path - notification event already signaled\n");
            return TRUE;
        }
        /* State changed, fall through to locked path */
    }

    /* ARM64: Memory barrier before critical section */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Lock the Dispatcher Database */
    OldIrql = KiAcquireDispatcherLock();

    /* ARM64: Additional barrier after acquiring lock for multi-core coherency */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Save the Previous State */
    PreviousState = Event->Header.SignalState;

    /* ARM64: Atomically set the Event to Signaled */
    _InterlockedExchange(&Event->Header.SignalState, 1);

    /* ARM64: Full memory barrier to ensure state change is visible, then signal other cores */
    ARM64_EVENT_SIGNAL_BARRIER();

    /* Check if the event just became signaled now, and it has waiters */
    if (!(PreviousState) && !(IsListEmpty(&Event->Header.WaitListHead)))
    {
        DPRINT("KeSetEvent: Waking waiting threads\n");

        /* Check the type of event */
        if (Event->Header.Type == EventNotificationObject)
        {
            /* Notification event: wake all waiters */
            KxUnwaitThread(&Event->Header, Increment);
        }
        else
        {
            /* Synchronization event: wake one waiter and auto-reset */
            KxUnwaitThreadForEvent(Event, Increment);
        }
    }

    /* Check what wait state was requested */
    if (!Wait)
    {
        /* ARM64: Release barrier before unlocking */
        ARM64_EVENT_RELEASE_BARRIER();

        /* Wait not requested, release Dispatcher Database and return */
        KiReleaseDispatcherLock(OldIrql);
    }
    else
    {
        /* Set wait state - the lock will be released by the wait operation */
        Thread = KeGetCurrentThread();
        Thread->WaitNext = TRUE;
        Thread->WaitIrql = OldIrql;

        /* ARM64: Memory barrier for wait state visibility */
        ARM64_EVENT_FULL_BARRIER();
    }

    DPRINT("KeSetEvent: Returning PreviousState=%ld\n", PreviousState);

    /* Return the previous State */
    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KePulseEvent(IN PKEVENT Event,
             IN KPRIORITY Increment,
             IN BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD Thread;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DPRINT("KePulseEvent: Event=%p, Increment=%d, Wait=%d\n",
           Event, Increment, Wait);

    /* ARM64: Memory barrier before critical section */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Lock the Dispatcher Database */
    OldIrql = KiAcquireDispatcherLock();

    /* ARM64: Additional barrier after acquiring lock for multi-core coherency */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Save the Old State */
    PreviousState = Event->Header.SignalState;

    /* Check if we are non-signaled and we have stuff in the Wait Queue */
    if (!PreviousState && !IsListEmpty(&Event->Header.WaitListHead))
    {
        /* ARM64: Atomically set the Event to Signaled */
        _InterlockedExchange(&Event->Header.SignalState, 1);

        /* ARM64: Full memory barrier to ensure state change is visible, then signal other cores */
        ARM64_EVENT_SIGNAL_BARRIER();

        DPRINT("KePulseEvent: Waking waiting threads\n");

        /* Wake the Event */
        KiWaitTest(&Event->Header, Increment);
    }

    /* ARM64: Atomically unsignal the event (pulse behavior) */
    _InterlockedExchange(&Event->Header.SignalState, 0);

    /* ARM64: Memory barrier to ensure pulse completion is visible */
    ARM64_EVENT_RELEASE_BARRIER();

    /* Check what wait state was requested */
    if (Wait == FALSE)
    {
        /* ARM64: Release barrier before unlocking */
        ARM64_EVENT_RELEASE_BARRIER();

        /* Wait not requested, release Dispatcher Database and return */
        KiReleaseDispatcherLock(OldIrql);
    }
    else
    {
        /* Set wait state - the lock will be released by the wait operation */
        Thread = KeGetCurrentThread();
        Thread->WaitNext = TRUE;
        Thread->WaitIrql = OldIrql;

        /* ARM64: Memory barrier for wait state visibility */
        ARM64_EVENT_FULL_BARRIER();
    }

    DPRINT("KePulseEvent: Returning PreviousState=%ld\n", PreviousState);

    /* Return the previous State */
    return PreviousState;
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetEventBoostPriority(IN PKEVENT Event,
                        IN PKTHREAD *WaitingThread OPTIONAL)
{
    KIRQL OldIrql;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread = KeGetCurrentThread(), WaitThread;

    ASSERT(Event->Header.Type == EventSynchronizationObject);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    DPRINT("KeSetEventBoostPriority: Event=%p, WaitingThread=%p\n",
           Event, WaitingThread);

    /* ARM64: Memory barrier before critical section */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Acquire Dispatcher Database Lock */
    OldIrql = KiAcquireDispatcherLock();

    /* ARM64: Additional barrier after acquiring lock for multi-core coherency */
    ARM64_EVENT_ACQUIRE_BARRIER();

    /* Check if the list is empty */
    if (IsListEmpty(&Event->Header.WaitListHead))
    {
        /* ARM64: Atomically set the Event to Signaled */
        _InterlockedExchange(&Event->Header.SignalState, 1);

        /* ARM64: Full memory barrier and signal cores */
        ARM64_EVENT_SIGNAL_BARRIER();

        DPRINT("KeSetEventBoostPriority: No waiters, event signaled\n");

        /* Release lock and return */
        KiReleaseDispatcherLock(OldIrql);
        return;
    }

    /* Get the first wait block */
    WaitBlock = CONTAINING_RECORD(Event->Header.WaitListHead.Flink,
                                  KWAIT_BLOCK,
                                  WaitListEntry);

    DPRINT("KeSetEventBoostPriority: WaitBlock=%p, WaitType=%d\n",
           WaitBlock, WaitBlock->WaitType);

    /* Check if this is a WaitAll */
    if (WaitBlock->WaitType == WaitAll)
    {
        /* ARM64: Atomically set the Event to Signaled */
        _InterlockedExchange(&Event->Header.SignalState, 1);

        /* ARM64: Full memory barrier and signal cores */
        ARM64_EVENT_SIGNAL_BARRIER();

        /* Unwait the thread and unsignal the event (synchronization event behavior) */
        KxUnwaitThreadForEvent(Event, EVENT_INCREMENT);
    }
    else
    {
        /* Get the waiting thread */
        WaitThread = WaitBlock->Thread;
        if (WaitingThread) *WaitingThread = WaitThread;

        DPRINT("KeSetEventBoostPriority: Boosting priority for thread %p\n", WaitThread);

        /* Calculate new priority */
        Thread->Priority = KiComputeNewPriority(Thread, 0);

        /* ARM64: Memory barrier before thread operations */
        ARM64_EVENT_ACQUIRE_BARRIER();

        /* Unlink the waiting thread */
        KiUnlinkThread(WaitThread, STATUS_SUCCESS);

        /* Request priority boosting */
        WaitThread->AdjustIncrement = Thread->Priority;
        WaitThread->AdjustReason = AdjustBoost;

        /* ARM64: Memory barrier before making thread ready */
        ARM64_EVENT_RELEASE_BARRIER();

        /* Ready the thread */
        KiReadyThread(WaitThread);

        /* ARM64: Signal other cores that thread state changed */
        __sev();
    }

    /* ARM64: Final barrier before releasing lock */
    ARM64_EVENT_RELEASE_BARRIER();

    /* Release the Dispatcher Database Lock */
    KiReleaseDispatcherLock(OldIrql);

    DPRINT("KeSetEventBoostPriority: Completed\n");
}

/*
 * @brief ARM64-specific event wait satisfaction logic
 *
 * This function handles the ARM64-specific aspects of event wait satisfaction.
 * Called from the generic wait system when an event needs to be processed.
 */
BOOLEAN
FASTCALL
KiArm64EventWaitSatisfaction(IN PKEVENT Event,
                             IN PKTHREAD Thread)
{
    LONG CurrentState;
    LONG NewState;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT(Thread != NULL);

    DPRINT("KiArm64EventWaitSatisfaction: Event=%p, Thread=%p, Type=%d\n",
           Event, Thread, Event->Header.Type);

    /* ARM64: Load current state with acquire semantics */
    ARM64_EVENT_ACQUIRE_BARRIER();
    CurrentState = Event->Header.SignalState;

    /* Check if event is signaled */
    if (CurrentState <= 0)
    {
        DPRINT("KiArm64EventWaitSatisfaction: Event not signaled (state=%ld)\n",
               CurrentState);
        return FALSE;
    }

    /* Handle based on event type */
    if (Event->Header.Type == EventNotificationObject)
    {
        /* Notification events stay signaled */
        DPRINT("KiArm64EventWaitSatisfaction: Notification event satisfied\n");
        return TRUE;
    }
    else
    {
        /* Synchronization events auto-reset */
        NewState = 0;

        /* ARM64: Atomic compare-and-swap with proper memory ordering */
        PreviousState = _InterlockedCompareExchange(&Event->Header.SignalState,
                                                   NewState,
                                                   CurrentState);

        if (PreviousState == CurrentState)
        {
            /* ARM64: Success - ensure the reset is visible */
            ARM64_EVENT_RELEASE_BARRIER();

            DPRINT("KiArm64EventWaitSatisfaction: Synchronization event satisfied and reset\n");
            return TRUE;
        }
        else
        {
            /* ARM64: CAS failed - another thread modified the event */
            DPRINT("KiArm64EventWaitSatisfaction: CAS failed - expected %ld, found %ld\n",
                   CurrentState, PreviousState);
            return FALSE;
        }
    }
}

/* EOF */