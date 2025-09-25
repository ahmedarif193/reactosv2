/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Mutex (Mutant) Implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* Mutex states */
#define MUTEX_LOCKED_BIT        0x0001
#define MUTEX_ABANDONED_BIT     0x0002
#define MUTEX_WAKE_BIT          0x0004

/* Fast mutex states */
#define FM_LOCK_BIT             0x0001
#define FM_LOCK_BIT_V           0x0000
#define FM_LOCK_WAITER_INC      0x0002

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Acquire mutex with wait
 */
static
NTSTATUS
FASTCALL
KiAcquireMutexObject(
    IN PKMUTANT Mutex,
    IN PKTHREAD Thread,
    IN PLARGE_INTEGER Timeout OPTIONAL)
{
    NTSTATUS Status;
    LONG OldState;

    /* Check if mutex is available or owned by current thread */
    OldState = Mutex->Header.SignalState;

    if (OldState > 0 || Mutex->OwnerThread == Thread)
    {
        /* We can acquire it */
        Mutex->Header.SignalState--;
        Mutex->OwnerThread = Thread;

        /* Check for abandoned mutex */
        if (Mutex->Abandoned)
        {
            Mutex->Abandoned = FALSE;
            Status = STATUS_ABANDONED_WAIT_0;
        }
        else
        {
            Status = STATUS_SUCCESS;
        }

        DPRINT("Thread %p acquired mutex %p (count=%ld)\n",
               Thread, Mutex, -Mutex->Header.SignalState);
    }
    else
    {
        /* Need to wait */
        Status = KeWaitForSingleObject(Mutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       Timeout);
    }

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize a mutex object
 * @implemented
 */
VOID
NTAPI
KeInitializeMutex(
    IN PKMUTEX Mutex,
    IN ULONG Level)
{
    DPRINT("Initializing mutex %p at level %lu\n", Mutex, Level);

    /* Initialize as mutant object */
    KeInitializeMutant(Mutex, FALSE);

    /* Store level for priority ordering */
    Mutex->Header.SignalState = 1;  /* Initially signaled (available) */

    UNREFERENCED_PARAMETER(Level);
}

/*
 * @brief Initialize a mutant object
 * @implemented
 */
VOID
NTAPI
KeInitializeMutant(
    IN PKMUTANT Mutant,
    IN BOOLEAN InitialOwner)
{
    DPRINT("Initializing mutant %p (initial owner=%d)\n", Mutant, InitialOwner);

    /* Initialize dispatcher header */
    Mutant->Header.Type = MutantObject;
    Mutant->Header.Size = sizeof(KMUTANT) / sizeof(LONG);
    InitializeListHead(&Mutant->Header.WaitListHead);

    /* Set initial state */
    if (InitialOwner)
    {
        /* Owned by current thread */
        Mutant->Header.SignalState = 0;
        Mutant->OwnerThread = KeGetCurrentThread();
    }
    else
    {
        /* Not owned */
        Mutant->Header.SignalState = 1;
        Mutant->OwnerThread = NULL;
    }

    Mutant->Abandoned = FALSE;
    Mutant->ApcDisable = 0;
    InitializeListHead(&Mutant->MutantListEntry);
}

/*
 * @brief Release a mutex
 * @implemented
 */
LONG
NTAPI
KeReleaseMutex(
    IN PKMUTEX Mutex,
    IN BOOLEAN Wait)
{
    return KeReleaseMutant(Mutex, MUTANT_INCREMENT, FALSE, Wait);
}

/*
 * @brief Release a mutant object
 * @implemented
 */
LONG
NTAPI
KeReleaseMutant(
    IN PKMUTANT Mutant,
    IN KPRIORITY Increment,
    IN BOOLEAN Abandoned,
    IN BOOLEAN Wait)
{
    PKTHREAD CurrentThread;
    LONG OldState;
    KIRQL OldIrql;
    PLIST_ENTRY WaitEntry;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitingThread;

    CurrentThread = KeGetCurrentThread();

    DPRINT("Releasing mutant %p by thread %p\n", Mutant, CurrentThread);

    /* Acquire dispatcher lock */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DispatcherLock);

    /* Verify ownership */
    if (Mutant->OwnerThread != CurrentThread)
    {
        KeReleaseSpinLockFromDpcLevel(&DispatcherLock);
        KeLowerIrql(OldIrql);
        DPRINT1("Thread %p does not own mutant %p\n", CurrentThread, Mutant);
        ExRaiseStatus(STATUS_MUTANT_NOT_OWNED);
    }

    /* Save old state */
    OldState = Mutant->Header.SignalState;

    /* Increment signal state (make less negative or positive) */
    Mutant->Header.SignalState++;

    /* Check if mutant is now available */
    if (Mutant->Header.SignalState == 1)
    {
        /* Mutant is now free */
        Mutant->OwnerThread = NULL;

        /* Mark as abandoned if requested */
        if (Abandoned)
        {
            Mutant->Abandoned = TRUE;
        }

        /* Check for waiters */
        if (!IsListEmpty(&Mutant->Header.WaitListHead))
        {
            /* Get first waiter */
            WaitEntry = RemoveHeadList(&Mutant->Header.WaitListHead);
            WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
            WaitingThread = WaitBlock->Thread;

            /* Transfer ownership to waiting thread */
            Mutant->Header.SignalState = 0;
            Mutant->OwnerThread = WaitingThread;

            /* Wake the thread */
            WaitingThread->WaitStatus = Abandoned ? STATUS_ABANDONED_WAIT_0 : STATUS_SUCCESS;
            /* TODO: Ready thread for execution */

            DPRINT("Transferred mutant %p ownership to thread %p\n", Mutant, WaitingThread);
        }
    }

    /* Release dispatcher lock */
    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);

    /* Handle wait after release if requested */
    if (Wait)
    {
        /* Lower to APC_LEVEL for wait */
        KeLowerIrql(APC_LEVEL);

        /* TODO: Enter wait state */
    }
    else
    {
        /* Just lower IRQL */
        KeLowerIrql(OldIrql);
    }

    DPRINT("Mutant %p released, old state was %ld\n", Mutant, OldState);

    return OldState;
}

/*
 * @brief Read mutant state
 * @implemented
 */
LONG
NTAPI
KeReadStateMutant(
    IN PKMUTANT Mutant)
{
    /* Return current signal state */
    return Mutant->Header.SignalState;
}

/*
 * @brief Acquire fast mutex
 * @implemented
 *
 * Fast mutexes are optimized for speed and don't support recursion
 */
VOID
FASTCALL
ExAcquireFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;

    /* Raise IRQL to APC_LEVEL to prevent thread switching */
    KeRaiseIrql(APC_LEVEL, &OldIrql);

    /* Try to acquire using atomic operation */
    if (InterlockedBitTestAndSet((PLONG)&FastMutex->Count, 0))
    {
        /* Fast mutex is already owned, must wait */
        KeWaitForSingleObject(&FastMutex->Event,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL);
    }

    /* We now own the mutex */
    FastMutex->Owner = KeGetCurrentThread();
    FastMutex->OldIrql = OldIrql;

    DPRINT("Thread %p acquired fast mutex %p\n", FastMutex->Owner, FastMutex);
}

/*
 * @brief Release fast mutex
 * @implemented
 */
VOID
FASTCALL
ExReleaseFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;

    DPRINT("Thread %p releasing fast mutex %p\n", KeGetCurrentThread(), FastMutex);

    /* Verify ownership */
    if (FastMutex->Owner != KeGetCurrentThread())
    {
        KeBugCheck(THREAD_NOT_MUTEX_OWNER);
    }

    /* Save IRQL to restore */
    OldIrql = FastMutex->OldIrql;

    /* Clear owner */
    FastMutex->Owner = NULL;

    /* Release using atomic operation */
    if (InterlockedBitTestAndReset((PLONG)&FastMutex->Count, 0))
    {
        /* Wake any waiters */
        KeSetEvent(&FastMutex->Event, IO_NO_INCREMENT, FALSE);
    }

    /* Lower IRQL back */
    KeLowerIrql(OldIrql);
}

/*
 * @brief Try to acquire fast mutex
 * @implemented
 *
 * Attempts to acquire without waiting
 */
BOOLEAN
FASTCALL
ExTryToAcquireFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;

    /* Raise IRQL to APC_LEVEL */
    KeRaiseIrql(APC_LEVEL, &OldIrql);

    /* Try to acquire using atomic operation */
    if (!InterlockedBitTestAndSet((PLONG)&FastMutex->Count, 0))
    {
        /* Successfully acquired */
        FastMutex->Owner = KeGetCurrentThread();
        FastMutex->OldIrql = OldIrql;

        DPRINT("Thread %p try-acquired fast mutex %p\n", FastMutex->Owner, FastMutex);
        return TRUE;
    }

    /* Failed to acquire, restore IRQL */
    KeLowerIrql(OldIrql);

    DPRINT("Thread %p failed to try-acquire fast mutex %p\n",
           KeGetCurrentThread(), FastMutex);
    return FALSE;
}

/*
 * @brief Initialize fast mutex
 * @implemented
 */
VOID
FASTCALL
ExInitializeFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    DPRINT("Initializing fast mutex %p\n", FastMutex);

    /* Initialize count (bit 0 clear = available) */
    FastMutex->Count = FM_LOCK_BIT_V;

    /* No owner initially */
    FastMutex->Owner = NULL;

    /* Initialize event for waiters */
    KeInitializeEvent(&FastMutex->Event, SynchronizationEvent, FALSE);

    /* IRQL will be set when acquired */
    FastMutex->OldIrql = PASSIVE_LEVEL;
}

/*
 * @brief Acquire fast mutex unsafe (doesn't change IRQL)
 * @implemented
 */
VOID
FASTCALL
ExAcquireFastMutexUnsafe(
    IN PFAST_MUTEX FastMutex)
{
    /* Verify we're already at APC_LEVEL or higher */
    ASSERT(KeGetCurrentIrql() >= APC_LEVEL);

    /* Try to acquire using atomic operation */
    if (InterlockedBitTestAndSet((PLONG)&FastMutex->Count, 0))
    {
        /* Fast mutex is already owned, must wait */
        KeWaitForSingleObject(&FastMutex->Event,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL);
    }

    /* We now own the mutex */
    FastMutex->Owner = KeGetCurrentThread();

    DPRINT("Thread %p acquired fast mutex %p (unsafe)\n", FastMutex->Owner, FastMutex);
}

/*
 * @brief Release fast mutex unsafe (doesn't change IRQL)
 * @implemented
 */
VOID
FASTCALL
ExReleaseFastMutexUnsafe(
    IN PFAST_MUTEX FastMutex)
{
    DPRINT("Thread %p releasing fast mutex %p (unsafe)\n",
           KeGetCurrentThread(), FastMutex);

    /* Verify ownership */
    if (FastMutex->Owner != KeGetCurrentThread())
    {
        KeBugCheck(THREAD_NOT_MUTEX_OWNER);
    }

    /* Clear owner */
    FastMutex->Owner = NULL;

    /* Release using atomic operation */
    if (InterlockedBitTestAndReset((PLONG)&FastMutex->Count, 0))
    {
        /* Wake any waiters */
        KeSetEvent(&FastMutex->Event, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * @brief Enter critical region (disable APCs)
 * @implemented
 */
VOID
NTAPI
KeEnterCriticalRegion(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Disable kernel APCs */
    Thread->KernelApcDisable--;

    DPRINT("Thread %p entered critical region (disable count=%d)\n",
           Thread, Thread->KernelApcDisable);
}

/*
 * @brief Leave critical region (enable APCs)
 * @implemented
 */
VOID
NTAPI
KeLeaveCriticalRegion(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Re-enable kernel APCs */
    Thread->KernelApcDisable++;

    /* Check if APCs should be delivered */
    if (Thread->KernelApcDisable == 0)
    {
        /* Check for pending APCs */
        if (!IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]))
        {
            /* Request APC interrupt */
            /* TODO: Request software interrupt for APC delivery */
            DPRINT("APCs pending for thread %p\n", Thread);
        }
    }

    DPRINT("Thread %p left critical region (disable count=%d)\n",
           Thread, Thread->KernelApcDisable);
}

/*
 * @brief Are APCs disabled?
 * @implemented
 */
BOOLEAN
NTAPI
KeAreApcsDisabled(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* APCs are disabled if:
     * - IRQL >= APC_LEVEL, or
     * - Inside critical region, or
     * - Special kernel APC in progress
     */
    return (KeGetCurrentIrql() >= APC_LEVEL) ||
           (Thread->KernelApcDisable < 0) ||
           (Thread->ApcState.KernelApcInProgress);
}

/*
 * @brief Enter guarded region (disable all APCs)
 * @implemented
 */
VOID
NTAPI
KeEnterGuardedRegion(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Disable all APCs (kernel and user) */
    Thread->SpecialApcDisable--;

    DPRINT("Thread %p entered guarded region\n", Thread);
}

/*
 * @brief Leave guarded region
 * @implemented
 */
VOID
NTAPI
KeLeaveGuardedRegion(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Re-enable all APCs */
    Thread->SpecialApcDisable++;

    /* Check if APCs should be delivered */
    if (Thread->SpecialApcDisable == 0)
    {
        /* Check for any pending APCs */
        if (!IsListEmpty(&Thread->ApcState.ApcListHead[KernelMode]) ||
            !IsListEmpty(&Thread->ApcState.ApcListHead[UserMode]))
        {
            /* Request APC interrupt */
            /* TODO: Request software interrupt for APC delivery */
            DPRINT("APCs pending for thread %p after leaving guarded region\n", Thread);
        }
    }

    DPRINT("Thread %p left guarded region\n", Thread);
}

/*
 * @brief Are we in a guarded region?
 * @implemented
 */
BOOLEAN
NTAPI
KeAreAllApcsDisabled(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* All APCs are disabled if in guarded region or at high IRQL */
    return (Thread->SpecialApcDisable < 0) ||
           (KeGetCurrentIrql() >= APC_LEVEL);
}

/* EOF */