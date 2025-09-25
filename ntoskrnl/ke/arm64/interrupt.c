/*
 * Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Kernel
 * PURPOSE:         ARM64 Interrupt Management
 * FILE:            ntoskrnl/ke/arm64/interrupt.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

/* ARM64 Generic Interrupt Controller (GIC) state */
PVOID KiGicDistributorBase = NULL;
PVOID KiGicRedistributorBase = NULL;
PVOID KiGicCpuInterfaceBase = NULL;

/* Interrupt vector table */
PVOID KiInterruptHandlerTable[256];

/* FUNCTIONS *****************************************************************/

/*
 * @brief Raise the processor's IRQL level
 *
 * @param NewIrql - The new IRQL to set
 * @return Previous IRQL level
 */
KIRQL
NTAPI
_KfRaiseIrql(IN KIRQL NewIrql)
{
    KIRQL OldIrql;

    /* Get current IRQL from PCR */
    OldIrql = KeGetPcr()->CurrentIrql;

    /* Check for valid raise */
    ASSERT(OldIrql <= NewIrql);

    /* Set new IRQL */
    KeGetPcr()->CurrentIrql = NewIrql;

    /* TODO: Configure GIC priority masking for this IRQL */

    return OldIrql;
}

/*
 * @brief Lower the processor's IRQL level
 *
 * @param NewIrql - The new IRQL to set (must be lower than current)
 */
VOID
NTAPI
_KfLowerIrql(IN KIRQL NewIrql)
{
    KIRQL OldIrql;

    /* Get current IRQL from PCR */
    OldIrql = KeGetPcr()->CurrentIrql;

    /* Check for valid lower */
    ASSERT(OldIrql >= NewIrql);

    /* Set new IRQL */
    KeGetPcr()->CurrentIrql = NewIrql;

    /* TODO: Configure GIC priority masking for this IRQL */
    /* TODO: Check for pending software interrupts */
}

/* _KeGetPreviousMode is defined as a macro in ketypes.h for ARM64 */

/*
 * @brief Initialize the ARM64 interrupt controller (GICv3/v4)
 *
 * The Generic Interrupt Controller (GIC) manages interrupts in ARM64 systems.
 * GICv3 introduced system register interface and support for more CPUs.
 *
 * @param LoaderBlock - Loader parameter block with GIC addresses
 * @return VOID
 */
VOID
NTAPI
KiInitializeInterruptController(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    DPRINT1("KiInitializeInterruptController: ARM64 GIC init stub\n");

    /* TODO: Initialize GIC Distributor (GICD) */
    /* GICD controls:
     * - Interrupt routing to CPUs
     * - Interrupt priorities
     * - Interrupt enable/disable
     * - Interrupt configuration (edge/level)
     */

    /* TODO: Initialize GIC Redistributor (GICR) */
    /* GICR handles:
     * - Per-CPU interrupt configuration
     * - SGIs (Software Generated Interrupts)
     * - PPIs (Private Peripheral Interrupts)
     */

    /* TODO: Initialize GIC CPU Interface */
    /* System register interface (ICC_*_EL1):
     * - ICC_PMR_EL1: Priority Mask
     * - ICC_IAR1_EL1: Interrupt Acknowledge
     * - ICC_EOIR1_EL1: End of Interrupt
     * - ICC_SRE_EL1: System Register Enable
     */

    /* TODO: Set default interrupt priorities */
    /* Priority levels 0-255, lower is higher priority */

    /* TODO: Enable interrupt groups */
    /* Group 0: Secure interrupts (FIQ)
     * Group 1: Non-secure interrupts (IRQ)
     */
}

/*
 * @brief Enable interrupts on current processor
 *
 * ARM64 uses DAIF register to control interrupts.
 * D - Debug exceptions
 * A - SError (System Error)
 * I - IRQ (Normal interrupts)
 * F - FIQ (Fast interrupts)
 *
 * @return BOOLEAN - Previous interrupt state
 */
BOOLEAN
NTAPI
KiEnableInterrupts(VOID)
{
    DPRINT1("KiEnableInterrupts: ARM64 stub\n");

    /* TODO: Read current DAIF state */
    /* MRS X0, DAIF */
    /* ULONG64 OldDaif = __readdaif(); */

    /* TODO: Clear interrupt mask bits */
    /* MSR DAIFClr, #0x2  ; Clear I bit to enable IRQ */

    /* Return whether interrupts were previously enabled */
    /* return (OldDaif & 0x80) == 0; */
    return FALSE; /* Stub: assume interrupts were disabled */
}

/*
 * @brief Disable interrupts on current processor
 *
 * @return BOOLEAN - Previous interrupt state
 */
BOOLEAN
NTAPI
KiDisableInterrupts(VOID)
{
    DPRINT1("KiDisableInterrupts: ARM64 stub\n");

    /* TODO: Read current DAIF state */
    /* MRS X0, DAIF */
    /* ULONG64 OldDaif = __readdaif(); */

    /* TODO: Set interrupt mask bits */
    /* MSR DAIFSet, #0x2  ; Set I bit to disable IRQ */

    /* Return whether interrupts were previously enabled */
    /* return (OldDaif & 0x80) == 0; */
    return FALSE; /* Stub: assume interrupts were disabled */
}

/* KeConnectInterrupt is implemented in irqobj.c */

/* KiInterruptHandler is the assembly entry point defined in ke.h
 * KiInterruptHandlerC is implemented in except.c for ARM64 */

/*
 * @brief Send Inter-Processor Interrupt (IPI)
 *
 * ARM64 uses SGIs (Software Generated Interrupts) for IPIs.
 *
 * @param TargetProcessors - Bitmap of target processors
 * @param Vector - IPI vector (SGI 0-15)
 * @return VOID
 */
VOID
NTAPI
KiSendIpi(
    IN KAFFINITY TargetProcessors,
    IN ULONG Vector)
{
    DPRINT1("KiSendIpi: Targets 0x%llx, Vector %u - ARM64 stub\n",
            TargetProcessors, Vector);

    /* SGIs are vectors 0-15 in ARM64 */
    if (Vector >= 16)
    {
        DPRINT1("Invalid SGI vector %u\n", Vector);
        return;
    }

    /* TODO: Generate SGI via system register */
    /* ICC_SGI1R_EL1 format:
     * Bits 47:44 - Target List (Aff3)
     * Bits 39:32 - Target List (Aff2)
     * Bits 23:16 - Target List (Aff1)
     * Bits 15:0  - Target List (each bit = one CPU)
     * Bits 27:24 - INTID (SGI number)
     * Bit 40     - IRM (Interrupt Routing Mode)
     */

    /* TODO: Calculate affinity routing from processor mask */
    /* ULONG64 SgiValue = ...; */
    /* __writeiccreg(ICC_SGI1R_EL1, SgiValue); */
}

/*
 * @brief Initialize timer interrupt
 *
 * ARM64 has several timer sources:
 * - Generic Timer (architected timer)
 * - Platform-specific timers
 *
 * @param Frequency - Timer frequency in Hz
 * @return VOID
 */
VOID
NTAPI
KiInitializeTimer(
    IN ULONG Frequency)
{
    DPRINT1("KiInitializeTimer: Frequency %u Hz - ARM64 stub\n", Frequency);

    /* TODO: Read timer frequency from system register */
    /* ULONG64 CntFrq = __readcntreg(CNTFRQ_EL0); */

    /* TODO: Configure timer control register */
    /* CNTKCTL_EL1 controls:
     * - EL0 access to timers
     * - Event stream generation
     */
    /* ULONG64 CntkCtl = ...; */

    /* TODO: Set timer compare value */
    /* CNTP_CVAL_EL0: Physical timer compare value
     * CNTV_CVAL_EL0: Virtual timer compare value
     */

    /* TODO: Enable timer interrupt */
    /* CNTP_CTL_EL0: Physical timer control
     *   Bit 0 - ENABLE
     *   Bit 1 - IMASK (interrupt mask)
     *   Bit 2 - ISTATUS (interrupt status)
     */
}