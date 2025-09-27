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

/* MACROS *******************************************************************/

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* GLOBALS ******************************************************************/

/* ARM64 Generic Interrupt Controller (GIC) state */
PVOID KiGicDistributorBase = NULL;
PVOID KiGicRedistributorBase = NULL;
PVOID KiGicCpuInterfaceBase = NULL;

/* Runtime flag: enable sysreg-based priority masking (GICv3). */
static volatile BOOLEAN KiSysRegIrqMaskingEnabled = FALSE;
/* Runtime flag: enable sysreg-based SGIs (GICv3). */
static volatile BOOLEAN KiSysRegSgiEnabled = FALSE;

/* Interrupt vector table */
PVOID KiInterruptHandlerTable[256];

/* Software interrupt vectors are defined in ketypes.h for ARM64 */

/* ARM64 DAIF register bit definitions */
#define DAIF_DEBUG_MASK     0x200   /* D bit - Debug exceptions */
#define DAIF_SERROR_MASK    0x100   /* A bit - SError (System Error) */
#define DAIF_IRQ_MASK       0x080   /* I bit - IRQ (Normal interrupts) at bit 7 */
#define DAIF_FIQ_MASK       0x040   /* F bit - FIQ (Fast interrupts) */

/* GIC Priority Register definitions */
#define GIC_MIN_PRIORITY    0xFF    /* Lowest priority (all interrupts masked) */
#define GIC_MAX_PRIORITY    0x00    /* Highest priority (no interrupts masked) */

/* IRQL to GIC Priority mapping table
 * Lower GIC priority values = higher actual priority
 * Higher IRQL values = higher priority (fewer interrupts allowed)
 */
static const UCHAR KiIrqlToGicPriority[16] = {
    0xF0,   /* PASSIVE_LEVEL (0) - Allow all interrupts */
    0xE0,   /* APC_LEVEL (1) */
    0xD0,   /* DISPATCH_LEVEL (2) */
    0xC0,   /* IRQL 3 */
    0xB0,   /* IRQL 4 */
    0xA0,   /* IRQL 5 */
    0x90,   /* IRQL 6 */
    0x80,   /* IRQL 7 */
    0x70,   /* IRQL 8 */
    0x60,   /* IRQL 9 */
    0x50,   /* IRQL 10 */
    0x40,   /* IRQL 11 */
    0x30,   /* IRQL 12 */
    0x20,   /* IRQL 13 */
    0x10,   /* PROFILE_LEVEL (14) */
    0x00    /* HIGH_LEVEL (15) - Block all maskable interrupts */
};

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

    /* Configure GIC priority masking for this IRQL */
    if (KiSysRegIrqMaskingEnabled && NewIrql < ARRAY_SIZE(KiIrqlToGicPriority))
    {
        UCHAR GicPriority = KiIrqlToGicPriority[NewIrql];
        __asm__ volatile("msr icc_pmr_el1, %0" :: "r"((ULONG64)GicPriority));
        __isb();
    }

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

    /* Configure GIC priority masking for this IRQL */
    if (KiSysRegIrqMaskingEnabled && NewIrql < ARRAY_SIZE(KiIrqlToGicPriority))
    {
        UCHAR GicPriority = KiIrqlToGicPriority[NewIrql];
        __asm__ volatile("msr icc_pmr_el1, %0" :: "r"((ULONG64)GicPriority));
        __isb();
    }

    /* Check for pending software interrupts at lower IRQL */
    if (NewIrql < DISPATCH_LEVEL)
    {
        /* Check for pending DPCs */
        PKPRCB Prcb = KeGetCurrentPrcb();
        if (Prcb->DpcInterruptRequested)
        {
            /* Request DPC software interrupt */
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
    }
    if (NewIrql < APC_LEVEL)
    {
        /* Check for pending APCs */
        PKTHREAD Thread = KeGetCurrentThread();
        if (Thread && (Thread->ApcState.KernelApcPending || Thread->ApcState.UserApcPending))
        {
            /* Request APC software interrupt */
            HalRequestSoftwareInterrupt(APC_LEVEL);
        }
    }
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
 * @brief Enable interrupts on current processor (_KfEnable function)
 *
 * ARM64 uses DAIF register to control interrupts.
 * D - Debug exceptions (bit 9)
 * A - SError (System Error) (bit 8)
 * I - IRQ (Normal interrupts) (bit 7) - THIS IS THE CORRECT BIT POSITION
 * F - FIQ (Fast interrupts) (bit 6)
 *
 * @return BOOLEAN - Previous interrupt state (TRUE if interrupts were enabled)
 */
BOOLEAN
NTAPI
_KfEnable(VOID)
{
    ULONG64 OldDaif;

    /* Read current DAIF state */
    __asm__ volatile("mrs %0, daif" : "=r" (OldDaif));

    /* Clear IRQ mask bit to enable interrupts (bit 7 = 0x80) */
    __asm__ volatile("msr daifclr, #0x2" ::: "memory");

    /* Return TRUE if interrupts were previously enabled (I bit was clear) */
    return (OldDaif & DAIF_IRQ_MASK) == 0;
}

/*
 * @brief Enable interrupts on current processor (legacy name)
 *
 * @return BOOLEAN - Previous interrupt state
 */
BOOLEAN
NTAPI
KiEnableInterrupts(VOID)
{
    return _KfEnable();
}

/*
 * @brief Disable interrupts on current processor (_KfDisable function)
 *
 * @return BOOLEAN - Previous interrupt state (TRUE if interrupts were enabled)
 */
BOOLEAN
NTAPI
_KfDisable(VOID)
{
    ULONG64 OldDaif;

    /* Read current DAIF state */
    __asm__ volatile("mrs %0, daif" : "=r" (OldDaif));

    /* Set IRQ mask bit to disable interrupts (bit 7 = 0x80) */
    __asm__ volatile("msr daifset, #0x2" ::: "memory");

    /* Return TRUE if interrupts were previously enabled (I bit was clear) */
    return (OldDaif & DAIF_IRQ_MASK) == 0;
}

/*
 * @brief Disable interrupts on current processor (legacy name)
 *
 * @return BOOLEAN - Previous interrupt state
 */
BOOLEAN
NTAPI
KiDisableInterrupts(VOID)
{
    return _KfDisable();
}

/* KeConnectInterrupt is implemented in irqobj.c */

/* KiInterruptHandler is the assembly entry point defined in ke.h
 * KiInterruptHandlerC is implemented in except.c for ARM64 */

/*
 * @brief Send Inter-Processor Interrupt (IPI)
 *
 * ARM64 uses SGIs (Software Generated Interrupts) for IPIs.
 * This implementation uses GICv3 system register interface.
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
    ULONG64 SgiValue;
    ULONG ProcessorIndex;
    ULONG64 TargetList = 0;

    DPRINT("KiSendIpi: Targets 0x%llx, Vector %u\n", TargetProcessors, Vector);

    /* Validate SGI vector range */
    if (Vector >= 16)
    {
        DPRINT1("Invalid SGI vector %u (must be 0-15)\n", Vector);
        return;
    }

    if (TargetProcessors == 0)
    {
        DPRINT1("No target processors specified\n");
        return;
    }

    /* Convert processor affinity mask to target list */
    for (ProcessorIndex = 0; ProcessorIndex < MAXIMUM_PROCESSORS; ProcessorIndex++)
    {
        if (TargetProcessors & (1ULL << ProcessorIndex))
        {
            /* For simplicity, assume all processors are in same cluster (Aff1=0, Aff2=0, Aff3=0) */
            TargetList |= (1ULL << ProcessorIndex);
        }
    }

    if (TargetList == 0)
    {
        DPRINT1("No valid target processors in affinity mask\n");
        return;
    }

    /* Build ICC_SGI1R_EL1 register value
     * Format:
     * Bits 55:48 - Aff3 (cluster level 3)
     * Bits 39:32 - Aff2 (cluster level 2)
     * Bit 40     - IRM (Interrupt Routing Mode) - 0 for target list
     * Bits 31:24 - Aff1 (cluster level 1)
     * Bits 27:24 - INTID (SGI number)
     * Bits 15:0  - TargetList (bitmap of target PEs)
     */
    SgiValue = ((ULONG64)Vector << 24) |  /* SGI number in bits 27:24 */
               (TargetList & 0xFFFF);     /* Target list in bits 15:0 */

    if (KiSysRegSgiEnabled)
    {
        /* Send SGI via system register */
        __asm__ volatile(
            "msr icc_sgi1r_el1, %0\n"
            "isb\n"
            ::
            "r" (SgiValue)
            : "memory");
    }
    else
    {
        /* No system-register SGIs available yet; ignore for UP early boot. */
        return;
    }

    DPRINT("SGI sent: Value=0x%llx, Vector=%u, Targets=0x%llx\n",
           SgiValue, Vector, TargetProcessors);
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
    ULONG64 CntFrq, CntkCtl;

    DPRINT("KiInitializeTimer: Requested frequency %u Hz\n", Frequency);

    /* Read timer frequency from system register */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (CntFrq));
    DPRINT("ARM64 timer frequency: %llu Hz\n", CntFrq);

    /* Configure timer control register for EL1 */
    __asm__ volatile("mrs %0, cntkctl_el1" : "=r" (CntkCtl));

    /* Enable EL0 access to physical and virtual timers */
    CntkCtl |= (1 << 0) | (1 << 1);  /* EL0PTEN | EL0VTEN */

    __asm__ volatile("msr cntkctl_el1, %0" :: "r" (CntkCtl));

    /* Initialize timer compare value to prevent immediate interrupt */
    ULONG64 CurrentCount;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (CurrentCount));

    /* Set initial compare value 1 second in the future */
    ULONG64 CompareValue = CurrentCount + CntFrq;
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r" (CompareValue));

    /* Enable physical timer but keep interrupt masked initially */
    ULONG64 TimerCtl = 1; /* ENABLE = 1, IMASK = 0, ISTATUS = 0 */
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r" (TimerCtl));

    /* Memory barrier to ensure timer setup is complete */
    __asm__ volatile("isb");

    DPRINT("ARM64 timer initialized: Compare=0x%llx, Control=0x%llx\n",
           CompareValue, TimerCtl);
}
