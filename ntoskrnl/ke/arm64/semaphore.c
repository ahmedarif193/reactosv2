/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Semaphore Dispatcher Object Implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64 specific intrinsics */
#include <internal/arm64/intrin_i.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 memory ordering for semaphore operations */
#define ARM64_SEMAPHORE_ACQUIRE_BARRIER()  __dmb(ishld)
#define ARM64_SEMAPHORE_RELEASE_BARRIER()  __dmb(ishst)
#define ARM64_SEMAPHORE_FULL_BARRIER()     __dmb(ish)

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeSemaphore(IN PKSEMAPHORE Semaphore,
                      IN LONG Count,
                      IN LONG Limit)
{
    ASSERT(Semaphore != NULL);
    ASSERT(Count >= 0);
    ASSERT(Limit > 0);
    ASSERT(Count <= Limit);

    DPRINT("KeInitializeSemaphore: Semaphore=%p, Count=%ld, Limit=%ld\n",
           Semaphore, Count, Limit);

    /* Initialize the Dispatcher Header */
    Semaphore->Header.Type = SemaphoreObject;
    Semaphore->Header.Size = sizeof(KSEMAPHORE) / sizeof(ULONG);
    Semaphore->Header.SignalState = Count;
    InitializeListHead(&(Semaphore->Header.WaitListHead));

    /* Set the Limit */
    Semaphore->Limit = Limit;

    /* ARM64: Ensure initialization is visible to all cores */
    ARM64_SEMAPHORE_FULL_BARRIER();
}

/*
 * @implemented
 */
LONG
NTAPI
KeReadStateSemaphore(IN PKSEMAPHORE Semaphore)
{
    LONG SignalState;

    ASSERT_SEMAPHORE(Semaphore);

    DPRINT("KeReadStateSemaphore: Semaphore=%p\n", Semaphore);

    /* ARM64: Load with acquire semantics for proper ordering */
    ARM64_SEMAPHORE_ACQUIRE_BARRIER();

    /* Read the signal state atomically */
    SignalState = Semaphore->Header.SignalState;

    /* ARM64: Memory barrier to ensure read completes */
    ARM64_SEMAPHORE_ACQUIRE_BARRIER();

    DPRINT("KeReadStateSemaphore: SignalState=%ld\n", SignalState);

    return SignalState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReleaseSemaphore(IN PKSEMAPHORE Semaphore,
                   IN KPRIORITY Increment,
                   IN LONG Adjustment,
                   IN BOOLEAN Wait)
{
    LONG InitialState, State;
    KIRQL OldIrql;
    PKTHREAD CurrentThread;

    ASSERT_SEMAPHORE(Semaphore);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT(Adjustment > 0);

    DPRINT("KeReleaseSemaphore: Semaphore=%p, Increment=%d, Adjustment=%ld, Wait=%d\n",
           Semaphore, Increment, Adjustment, Wait);

    /* ARM64: Memory barrier before critical section */
    ARM64_SEMAPHORE_ACQUIRE_BARRIER();

    /* Lock the Dispatcher Database */
    OldIrql = KiAcquireDispatcherLock();

    /* ARM64: Additional barrier after acquiring lock for multi-core coherency */
    ARM64_SEMAPHORE_ACQUIRE_BARRIER();

    /* Save the Old State and compute new state atomically */
    InitialState = Semaphore->Header.SignalState;
    State = InitialState + Adjustment;

    DPRINT("KeReleaseSemaphore: InitialState=%ld, NewState=%ld, Limit=%ld\n",
           InitialState, State, Semaphore->Limit);

    /* Check if the Limit was exceeded or integer overflow occurred */
    if ((Semaphore->Limit < State) || (InitialState > State))
    {
        DPRINT1("KeReleaseSemaphore: Limit exceeded - Limit=%ld, NewState=%ld\n",
                Semaphore->Limit, State);

        /* ARM64: Release barrier before unlocking */
        ARM64_SEMAPHORE_RELEASE_BARRIER();

        /* Release the lock and raise error */
        KiReleaseDispatcherLock(OldIrql);
        ExRaiseStatus(STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    }

    /* ARM64: Use atomic store for the new state with release semantics */
    _InterlockedExchange(&Semaphore->Header.SignalState, State);

    /* ARM64: Full memory barrier to ensure state change is visible */
    ARM64_SEMAPHORE_FULL_BARRIER();

    /* Check if we should wake waiting threads */
    if (!(InitialState) && !(IsListEmpty(&Semaphore->Header.WaitListHead)))
    {
        DPRINT("KeReleaseSemaphore: Waking waiting threads\n");

        /* Wake the Semaphore - this handles ARM64 scheduling appropriately */
        KiWaitTest(&Semaphore->Header, Increment);
    }

    /* Check if the caller wants to wait after this release */
    if (Wait == FALSE)
    {
        /* ARM64: Release barrier before unlocking */
        ARM64_SEMAPHORE_RELEASE_BARRIER();

        /* Release the Lock */
        KiReleaseDispatcherLock(OldIrql);
    }
    else
    {
        /* Set a wait - the lock will be released by the wait operation */
        CurrentThread = KeGetCurrentThread();
        CurrentThread->WaitNext = TRUE;
        CurrentThread->WaitIrql = OldIrql;

        /* ARM64: Memory barrier for wait state visibility */
        ARM64_SEMAPHORE_FULL_BARRIER();
    }

    DPRINT("KeReleaseSemaphore: Returning InitialState=%ld\n", InitialState);

    /* Return the previous state */
    return InitialState;
}

/*
 * @brief ARM64-specific semaphore wait satisfaction logic
 *
 * This function handles the ARM64-specific aspects of semaphore wait satisfaction.
 * Called from the generic wait system when a semaphore needs to be decremented.
 */
BOOLEAN
FASTCALL
KiArm64SemaphoreWaitSatisfaction(IN PKSEMAPHORE Semaphore,
                                 IN PKTHREAD Thread)
{
    LONG CurrentState;
    LONG NewState;
    LONG PreviousState;

    ASSERT_SEMAPHORE(Semaphore);
    ASSERT(Thread != NULL);

    DPRINT("KiArm64SemaphoreWaitSatisfaction: Semaphore=%p, Thread=%p\n",
           Semaphore, Thread);

    /* ARM64: Load current state with acquire semantics */
    ARM64_SEMAPHORE_ACQUIRE_BARRIER();
    CurrentState = Semaphore->Header.SignalState;

    /* Check if semaphore is available */
    if (CurrentState <= 0)
    {
        DPRINT("KiArm64SemaphoreWaitSatisfaction: Semaphore not available (state=%ld)\n",
               CurrentState);
        return FALSE;
    }

    /* Calculate new state after decrementing */
    NewState = CurrentState - 1;

    /* ARM64: Atomic compare-and-swap with proper memory ordering */
    PreviousState = _InterlockedCompareExchange(&Semaphore->Header.SignalState,
                                               NewState,
                                               CurrentState);

    if (PreviousState == CurrentState)
    {
        /* ARM64: Success - ensure the decrement is visible */
        ARM64_SEMAPHORE_RELEASE_BARRIER();

        DPRINT("KiArm64SemaphoreWaitSatisfaction: Success - decremented from %ld to %ld\n",
               CurrentState, NewState);
        return TRUE;
    }
    else
    {
        /* ARM64: CAS failed - another thread modified the semaphore */
        DPRINT("KiArm64SemaphoreWaitSatisfaction: CAS failed - expected %ld, found %ld\n",
               CurrentState, PreviousState);
        return FALSE;
    }
}

/* EOF */