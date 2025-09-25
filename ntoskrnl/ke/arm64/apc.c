/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Asynchronous Procedure Calls (APC) Implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* APC Environment Types */
#define APC_ENVIRONMENT_THREAD          0
#define APC_ENVIRONMENT_PROCESS         1

/* APC States */
#define APC_STATE_IDLE                  0
#define APC_STATE_QUEUED                1
#define APC_STATE_RUNNING               2

/* Maximum APCs to process in one pass */
#define MAX_APC_PROCESS_COUNT           10

/* GLOBALS ********************************************************************/

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Insert APC into thread's APC queue
 *
 * Inserts an APC into the appropriate queue (kernel or user mode)
 * for the specified thread.
 */
static
VOID
FASTCALL
KiInsertApcInList(
    IN PKAPC Apc,
    IN PKTHREAD Thread)
{
    PLIST_ENTRY ListHead;
    PKAPC_STATE ApcState;
    PLIST_ENTRY NextEntry;
    PKAPC QueuedApc;
    BOOLEAN Inserted = FALSE;

    /* Get the APC state based on environment */
    if (Apc->ApcStateIndex == OriginalApcEnvironment)
    {
        ApcState = &Thread->ApcState;
    }
    else
    {
        ApcState = &Thread->SavedApcState;
    }

    /* Select appropriate queue based on APC mode */
    if (Apc->ApcMode == KernelMode)
    {
        ListHead = &ApcState->ApcListHead[KernelMode];
    }
    else
    {
        ListHead = &ApcState->ApcListHead[UserMode];
    }

    /* Handle special kernel APCs (they go to the front) */
    if (Apc->NormalRoutine == NULL && Apc->ApcMode == KernelMode)
    {
        /* Special kernel APC - insert at head */
        InsertHeadList(ListHead, &Apc->ApcListEntry);
        DPRINT("Special kernel APC %p inserted at head\n", Apc);
    }
    else
    {
        /* Normal APC - insert based on priority */
        NextEntry = ListHead->Flink;
        while (NextEntry != ListHead)
        {
            QueuedApc = CONTAINING_RECORD(NextEntry, KAPC, ApcListEntry);

            /* Insert before lower priority APCs */
            if (Apc->NormalRoutine == NULL && QueuedApc->NormalRoutine != NULL)
            {
                /* Special APC before normal APC */
                InsertTailList(NextEntry->Blink, &Apc->ApcListEntry);
                Inserted = TRUE;
                break;
            }

            NextEntry = NextEntry->Flink;
        }

        /* If not inserted yet, add to tail */
        if (!Inserted)
        {
            InsertTailList(ListHead, &Apc->ApcListEntry);
        }

        DPRINT("Normal APC %p inserted in queue\n", Apc);
    }

    /* Update APC state index */
    if (Apc->ApcMode == KernelMode)
    {
        ApcState->KernelApcPending = TRUE;
    }
    else
    {
        ApcState->UserApcPending = TRUE;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize an APC object
 * @implemented
 *
 * Initializes an Asynchronous Procedure Call object.
 *
 * @param Apc - Pointer to the APC object to initialize
 * @param Thread - Target thread for the APC
 * @param Environment - APC environment (thread or process)
 * @param KernelRoutine - Kernel routine to execute
 * @param RundownRoutine - Optional rundown routine
 * @param NormalRoutine - Optional normal routine
 * @param ProcessorMode - Processor mode for execution
 * @param NormalContext - Context for normal routine
 */
VOID
NTAPI
KeInitializeApc(
    IN PKAPC Apc,
    IN PKTHREAD Thread,
    IN KAPC_ENVIRONMENT Environment,
    IN PKKERNEL_ROUTINE KernelRoutine,
    IN PKRUNDOWN_ROUTINE RundownRoutine OPTIONAL,
    IN PKNORMAL_ROUTINE NormalRoutine OPTIONAL,
    IN KPROCESSOR_MODE ProcessorMode,
    IN PVOID NormalContext OPTIONAL)
{
    DPRINT("Initializing APC %p for thread %p\n", Apc, Thread);

    /* Initialize APC structure */
    Apc->Type = ApcObject;
    Apc->Size = sizeof(KAPC);
    Apc->Thread = Thread;
    Apc->ApcStateIndex = (CCHAR)Environment;
    Apc->KernelRoutine = KernelRoutine;
    Apc->RundownRoutine = RundownRoutine;
    Apc->NormalRoutine = NormalRoutine;
    Apc->NormalContext = NormalContext;

    /* Set APC mode based on normal routine presence */
    if (NormalRoutine)
    {
        Apc->ApcMode = (CCHAR)ProcessorMode;
    }
    else
    {
        /* Special kernel APC (no normal routine) */
        Apc->ApcMode = KernelMode;
    }

    /* Initialize as not inserted */
    Apc->Inserted = FALSE;
    InitializeListHead(&Apc->ApcListEntry);
}

/*
 * @brief Insert an APC into the queue
 * @implemented
 *
 * Queues an APC for execution on the target thread.
 *
 * @param Apc - Pointer to the APC to queue
 * @param SystemArgument1 - First system argument
 * @param SystemArgument2 - Second system argument
 * @param PriorityBoost - Priority boost for thread
 * @return TRUE if APC was queued, FALSE otherwise
 */
BOOLEAN
NTAPI
KeInsertQueueApc(
    IN PKAPC Apc,
    IN PVOID SystemArgument1 OPTIONAL,
    IN PVOID SystemArgument2 OPTIONAL,
    IN KPRIORITY PriorityBoost)
{
    KIRQL OldIrql;
    PKTHREAD Thread;
    BOOLEAN Inserted = FALSE;
    BOOLEAN RequestInterrupt = FALSE;

    DPRINT("Inserting APC %p into queue\n", Apc);

    /* Get target thread */
    Thread = Apc->Thread;
    if (!Thread)
    {
        DPRINT1("APC has no target thread\n");
        return FALSE;
    }

    /* Acquire thread APC lock */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&Thread->ApcQueueLock);

    /* Check if APC is already inserted */
    if (Apc->Inserted)
    {
        DPRINT("APC %p already inserted\n", Apc);
        goto Exit;
    }

    /* Check if thread is terminating */
    if (Thread->ApcState.KernelApcInProgress && Apc->ApcMode == KernelMode)
    {
        DPRINT("Cannot queue kernel APC - already in progress\n");
        goto Exit;
    }

    /* Store system arguments */
    Apc->SystemArgument1 = SystemArgument1;
    Apc->SystemArgument2 = SystemArgument2;

    /* Insert APC into appropriate list */
    KiInsertApcInList(Apc, Thread);
    Apc->Inserted = TRUE;
    Inserted = TRUE;

    /* Apply priority boost if specified */
    if (PriorityBoost != 0)
    {
        /* TODO: Adjust thread priority
         * Thread->Priority = min(Thread->Priority + PriorityBoost, HIGH_PRIORITY); */
    }

    /* Check if we need to alert thread */
    if (Apc->ApcMode == KernelMode)
    {
        /* Kernel APC - check if thread is waiting */
        if (Thread->State == Waiting && Thread->WaitIrql < APC_LEVEL)
        {
            /* Wake up thread to process APC */
            RequestInterrupt = TRUE;
            DPRINT("Waking thread %p for kernel APC\n", Thread);
        }
    }
    else
    {
        /* User APC - check if thread is alertable */
        if (Thread->State == Waiting && Thread->Alertable)
        {
            /* Wake up thread to process APC */
            RequestInterrupt = TRUE;
            DPRINT("Waking thread %p for user APC\n", Thread);
        }
    }

Exit:
    /* Release APC lock */
    KeReleaseSpinLock(&Thread->ApcQueueLock, OldIrql);

    /* Request interrupt if needed */
    if (RequestInterrupt)
    {
        /* TODO: Send IPI to target processor if different
         * KiIpiSendRequest(Thread->NextProcessor, IPI_APC); */
    }

    return Inserted;
}

/*
 * @brief Remove an APC from the queue
 * @implemented
 *
 * Removes a queued APC from the thread's APC list.
 *
 * @param Apc - Pointer to the APC to remove
 * @return TRUE if APC was removed, FALSE otherwise
 */
BOOLEAN
NTAPI
KeRemoveQueueApc(
    IN PKAPC Apc)
{
    KIRQL OldIrql;
    PKTHREAD Thread;
    BOOLEAN Removed = FALSE;

    DPRINT("Removing APC %p from queue\n", Apc);

    /* Get target thread */
    Thread = Apc->Thread;
    if (!Thread)
    {
        return FALSE;
    }

    /* Acquire thread APC lock */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&Thread->ApcQueueLock);

    /* Check if APC is inserted */
    if (Apc->Inserted)
    {
        /* Remove from list */
        RemoveEntryList(&Apc->ApcListEntry);
        Apc->Inserted = FALSE;
        Removed = TRUE;

        DPRINT("APC %p removed from queue\n", Apc);
    }

    /* Release APC lock */
    KeReleaseSpinLock(&Thread->ApcQueueLock, OldIrql);

    return Removed;
}

/*
 * @brief Deliver pending APCs
 * @implemented
 *
 * Delivers pending APCs for the current thread.
 * This is called when returning from kernel to user mode
 * or when thread becomes alertable.
 *
 * @param ProcessorMode - Mode to deliver APCs for
 * @param ExceptionFrame - Exception frame (ARM64 specific)
 * @param TrapFrame - Trap frame containing context
 */
VOID
NTAPI
KiDeliverApc(
    IN KPROCESSOR_MODE ProcessorMode,
    IN PKEXCEPTION_FRAME ExceptionFrame OPTIONAL,
    IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PKAPC Apc;
    PLIST_ENTRY ListHead, NextEntry;
    PKKERNEL_ROUTINE KernelRoutine;
    PKNORMAL_ROUTINE NormalRoutine;
    PKRUNDOWN_ROUTINE RundownRoutine;
    PVOID NormalContext;
    PVOID SystemArgument1, SystemArgument2;
    KIRQL OldIrql;
    ULONG ApcCount = 0;

    UNREFERENCED_PARAMETER(ExceptionFrame);

    /* Get current thread */
    Thread = KeGetCurrentThread();

    DPRINT("Delivering APCs for thread %p (mode=%d)\n", Thread, ProcessorMode);

    /* Raise IRQL to APC_LEVEL */
    KeRaiseIrql(APC_LEVEL, &OldIrql);

    /* Process kernel APCs first */
    if (ProcessorMode == KernelMode || Thread->ApcState.KernelApcPending)
    {
        /* Acquire APC lock */
        KeAcquireSpinLockAtDpcLevel(&Thread->ApcQueueLock);

        /* Process kernel APC queue */
        ListHead = &Thread->ApcState.ApcListHead[KernelMode];

        while (!IsListEmpty(ListHead) && ApcCount < MAX_APC_PROCESS_COUNT)
        {
            /* Get next APC */
            NextEntry = RemoveHeadList(ListHead);
            Apc = CONTAINING_RECORD(NextEntry, KAPC, ApcListEntry);

            /* Mark as not inserted */
            Apc->Inserted = FALSE;

            /* Save APC parameters */
            KernelRoutine = Apc->KernelRoutine;
            NormalRoutine = Apc->NormalRoutine;
            RundownRoutine = Apc->RundownRoutine;
            NormalContext = Apc->NormalContext;
            SystemArgument1 = Apc->SystemArgument1;
            SystemArgument2 = Apc->SystemArgument2;

            /* Mark kernel APC in progress */
            Thread->ApcState.KernelApcInProgress = TRUE;

            /* Release lock to call kernel routine */
            KeReleaseSpinLockFromDpcLevel(&Thread->ApcQueueLock);

            /* Lower IRQL for kernel routine */
            KeLowerIrql(OldIrql);

            /* Call kernel routine */
            DPRINT("Calling kernel APC routine %p\n", KernelRoutine);
            KernelRoutine(Apc, &NormalRoutine, &NormalContext,
                         &SystemArgument1, &SystemArgument2);

            /* Check if there's a normal routine */
            if (NormalRoutine && ProcessorMode != KernelMode)
            {
                /* Setup user mode APC delivery
                 * TODO: Build APC frame on user stack
                 * TODO: Set TrapFrame to return to KiUserApcDispatcher */

                DPRINT("Setting up user APC delivery for routine %p\n", NormalRoutine);
            }

            /* Raise IRQL back to APC_LEVEL */
            KeRaiseIrql(APC_LEVEL, &OldIrql);

            /* Reacquire lock */
            KeAcquireSpinLockAtDpcLevel(&Thread->ApcQueueLock);

            /* Clear kernel APC in progress */
            Thread->ApcState.KernelApcInProgress = FALSE;

            ApcCount++;
        }

        /* Check if queue is empty */
        if (IsListEmpty(ListHead))
        {
            Thread->ApcState.KernelApcPending = FALSE;
        }

        /* Release APC lock */
        KeReleaseSpinLockFromDpcLevel(&Thread->ApcQueueLock);
    }

    /* Process user APCs if returning to user mode */
    if (ProcessorMode == UserMode && Thread->ApcState.UserApcPending)
    {
        /* Acquire APC lock */
        KeAcquireSpinLockAtDpcLevel(&Thread->ApcQueueLock);

        /* Process user APC queue */
        ListHead = &Thread->ApcState.ApcListHead[UserMode];

        if (!IsListEmpty(ListHead))
        {
            /* Get next APC */
            NextEntry = RemoveHeadList(ListHead);
            Apc = CONTAINING_RECORD(NextEntry, KAPC, ApcListEntry);

            /* Mark as not inserted */
            Apc->Inserted = FALSE;

            /* TODO: Setup user mode APC context on stack
             * - Save current context
             * - Push APC parameters
             * - Set return address to KiUserApcDispatcher
             */

            DPRINT("Queued user APC for delivery\n");
        }

        /* Check if queue is empty */
        if (IsListEmpty(ListHead))
        {
            Thread->ApcState.UserApcPending = FALSE;
        }

        /* Release APC lock */
        KeReleaseSpinLockFromDpcLevel(&Thread->ApcQueueLock);
    }

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    DPRINT("Delivered %lu APCs\n", ApcCount);
}

/*
 * @brief Test if thread is alerted
 * @implemented
 *
 * Tests and optionally clears the alert state of a thread.
 *
 * @param AlertMode - Alert mode to test
 * @return TRUE if thread was alerted, FALSE otherwise
 */
BOOLEAN
NTAPI
KeTestAlertThread(
    IN KPROCESSOR_MODE AlertMode)
{
    PKTHREAD Thread;
    BOOLEAN Alerted = FALSE;
    KIRQL OldIrql;

    /* Get current thread */
    Thread = KeGetCurrentThread();

    DPRINT("Testing alert for thread %p (mode=%d)\n", Thread, AlertMode);

    /* Acquire thread APC lock */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&Thread->ApcQueueLock);

    /* Check alert state based on mode */
    if (AlertMode == KernelMode)
    {
        /* Check kernel alert */
        if (Thread->Alerted[KernelMode])
        {
            Thread->Alerted[KernelMode] = FALSE;
            Alerted = TRUE;
        }
    }
    else
    {
        /* Check user alert */
        if (Thread->Alerted[UserMode])
        {
            Thread->Alerted[UserMode] = FALSE;
            Alerted = TRUE;
        }
        else if (Thread->ApcState.UserApcPending)
        {
            /* User APCs are pending */
            Alerted = TRUE;
        }
    }

    /* Release lock */
    KeReleaseSpinLock(&Thread->ApcQueueLock, OldIrql);

    DPRINT("Thread %p alert test: %s\n", Thread, Alerted ? "alerted" : "not alerted");

    return Alerted;
}

/*
 * @brief Alert thread
 * @implemented
 *
 * Sets the alert state for a thread.
 *
 * @param Thread - Thread to alert
 * @param AlertMode - Alert mode to set
 * @return Previous alert state
 */
BOOLEAN
NTAPI
KeAlertThread(
    IN PKTHREAD Thread,
    IN KPROCESSOR_MODE AlertMode)
{
    BOOLEAN PreviousState;
    KIRQL OldIrql;

    DPRINT("Alerting thread %p (mode=%d)\n", Thread, AlertMode);

    /* Acquire thread APC lock */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&Thread->ApcQueueLock);

    /* Get previous state and set new state */
    PreviousState = Thread->Alerted[AlertMode];
    Thread->Alerted[AlertMode] = TRUE;

    /* Check if thread should be woken */
    if (!PreviousState && Thread->State == Waiting)
    {
        /* Check wait mode */
        if ((AlertMode == KernelMode && Thread->WaitMode == KernelMode) ||
            (AlertMode == UserMode && Thread->Alertable))
        {
            /* TODO: Wake thread from wait
             * KiUnwaitThread(Thread, STATUS_ALERTED, 0); */

            DPRINT("Waking thread %p due to alert\n", Thread);
        }
    }

    /* Release lock */
    KeReleaseSpinLock(&Thread->ApcQueueLock, OldIrql);

    return PreviousState;
}

/*
 * @brief Alert and resume thread
 * @implemented
 *
 * Alerts a thread and resumes it if suspended.
 *
 * @param Thread - Thread to alert and resume
 * @return Previous suspend count
 */
ULONG
NTAPI
KeAlertResumeThread(
    IN PKTHREAD Thread)
{
    ULONG PreviousSuspendCount;
    KIRQL OldIrql;

    DPRINT("Alert and resume thread %p\n", Thread);

    /* Acquire thread lock */
    OldIrql = KeAcquireSpinLockRaiseToSynch(&Thread->ApcQueueLock);

    /* Alert thread in user mode */
    Thread->Alerted[UserMode] = TRUE;

    /* Get previous suspend count */
    PreviousSuspendCount = Thread->SuspendCount;

    /* Resume thread if suspended */
    if (Thread->SuspendCount > 0)
    {
        Thread->SuspendCount--;

        if (Thread->SuspendCount == 0 && Thread->State == Suspended)
        {
            /* TODO: Resume thread execution
             * KiReadyThread(Thread); */

            DPRINT("Thread %p resumed from suspension\n", Thread);
        }
    }

    /* Release lock */
    KeReleaseSpinLock(&Thread->ApcQueueLock, OldIrql);

    return PreviousSuspendCount;
}

/*
 * @brief Initialize thread APC state
 * @implemented
 *
 * Initializes APC state structures for a thread.
 *
 * @param Thread - Thread to initialize
 * @param Process - Parent process
 */
VOID
NTAPI
KiInitializeApcState(
    IN PKTHREAD Thread,
    IN PKPROCESS Process)
{
    DPRINT("Initializing APC state for thread %p\n", Thread);

    /* Initialize APC queue lock */
    KeInitializeSpinLock(&Thread->ApcQueueLock);

    /* Initialize normal APC state */
    InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->ApcState.ApcListHead[UserMode]);
    Thread->ApcState.Process = Process;
    Thread->ApcState.KernelApcInProgress = FALSE;
    Thread->ApcState.KernelApcPending = FALSE;
    Thread->ApcState.UserApcPending = FALSE;

    /* Initialize saved APC state */
    InitializeListHead(&Thread->SavedApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->SavedApcState.ApcListHead[UserMode]);
    Thread->SavedApcState.Process = Process;
    Thread->SavedApcState.KernelApcInProgress = FALSE;
    Thread->SavedApcState.KernelApcPending = FALSE;
    Thread->SavedApcState.UserApcPending = FALSE;

    /* Clear alert state */
    Thread->Alerted[KernelMode] = FALSE;
    Thread->Alerted[UserMode] = FALSE;
    Thread->Alertable = FALSE;

    DPRINT("APC state initialized for thread %p\n", Thread);
}

/* EOF */