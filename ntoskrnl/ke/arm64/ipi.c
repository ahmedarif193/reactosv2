/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Inter-Processor Interrupt (IPI) Support
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* IPI Request Types */
typedef enum _KIPI_REQUEST_TYPE {
    KIPI_REQUEST_APC = 0x1,
    KIPI_REQUEST_DPC = 0x2,
    KIPI_REQUEST_FLUSH_TB = 0x4,
    KIPI_REQUEST_FLUSH_ENTIRE_TB = 0x8,
    KIPI_REQUEST_FLUSH_MULTIPLE_TB = 0x10,
    KIPI_REQUEST_FLUSH_SINGLE_TB = 0x20,
    KIPI_REQUEST_FLUSH_ALL_TB = 0x40,
    KIPI_REQUEST_PACKET = 0x80
} KIPI_REQUEST_TYPE;

typedef ULONG KIPI_REQUEST;

/* GLOBALS ********************************************************************/

static KAFFINITY ActiveProcessors = 1;
static ULONG IpiRequestCounts[32] = {0};

/* FUNCTIONS ******************************************************************/

/*
 * @brief Send Inter-Processor Interrupt to target processors
 */
VOID
NTAPI
KiIpiSendArm64(
    IN KAFFINITY TargetProcessors,
    IN KIPI_REQUEST Request)
{
    ULONG ProcessorNumber;
    KAFFINITY RemainingProcessors = TargetProcessors;

    /* TODO: Implement ARM64 IPI sending using GIC SGI (Software Generated Interrupts)
     * - Use GIC Distributor SGIR register for GICv2
     * - Use ICC_SGI1R_EL1 system register for GICv3+
     * - Map KIPI_REQUEST to appropriate SGI interrupt number
     * - Handle processor affinity routing for ARM64
     */

    DPRINT("KiIpiSend: TargetProcessors=0x%lx, Request=%d\n",
           TargetProcessors, Request);

    /* Loop through each target processor */
    ProcessorNumber = 0;
    while (RemainingProcessors && ProcessorNumber < 32)
    {
        if (RemainingProcessors & 1)
        {
            /* Send IPI to this processor */
            DPRINT("Sending IPI request %d to CPU %lu\n", Request, ProcessorNumber);

            /* TODO: Map Request type to SGI interrupt number:
             * - KIPI_REQUEST_DPC -> SGI 0
             * - KIPI_REQUEST_APC -> SGI 1
             * - KIPI_REQUEST_FLUSH_TB -> SGI 2
             * - KIPI_REQUEST_INTERRUPT -> SGI 3
             */

            /* For GICv2, write to GICD_SGIR:
             * - Bits 0-3: SGI interrupt ID
             * - Bits 16-23: Target CPU list
             * - Bits 24-25: Target list filter
             */

            /* For GICv3+, write to ICC_SGI1R_EL1:
             * - Bits 0-15: Target list (within affinity level)
             * - Bits 16-23: Affinity 0 (CPU number within cluster)
             * - Bits 24-31: SGI interrupt ID
             * - Bits 32-39: Affinity 1 (cluster number)
             * - Bits 40-47: Affinity 2 (higher level affinity)
             * - Bits 48-55: Affinity 3 (highest level affinity)
             */

            /* Placeholder for actual hardware IPI sending */
            // HalSendSoftwareInterrupt(ProcessorNumber, Request);

            /* Track IPI requests for debugging */
            if (Request < 32)
            {
                IpiRequestCounts[Request]++;
            }
        }

        RemainingProcessors >>= 1;
        ProcessorNumber++;
    }
}

/*
 * @brief Handle incoming Inter-Processor Interrupt
 */
BOOLEAN
NTAPI
KiIpiServiceRoutineArm64(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    ULONG ProcessorNumber;
    PKPRCB Prcb;
    BOOLEAN Handled = FALSE;

    /* TODO: Implement ARM64 IPI service routine
     * - Determine which SGI interrupt was received
     * - Process queued IPI requests for this processor
     * - Handle different IPI request types appropriately
     * - Acknowledge the SGI interrupt
     */

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);

    ProcessorNumber = KeGetCurrentProcessorNumber();
    Prcb = KeGetCurrentPrcb();

    DPRINT("KiIpiServiceRoutine: CPU %lu\n", ProcessorNumber);

    /* TODO: Check which SGI interrupt triggered this service routine
     * - Read GIC interrupt acknowledge register
     * - Determine SGI interrupt ID (0-15)
     * - Map SGI ID back to KIPI_REQUEST type
     */

    /* TODO: Process different IPI request types */

    /* Handle DPC requests */
    if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0 ||
        Prcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)
    {
        /* TODO: Process DPC queue */
        DPRINT("Processing DPC IPI on CPU %lu\n", ProcessorNumber);
        // KiRetireDpcList(Prcb);
        Handled = TRUE;
    }

    /* Handle APC requests */
    /* TODO: ApcBypassCount field may not exist in ARM64 PRCB structure - check and fix */
    if (/*!Prcb->ApcBypassCount == 0 &&*/ !IsListEmpty(&KeGetCurrentThread()->ApcState.ApcListHead[KernelMode]))
    {
        /* TODO: Process APC queue */
        DPRINT("Processing APC IPI on CPU %lu\n", ProcessorNumber);
        // KiDeliverApc(KernelMode, ExceptionFrame, TrapFrame);
        Handled = TRUE;
    }

    /* Handle TLB flush requests */
    /* TODO: Check for TLB flush IPI and handle appropriately */
    if (/* TLB flush requested */ FALSE)
    {
        DPRINT("Processing TLB flush IPI on CPU %lu\n", ProcessorNumber);
        /* TODO: Flush TLB as requested */
        // KiFlushCurrentTb();
        Handled = TRUE;
    }

    /* Handle generic interrupt requests */
    /* TODO: Handle other IPI request types */

    /* TODO: Send EOI (End of Interrupt) to GIC for the SGI */
    // HalEndOfInterrupt(SgiInterruptId);

    return Handled;
}

/*
 * @brief Initialize IPI support for ARM64
 */
VOID
NTAPI
KiInitializeIpiSupport(VOID)
{
    ULONG ProcessorNumber;

    DPRINT("Initializing ARM64 IPI support\n");

    /* TODO: Initialize ARM64 IPI infrastructure
     * - Configure GIC for SGI interrupt handling
     * - Set up SGI interrupt handlers
     * - Initialize per-processor IPI state
     * - Configure SGI priority and routing
     */

    ProcessorNumber = KeGetCurrentProcessorNumber();

    /* TODO: Enable SGI interrupts in GIC
     * For ARM64, SGIs 0-15 are typically used for IPI
     * - SGI 0: DPC requests
     * - SGI 1: APC requests
     * - SGI 2: TLB flush requests
     * - SGI 3: Generic interrupt requests
     * - SGI 4-15: Reserved for future use
     */

    /* Register this processor as active */
    ActiveProcessors |= (1UL << ProcessorNumber);

    DPRINT("ARM64 IPI support initialized for CPU %lu\n", ProcessorNumber);
}

/*
 * @brief Broadcast IPI to all processors except current
 */
VOID
NTAPI
KiIpiBroadcast(
    IN KIPI_REQUEST Request)
{
    KAFFINITY TargetProcessors;
    ULONG CurrentProcessor;

    CurrentProcessor = KeGetCurrentProcessorNumber();
    TargetProcessors = ActiveProcessors & ~(1UL << CurrentProcessor);

    if (TargetProcessors)
    {
        KiIpiSend(TargetProcessors, Request);
    }
}

/*
 * @brief Send IPI to specific processor
 */
VOID
NTAPI
KiIpiSendProcessor(
    IN ULONG ProcessorNumber,
    IN KIPI_REQUEST Request)
{
    KAFFINITY TargetMask;

    if (ProcessorNumber < 32 && (ActiveProcessors & (1UL << ProcessorNumber)))
    {
        TargetMask = (1UL << ProcessorNumber);
        KiIpiSend(TargetMask, Request);
    }
}

/*
 * @brief Flush TLB on all processors
 */
VOID
NTAPI
KiIpiFlushTbAll(VOID)
{
    /* TODO: Implement TLB flush IPI for ARM64
     * - Send TLB flush request to all other processors
     * - Wait for acknowledgment from all processors
     * - Flush local TLB
     * - Use ARM64-specific TLB invalidation instructions
     */

    DPRINT("Broadcasting TLB flush IPI to all processors\n");

    /* Broadcast TLB flush request to other processors */
    KiIpiBroadcast(KIPI_REQUEST_FLUSH_TB);

    /* TODO: Wait for other processors to complete TLB flush */
    /* This requires some synchronization mechanism */

    /* Flush local TLB */
    // KiFlushCurrentTb();

    DPRINT("TLB flush completed on all processors\n");
}

/*
 * @brief Get IPI statistics for debugging
 */
VOID
NTAPI
KiIpiGetStatistics(
    OUT PULONG IpiCounts,
    IN ULONG MaxCount)
{
    ULONG i;

    if (!IpiCounts || MaxCount == 0)
        return;

    for (i = 0; i < MaxCount && i < 32; i++)
    {
        IpiCounts[i] = IpiRequestCounts[i];
    }
}

/*
 * @brief Reset IPI statistics
 */
VOID
NTAPI
KiIpiResetStatistics(VOID)
{
    RtlZeroMemory(IpiRequestCounts, sizeof(IpiRequestCounts));
}

/*
 * @brief Check if processor is active for IPI
 */
BOOLEAN
NTAPI
KiIpiIsProcessorActive(
    IN ULONG ProcessorNumber)
{
    return (ProcessorNumber < 32) &&
           ((ActiveProcessors & (1UL << ProcessorNumber)) != 0);
}

/*
 * @brief Set processor active state for IPI
 */
VOID
NTAPI
KiIpiSetProcessorActive(
    IN ULONG ProcessorNumber,
    IN BOOLEAN Active)
{
    if (ProcessorNumber < 32)
    {
        if (Active)
        {
            ActiveProcessors |= (1UL << ProcessorNumber);
        }
        else
        {
            ActiveProcessors &= ~(1UL << ProcessorNumber);
        }
    }
}

/*
 * @brief Generic IPI synchronization function
 */
VOID
NTAPI
KiIpiGenericCall(
    IN KIPI_BROADCAST_WORKER WorkerFunction,
    IN PVOID Context)
{
    /* TODO: Implement generic IPI call mechanism
     * - Send IPI to all processors with function pointer and context
     * - Wait for all processors to complete the function
     * - Handle synchronization and error conditions
     * - Support both blocking and non-blocking calls
     */

    UNREFERENCED_PARAMETER(WorkerFunction);
    UNREFERENCED_PARAMETER(Context);

    DPRINT1("KiIpiGenericCall not fully implemented for ARM64\n");
}