/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Deferred Procedure Calls (DPC) Implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* DPC Priority Levels */
#define DPC_PRIORITY_LOW        0
#define DPC_PRIORITY_MEDIUM     1
#define DPC_PRIORITY_HIGH       2

/* DPC Queue States */
#define DPC_STATE_IDLE          0
#define DPC_STATE_QUEUED        1
#define DPC_STATE_RUNNING       2

/* Maximum DPC queue depth before forcing flush */
#define MAX_DPC_QUEUE_DEPTH     16

/* GLOBALS ********************************************************************/

KDPC KiTimerExpireDpc;
ULONG KiMaximumDpcQueueDepth = MAX_DPC_QUEUE_DEPTH;
ULONG KiMinimumDpcRate = 3;
ULONG KiAdjustDpcThreshold = 20;
ULONG KiIdealDpcRate = 20;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Request DPC software interrupt
 *
 * Requests a software interrupt to process the DPC queue
 * on the current processor.
 */
static
VOID
FASTCALL
KiRequestDpcSoftwareInterrupt(
    IN PKPRCB Prcb)
{
    /* TODO: Request software interrupt for DPC processing
     * On ARM64, this typically involves:
     * 1. Setting a software interrupt pending bit
     * 2. Using SGI (Software Generated Interrupt) for IPI
     * 3. Or using a dedicated DPC interrupt vector
     */

    /* For now, just set the DPC interrupt flag */
    Prcb->DpcInterruptRequested = TRUE;

    /* TODO: Trigger actual software interrupt
     * HalRequestSoftwareInterrupt(DISPATCH_LEVEL); */

    DPRINT("DPC software interrupt requested on CPU %u\n",
           KeGetCurrentProcessorNumber());
}

/*
 * @brief Insert DPC into processor queue
 *
 * Inserts a DPC into the appropriate queue based on priority
 */
static
VOID
FASTCALL
KiInsertDpcInQueue(
    IN PKDPC Dpc,
    IN PKPRCB Prcb,
    IN KDPC_IMPORTANCE Importance)
{
    PLIST_ENTRY ListHead;

    /* Select queue based on importance */
    switch (Importance)
    {
        case LowImportance:
            ListHead = &Prcb->DpcData[DPC_NORMAL].DpcListHead;
            break;

        case MediumImportance:
        case MediumHighImportance:
            ListHead = &Prcb->DpcData[DPC_NORMAL].DpcListHead;
            break;

        case HighImportance:
            ListHead = &Prcb->DpcData[DPC_THREADED].DpcListHead;
            break;

        default:
            ListHead = &Prcb->DpcData[DPC_NORMAL].DpcListHead;
            break;
    }

    /* Insert DPC at end of queue */
    InsertTailList(ListHead, &Dpc->DpcListEntry);

    /* Update queue depth */
    Prcb->DpcData[DPC_NORMAL].DpcQueueDepth++;

    /* Mark DPC as queued */
    Dpc->Type = (UCHAR)(DPC_STATE_QUEUED | (Dpc->Type & ~0x3));

    DPRINT("DPC %p inserted in queue (depth=%lu)\n",
           Dpc, Prcb->DpcData[DPC_NORMAL].DpcQueueDepth);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize a DPC object
 * @implemented
 *
 * Initializes a Deferred Procedure Call object with the specified
 * routine and context.
 *
 * @param Dpc - Pointer to the DPC object to initialize
 * @param DeferredRoutine - Function to call when DPC executes
 * @param DeferredContext - Context parameter for the routine
 */
VOID
NTAPI
KeInitializeDpc(
    IN PKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext OPTIONAL)
{
    DPRINT("Initializing DPC %p with routine %p\n", Dpc, DeferredRoutine);

    /* Initialize DPC structure */
    Dpc->Type = DpcObject;
    Dpc->Number = 0;  /* Not targeted to specific processor */
    Dpc->Importance = MediumImportance;
    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->DeferredContext = DeferredContext;
    Dpc->SystemArgument1 = NULL;
    Dpc->SystemArgument2 = NULL;
    InitializeListHead(&Dpc->DpcListEntry);
}

/*
 * @brief Insert a DPC into the queue
 * @implemented
 *
 * Queues a DPC for execution at DISPATCH_LEVEL. The DPC will
 * execute on the target processor or current processor.
 *
 * @param Dpc - Pointer to the DPC to queue
 * @param SystemArgument1 - First system argument
 * @param SystemArgument2 - Second system argument
 * @return TRUE if DPC was queued, FALSE if already queued
 */
BOOLEAN
NTAPI
KeInsertQueueDpc(
    IN PKDPC Dpc,
    IN PVOID SystemArgument1 OPTIONAL,
    IN PVOID SystemArgument2 OPTIONAL)
{
    KIRQL OldIrql;
    PKPRCB Prcb;
    BOOLEAN Queued = FALSE;
    ULONG Processor;

    DPRINT("Inserting DPC %p into queue\n", Dpc);

    /* Raise IRQL to DISPATCH_LEVEL */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Check if DPC is already queued */
    if (Dpc->Type & DPC_STATE_QUEUED)
    {
        DPRINT("DPC %p already queued\n", Dpc);
        goto Exit;
    }

    /* Store system arguments */
    Dpc->SystemArgument1 = SystemArgument1;
    Dpc->SystemArgument2 = SystemArgument2;

    /* Determine target processor */
    if (Dpc->Number >= MAXIMUM_PROCESSORS)
    {
        /* DPC targets current processor */
        Processor = KeGetCurrentProcessorNumber();
    }
    else if (Dpc->Number == 0)
    {
        /* DPC can run on any processor - use current */
        Processor = KeGetCurrentProcessorNumber();
    }
    else
    {
        /* DPC targets specific processor */
        Processor = Dpc->Number - 1;
    }

    /* Get processor control block */
    Prcb = KiProcessorBlock[Processor];
    if (!Prcb)
    {
        /* Processor not available */
        DPRINT1("Processor %lu not available for DPC\n", Processor);
        goto Exit;
    }

    /* Insert DPC into queue */
    KiInsertDpcInQueue(Dpc, Prcb, Dpc->Importance);
    Queued = TRUE;

    /* Check if we need to request DPC interrupt */
    if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth >= KiMaximumDpcQueueDepth ||
        Dpc->Importance == HighImportance)
    {
        /* Request DPC software interrupt */
        KiRequestDpcSoftwareInterrupt(Prcb);
    }

Exit:
    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    return Queued;
}

/*
 * @brief Remove a DPC from the queue
 * @implemented
 *
 * Removes a queued DPC from execution queue.
 *
 * @param Dpc - Pointer to the DPC to remove
 * @return TRUE if DPC was in queue, FALSE otherwise
 */
BOOLEAN
NTAPI
KeRemoveQueueDpc(
    IN PKDPC Dpc)
{
    KIRQL OldIrql;
    BOOLEAN InQueue = FALSE;
    PKPRCB Prcb;

    DPRINT("Removing DPC %p from queue\n", Dpc);

    /* Raise IRQL to DISPATCH_LEVEL */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Check if DPC is queued */
    if (Dpc->Type & DPC_STATE_QUEUED)
    {
        /* Remove from list */
        RemoveEntryList(&Dpc->DpcListEntry);

        /* Clear queued flag */
        Dpc->Type &= ~DPC_STATE_QUEUED;

        /* Get processor control block and update queue depth */
        Prcb = KeGetCurrentPrcb();
        if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0)
        {
            Prcb->DpcData[DPC_NORMAL].DpcQueueDepth--;
        }

        InQueue = TRUE;
        DPRINT("DPC %p removed from queue\n", Dpc);
    }

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    return InQueue;
}

/*
 * @brief Set target processor for DPC
 * @implemented
 *
 * Sets which processor the DPC should execute on.
 *
 * @param Dpc - Pointer to the DPC
 * @param Number - Processor number (0 = any, 1-N = specific)
 */
VOID
NTAPI
KeSetTargetProcessorDpc(
    IN PKDPC Dpc,
    IN CCHAR Number)
{
    /* Validate processor number */
    if (Number < 0 || Number > MAXIMUM_PROCESSORS)
    {
        DPRINT1("Invalid processor number %d for DPC\n", Number);
        return;
    }

    /* Set target processor */
    Dpc->Number = Number;

    DPRINT("DPC %p targeted to processor %d\n", Dpc, Number);
}

/*
 * @brief Set DPC importance
 * @implemented
 *
 * Sets the importance level of a DPC which affects queue priority.
 *
 * @param Dpc - Pointer to the DPC
 * @param Importance - Importance level
 */
VOID
NTAPI
KeSetImportanceDpc(
    IN PKDPC Dpc,
    IN KDPC_IMPORTANCE Importance)
{
    /* Validate importance level */
    if (Importance > HighImportance)
    {
        DPRINT1("Invalid importance %d for DPC\n", Importance);
        return;
    }

    /* Set importance */
    Dpc->Importance = (UCHAR)Importance;

    DPRINT("DPC %p importance set to %d\n", Dpc, Importance);
}

/*
 * @brief Process the DPC queue
 * @implemented
 *
 * Called at DISPATCH_LEVEL to process queued DPCs.
 * This is typically called from the DPC software interrupt handler.
 */
VOID
NTAPI
KiRetireDpcList(
    IN PKPRCB Prcb)
{
    PLIST_ENTRY ListHead, Entry;
    PKDPC Dpc;
    PKDEFERRED_ROUTINE DeferredRoutine;
    PVOID DeferredContext, SystemArgument1, SystemArgument2;
    ULONG Count = 0;

    DPRINT("Processing DPC queue on CPU %u\n", KeGetCurrentProcessorNumber());

    /* Process normal DPC queue */
    ListHead = &Prcb->DpcData[DPC_NORMAL].DpcListHead;

    while (!IsListEmpty(ListHead))
    {
        /* Get next DPC from queue */
        Entry = RemoveHeadList(ListHead);
        Dpc = CONTAINING_RECORD(Entry, KDPC, DpcListEntry);

        /* Update queue depth */
        if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0)
        {
            Prcb->DpcData[DPC_NORMAL].DpcQueueDepth--;
        }

        /* Clear queued flag and set running flag */
        Dpc->Type = (UCHAR)((Dpc->Type & ~DPC_STATE_QUEUED) | DPC_STATE_RUNNING);

        /* Save DPC parameters */
        DeferredRoutine = Dpc->DeferredRoutine;
        DeferredContext = Dpc->DeferredContext;
        SystemArgument1 = Dpc->SystemArgument1;
        SystemArgument2 = Dpc->SystemArgument2;

        /* Clear running flag */
        Dpc->Type &= ~DPC_STATE_RUNNING;

        /* Release DPC lock (enable interrupts) */
        _enable();

        /* Call DPC routine */
        DPRINT("Calling DPC routine %p for DPC %p\n", DeferredRoutine, Dpc);
        DeferredRoutine(Dpc, DeferredContext, SystemArgument1, SystemArgument2);

        /* Reacquire DPC lock (disable interrupts) */
        _disable();

        Count++;

        /* Check if we've processed too many DPCs */
        if (Count >= KiMaximumDpcQueueDepth)
        {
            /* Yield to allow other processing */
            DPRINT("DPC quota reached, yielding\n");
            break;
        }
    }

    /* Clear DPC interrupt flag */
    Prcb->DpcInterruptRequested = FALSE;

    DPRINT("Processed %lu DPCs on CPU %u\n", Count, KeGetCurrentProcessorNumber());
}

/*
 * @brief DPC software interrupt handler
 * @implemented
 *
 * Handles the software interrupt for DPC processing.
 * This runs at DISPATCH_LEVEL.
 */
VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb;
    KIRQL OldIrql;

    /* Get current processor control block */
    Prcb = KeGetCurrentPrcb();

    DPRINT("DPC interrupt on CPU %u\n", KeGetCurrentProcessorNumber());

    /* Ensure we're at DISPATCH_LEVEL */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    }

    /* Process DPC queue if not empty */
    if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0 ||
        Prcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)
    {
        /* Process the DPC list */
        KiRetireDpcList(Prcb);
    }

    /* Check for pending timer expiration DPC
     * TODO: Process timer DPCs */

    /* Lower IRQL if we raised it */
    if (OldIrql < DISPATCH_LEVEL)
    {
        KeLowerIrql(OldIrql);
    }
}

/*
 * @brief Initialize DPC system
 * @implemented
 *
 * Initializes the DPC subsystem during kernel initialization.
 */
VOID
NTAPI
KiInitializeDpcSystem(VOID)
{
    PKPRCB Prcb;
    ULONG i;

    DPRINT("Initializing ARM64 DPC system\n");

    /* Initialize DPC data for each processor */
    for (i = 0; i < KeNumberProcessors; i++)
    {
        Prcb = KiProcessorBlock[i];
        if (!Prcb) continue;

        /* Initialize normal DPC queue */
        InitializeListHead(&Prcb->DpcData[DPC_NORMAL].DpcListHead);
        Prcb->DpcData[DPC_NORMAL].DpcQueueDepth = 0;
        Prcb->DpcData[DPC_NORMAL].DpcCount = 0;
        KeInitializeSpinLock(&Prcb->DpcData[DPC_NORMAL].DpcLock);

        /* Initialize threaded DPC queue */
        InitializeListHead(&Prcb->DpcData[DPC_THREADED].DpcListHead);
        Prcb->DpcData[DPC_THREADED].DpcQueueDepth = 0;
        Prcb->DpcData[DPC_THREADED].DpcCount = 0;
        KeInitializeSpinLock(&Prcb->DpcData[DPC_THREADED].DpcLock);

        /* Clear DPC interrupt flag */
        Prcb->DpcInterruptRequested = FALSE;
    }

    /* Initialize global timer expiration DPC */
    KeInitializeDpc(&KiTimerExpireDpc, KiTimerExpiration, NULL);
    KeSetTargetProcessorDpc(&KiTimerExpireDpc, 0);
    KeSetImportanceDpc(&KiTimerExpireDpc, HighImportance);

    DPRINT("ARM64 DPC system initialized\n");
}

/*
 * @brief Check if running at DPC level
 * @implemented
 *
 * Returns TRUE if currently executing at DISPATCH_LEVEL or higher.
 *
 * @return TRUE if at DPC level, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsExecutingDpc(VOID)
{
    PKPRCB Prcb;

    /* Get current processor control block */
    Prcb = KeGetCurrentPrcb();

    /* Check if we're in a DPC */
    return (KeGetCurrentIrql() >= DISPATCH_LEVEL) ||
           (Prcb->DpcRoutineActive);
}

/*
 * @brief Flush DPC queue
 * @implemented
 *
 * Forces immediate processing of all queued DPCs.
 */
VOID
NTAPI
KeFlushQueuedDpcs(VOID)
{
    KIRQL OldIrql;
    PKPRCB Prcb;

    DPRINT("Flushing DPC queue\n");

    /* Raise to DISPATCH_LEVEL */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Get current processor control block */
    Prcb = KeGetCurrentPrcb();

    /* Process all queued DPCs */
    while (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0 ||
           Prcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)
    {
        KiRetireDpcList(Prcb);
    }

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    DPRINT("DPC queue flushed\n");
}

/* EOF */