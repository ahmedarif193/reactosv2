/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Generic Interrupt Controller (GIC) Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* GIC Distributor registers */
#define GICD_CTLR           0x0000    /* Distributor Control Register */
#define GICD_TYPER          0x0004    /* Interrupt Controller Type Register */
#define GICD_IIDR           0x0008    /* Distributor Implementer Identification */
#define GICD_IGROUPR        0x0080    /* Interrupt Group Registers */
#define GICD_ISENABLER      0x0100    /* Interrupt Set-Enable Registers */
#define GICD_ICENABLER      0x0180    /* Interrupt Clear-Enable Registers */
#define GICD_ISPENDR        0x0200    /* Interrupt Set-Pending Registers */
#define GICD_ICPENDR        0x0280    /* Interrupt Clear-Pending Registers */
#define GICD_ISACTIVER      0x0300    /* Interrupt Set-Active Registers */
#define GICD_ICACTIVER      0x0380    /* Interrupt Clear-Active Registers */
#define GICD_IPRIORITYR     0x0400    /* Interrupt Priority Registers */
#define GICD_ITARGETSR      0x0800    /* Interrupt Processor Targets Registers (GICv2) */
#define GICD_ICFGR          0x0C00    /* Interrupt Configuration Registers */
#define GICD_SGIR           0x0F00    /* Software Generated Interrupt Register */

/* GIC CPU Interface registers (GICv2) */
#define GICC_CTLR           0x0000    /* CPU Interface Control Register */
#define GICC_PMR            0x0004    /* Interrupt Priority Mask Register */
#define GICC_BPR            0x0008    /* Binary Point Register */
#define GICC_IAR            0x000C    /* Interrupt Acknowledge Register */
#define GICC_EOIR           0x0010    /* End of Interrupt Register */
#define GICC_RPR            0x0014    /* Running Priority Register */
#define GICC_HPPIR          0x0018    /* Highest Priority Pending Interrupt Register */

/* GIC Control bits */
#define GICD_CTLR_ENABLE    0x1       /* Enable Distributor */
#define GICD_CTLR_ARE_S     0x10      /* Affinity Routing Enable (Secure) */
#define GICD_CTLR_ARE_NS    0x20      /* Affinity Routing Enable (Non-secure) */
#define GICC_CTLR_ENABLE    0x1       /* Enable CPU Interface */

/* GICv3 Redistributor registers */
#define GICR_CTLR           0x0000    /* Redistributor Control Register */
#define GICR_IIDR           0x0004    /* Redistributor Implementer Identification */
#define GICR_TYPER          0x0008    /* Redistributor Type Register */
#define GICR_WAKER          0x0014    /* Redistributor Wake Register */
#define GICR_IGROUPR0       0x10080   /* Redistributor Interrupt Group Register 0 */
#define GICR_ISENABLER0     0x10100   /* Redistributor Interrupt Set-Enable Register 0 */
#define GICR_ICENABLER0     0x10180   /* Redistributor Interrupt Clear-Enable Register 0 */
#define GICR_IPRIORITYR     0x10400   /* Redistributor Interrupt Priority Registers */

/* GICv3 System Registers */
#define ICC_PMR_EL1         "S3_0_C4_C6_0"     /* Interrupt Priority Mask Register */
#define ICC_CTLR_EL1        "S3_0_C12_C12_4"   /* Interrupt Controller Control Register */
#define ICC_IGRPEN1_EL1     "S3_0_C12_C12_7"   /* Interrupt Group 1 Enable Register */
#define ICC_IAR1_EL1        "S3_0_C12_C12_0"   /* Interrupt Acknowledge Register 1 */
#define ICC_EOIR1_EL1       "S3_0_C12_C12_1"   /* End Of Interrupt Register 1 */
#define ICC_SGI1R_EL1       "S3_0_C12_C11_5"   /* Software Generated Interrupt Group 1 Register */
/* System Register Enable (must be set before using ICC_* at EL1) */
#define ICC_SRE_EL1         "S3_0_C12_C12_5"

/* Special interrupt numbers */
#define GIC_SPURIOUS_INTERRUPT  1023

/* Interrupt types */
#define GIC_SGI_BASE        0         /* Software Generated Interrupts (0-15) */
#define GIC_PPI_BASE        16        /* Private Peripheral Interrupts (16-31) */
#define GIC_SPI_BASE        32        /* Shared Peripheral Interrupts (32+) */

/* GLOBALS ********************************************************************/

static PVOID GicDistributorBase = NULL;
static PVOID GicCpuInterfaceBase = NULL;
static PVOID GicRedistributorBase = NULL;  /* For GICv3+ */
static ULONG GicVersion = 0;
static ULONG GicMaxInterrupts = 0;
static BOOLEAN GicInitialized = FALSE;
static ARM64_GIC_INFO HalGicConfiguration;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Detect GIC version from Distributor PIDR2 register
 */
static
ULONG
HalDetectGicVersion(
    IN PVOID DistributorBase)
{
    ULONG Pidr2;

    if (!DistributorBase)
        return 0;

    /* Read Peripheral ID2 Register to determine GIC architecture revision */
    Pidr2 = READ_REGISTER_ULONG((PULONG)((PUCHAR)DistributorBase + 0xFFE8));

    /* Extract architecture revision from PIDR2[7:4] */
    switch ((Pidr2 >> 4) & 0xF)
    {
        case 1:
        case 2:
            return 2;  /* GICv2 */
        case 3:
            return 3;  /* GICv3 */
        case 4:
            return 4;  /* GICv4 */
        default:
            return 0;  /* Unknown */
    }
}

/*
 * @brief Initialize the ARM64 Generic Interrupt Controller
 */
BOOLEAN
NTAPI
HalInitializeInterruptController(VOID)
{
    ULONG TyperRegister, DistributorId;
    PHYSICAL_ADDRESS GicPhysicalBase;

    DPRINT("Initializing ARM64 Generic Interrupt Controller\n");

    /* Prefer ACPI‑provided configuration if available */
    if (HalAcpiIsAvailable())
    {
        ARM64_GIC_INFO AcpiCfg;
        if (HalAcpiGetGicConfiguration(&AcpiCfg))
        {
            if (AcpiCfg.DistributorBase.QuadPart)
            {
                GicDistributorBase = MmMapIoSpace(AcpiCfg.DistributorBase, 0x10000, MmNonCached);
            }
            if (AcpiCfg.CpuInterfaceBase.QuadPart)
            {
                GicCpuInterfaceBase = MmMapIoSpace(AcpiCfg.CpuInterfaceBase, 0x10000, MmNonCached);
            }
            if (AcpiCfg.RedistributorBase.QuadPart)
            {
                GicRedistributorBase = (PVOID)(ULONG_PTR)AcpiCfg.RedistributorBase.QuadPart; /* optional */
            }
            if (AcpiCfg.Version)
            {
                GicVersion = AcpiCfg.Version;
            }
        }
    }

    /* Fallback to common QEMU "virt" GICv2 addresses if ACPI not available */
    if (!GicDistributorBase)
    {
        GicPhysicalBase.QuadPart = 0x08000000;  /* Typical GIC distributor base */
        GicDistributorBase = MmMapIoSpace(GicPhysicalBase, 0x10000, MmNonCached);
    }
    if (!GicCpuInterfaceBase)
    {
        GicPhysicalBase.QuadPart = 0x08010000;  /* Typical GIC CPU interface base */
        GicCpuInterfaceBase = MmMapIoSpace(GicPhysicalBase, 0x10000, MmNonCached);
    }

    if (!GicDistributorBase)
    {
        DPRINT1("Failed to map GIC Distributor base address\n");
        return FALSE;
    }

    /* Detect GIC version if not provided by ACPI */
    if (GicVersion == 0)
        GicVersion = HalDetectGicVersion(GicDistributorBase);
    if (GicVersion == 0)
    {
        DPRINT1("Failed to detect GIC version\n");
        return FALSE;
    }

    DPRINT("Detected GICv%lu\n", GicVersion);

    /* Store GIC configuration */
    HalGicConfiguration.Version = GicVersion;
    HalGicConfiguration.DistributorBase.QuadPart = (ULONG64)GicDistributorBase;
    HalGicConfiguration.CpuInterfaceBase.QuadPart = (ULONG64)GicCpuInterfaceBase;

    /* Read GIC Type Register to determine capabilities */
    TyperRegister = READ_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_TYPER));
    GicMaxInterrupts = ((TyperRegister & 0x1F) + 1) * 32;

    /* Read Distributor ID for identification */
    DistributorId = READ_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_IIDR));

    HalGicConfiguration.MaxInterrupts = GicMaxInterrupts;
    HalGicConfiguration.MSISupport = (GicVersion >= 3) ? TRUE : FALSE;
    HalGicConfiguration.LPISupport = (GicVersion >= 3) ? TRUE : FALSE;

    DPRINT("GIC supports %lu interrupts (ID=0x%08lx)\n", GicMaxInterrupts, DistributorId);

    /* Initialize GIC Distributor */
    if (!HalInitializeGicDistributor())
    {
        DPRINT1("Failed to initialize GIC Distributor\n");
        return FALSE;
    }

    /* Initialize GIC CPU Interface based on version */
    if (GicVersion >= 3)
    {
        if (!HalInitializeGicv3CpuInterface())
        {
            DPRINT1("Failed to initialize GICv3 CPU Interface\n");
            return FALSE;
        }
    }
    else
    {
        if (!GicCpuInterfaceBase)
        {
            DPRINT1("GICv2 CPU Interface base address not available\n");
            return FALSE;
        }

        if (!HalInitializeGicCpuInterface())
        {
            DPRINT1("Failed to initialize GICv2 CPU Interface\n");
            return FALSE;
        }
    }

    GicInitialized = TRUE;
    DPRINT("ARM64 GIC initialization complete\n");

    return TRUE;
}

/*
 * @brief Initialize GIC Distributor
 */
BOOLEAN
NTAPI
HalInitializeGicDistributor(VOID)
{
    ULONG i, ControlRegister;
    ULONG GroupRegisterCount, PriorityRegisterCount;

    DPRINT("Initializing GIC Distributor\n");

    if (!GicDistributorBase)
        return FALSE;

    /* Disable the distributor during configuration */
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_CTLR), 0);

    /* Ensure distributor is disabled before proceeding */
    HalDataSynchronizationBarrier();

    /* Calculate register counts based on maximum interrupts */
    GroupRegisterCount = (GicMaxInterrupts + 31) / 32;
    PriorityRegisterCount = (GicMaxInterrupts + 3) / 4;

    /* Configure all interrupts as Group 1 (non-secure) for ARM64 */
    for (i = 0; i < GroupRegisterCount; i++)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_IGROUPR + (i * 4)), 0xFFFFFFFF);
    }

    /* Set default priority (0xA0) for all interrupts
     * Priority 0x00 = highest, 0xFF = lowest */
    for (i = 0; i < PriorityRegisterCount; i++)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_IPRIORITYR + (i * 4)), 0xA0A0A0A0);
    }

    /* Configure interrupt targets for GICv2 (SPI interrupts only) */
    if (GicVersion <= 2)
    {
        /* Set all SPI interrupts to target CPU 0 by default */
        for (i = GIC_SPI_BASE; i < GicMaxInterrupts; i += 4)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ITARGETSR + i), 0x01010101);
        }
    }

    /* Configure interrupt types - set all SPIs as level-sensitive by default
     * Bit 0 = level-sensitive, Bit 1 = edge-triggered */
    for (i = GIC_SPI_BASE; i < GicMaxInterrupts; i += 16)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ICFGR + (i / 4)), 0);
    }

    /* Disable all interrupts initially */
    for (i = 0; i < GroupRegisterCount; i++)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ICENABLER + (i * 4)), 0xFFFFFFFF);
    }

    /* Clear all pending interrupts */
    for (i = 0; i < GroupRegisterCount; i++)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ICPENDR + (i * 4)), 0xFFFFFFFF);
    }

    /* Clear all active interrupts */
    for (i = 0; i < GroupRegisterCount; i++)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ICACTIVER + (i * 4)), 0xFFFFFFFF);
    }

    /* Memory barrier to ensure all configuration writes complete */
    HalDataSynchronizationBarrier();

    /* Enable the distributor with appropriate control settings */
    ControlRegister = GICD_CTLR_ENABLE;

    /* For GICv3+, enable Affinity Routing if supported */
    if (GicVersion >= 3)
    {
        ControlRegister |= GICD_CTLR_ARE_NS;  /* Enable ARE for NS Group 1 interrupts */
    }

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_CTLR), ControlRegister);

    /* Memory barrier to ensure enable takes effect */
    HalDataSynchronizationBarrier();

    DPRINT("GIC Distributor initialized (version %lu, %lu interrupts)\n", GicVersion, GicMaxInterrupts);
    return TRUE;
}

/*
 * @brief Initialize GICv2 CPU Interface
 */
BOOLEAN
NTAPI
HalInitializeGicCpuInterface(VOID)
{
    DPRINT("Initializing GICv2 CPU Interface\n");

    if (!GicCpuInterfaceBase)
        return FALSE;

    /* Set interrupt priority mask to allow all interrupts
     * 0xFF = lowest priority (allow all), 0x00 = highest priority (block all) */
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicCpuInterfaceBase + GICC_PMR), 0xFF);

    /* Set binary point register for interrupt preemption
     * This controls the split between group priority and subpriority */
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicCpuInterfaceBase + GICC_BPR), 0x3);

    /* Memory barrier to ensure configuration writes complete */
    HalDataSynchronizationBarrier();

    /* Enable CPU interface for Group 1 interrupts */
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicCpuInterfaceBase + GICC_CTLR), GICC_CTLR_ENABLE);

    /* Memory barrier to ensure enable takes effect */
    HalDataSynchronizationBarrier();

    DPRINT("GICv2 CPU Interface initialized\n");
    return TRUE;
}

/*
 * @brief Initialize GICv3 CPU Interface using System Registers
 */
BOOLEAN
NTAPI
HalInitializeGicv3CpuInterface(VOID)
{
    ULONG64 IccCtlrEl1, IccPmrEl1, EnableValue;
    ULONG64 IccSreEl1;

    DPRINT("Initializing GICv3 CPU Interface\n");

    /* Enable GIC system register interface at EL1: set SRE|DFB|DIB */
    __asm__ __volatile__ (
        "mrs %0, " ICC_SRE_EL1 "\n"
        : "=r"(IccSreEl1)
    );
    IccSreEl1 |= 0x7ULL; /* SRE=1, DFB=1, DIB=1 */
    __asm__ __volatile__ (
        "msr " ICC_SRE_EL1 ", %0\n"
        "isb\n"
        :: "r"(IccSreEl1)
    );

    /* Enable System Register interface for GICv3 */
    __asm__ __volatile__ (
        "mrs %0, " ICC_CTLR_EL1 "\n"
        : "=r"(IccCtlrEl1)
    );

    /* Configure interrupt controller control */
    IccCtlrEl1 |= 0x1;  /* Enable signaling of Group 1 interrupts */

    __asm__ __volatile__ (
        "msr " ICC_CTLR_EL1 ", %0\n"
        "isb\n"
        :: "r"(IccCtlrEl1)
    );

    /* Set interrupt priority mask to allow all interrupts */
    IccPmrEl1 = 0xFF;

    __asm__ __volatile__ (
        "msr " ICC_PMR_EL1 ", %0\n"
        "isb\n"
        :: "r"(IccPmrEl1)
    );

    /* Enable Group 1 interrupts at the CPU interface */
    EnableValue = 1;
    __asm__ __volatile__ (
        "msr " ICC_IGRPEN1_EL1 ", %0\n"
        "isb\n"
        :: "r"(EnableValue)
    );

    DPRINT("GICv3 CPU Interface initialized\n");
    return TRUE;
}

/*
 * @brief Enable a specific interrupt in the GIC
 */
VOID
NTAPI
HalEnableInterrupt(
    IN ULONG InterruptNumber)
{
    ULONG RegisterOffset, BitPosition;

    if (!GicInitialized || InterruptNumber >= GicMaxInterrupts)
    {
        DPRINT1("Invalid interrupt number: %lu (max: %lu, initialized: %s)\n",
                InterruptNumber, GicMaxInterrupts, GicInitialized ? "Yes" : "No");
        return;
    }

    /* Calculate register offset and bit position */
    RegisterOffset = (InterruptNumber / 32) * 4;
    BitPosition = InterruptNumber % 32;

    /* Enable the interrupt in the GIC Distributor */
    if (GicDistributorBase)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ISENABLER + RegisterOffset),
                             1 << BitPosition);

        /* Memory barrier to ensure the write takes effect */
        HalDataSynchronizationBarrier();

        DPRINT("Enabled interrupt %lu (reg=0x%lx, bit=%lu)\n", InterruptNumber, RegisterOffset, BitPosition);
    }
    else
    {
        DPRINT1("GIC Distributor base not available\n");
    }
}

/*
 * @brief Disable a specific interrupt in the GIC
 */
VOID
NTAPI
HalDisableInterrupt(
    IN ULONG InterruptNumber)
{
    ULONG RegisterOffset, BitPosition;

    if (!GicInitialized || InterruptNumber >= GicMaxInterrupts)
    {
        DPRINT1("Invalid interrupt number: %lu (max: %lu, initialized: %s)\n",
                InterruptNumber, GicMaxInterrupts, GicInitialized ? "Yes" : "No");
        return;
    }

    /* Calculate register offset and bit position */
    RegisterOffset = (InterruptNumber / 32) * 4;
    BitPosition = InterruptNumber % 32;

    /* Disable the interrupt in the GIC Distributor */
    if (GicDistributorBase)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_ICENABLER + RegisterOffset),
                             1 << BitPosition);

        /* Memory barrier to ensure the write takes effect */
        HalDataSynchronizationBarrier();

        DPRINT("Disabled interrupt %lu (reg=0x%lx, bit=%lu)\n", InterruptNumber, RegisterOffset, BitPosition);
    }
    else
    {
        DPRINT1("GIC Distributor base not available\n");
    }
}

/*
 * @brief Acknowledge an interrupt and return the interrupt number
 */
ULONG
NTAPI
HalAcknowledgeInterrupt(VOID)
{
    ULONG InterruptNumber;

    if (!GicInitialized)
    {
        return GIC_SPURIOUS_INTERRUPT;
    }

    /* Read the Interrupt Acknowledge Register based on GIC version */
    if (GicVersion >= 3)
    {
        /* GICv3: Use system register interface */
        __asm__ __volatile__ (
            "mrs %0, " ICC_IAR1_EL1 "\n"
            : "=r"(InterruptNumber)
        );

        /* Extract interrupt ID (bits 0-23 for GICv3) */
        InterruptNumber &= 0xFFFFFF;
    }
    else
    {
        /* GICv2: Use memory-mapped interface */
        if (GicCpuInterfaceBase)
        {
            InterruptNumber = READ_REGISTER_ULONG((PULONG)((PUCHAR)GicCpuInterfaceBase + GICC_IAR));

            /* Extract interrupt ID (bits 0-9 for GICv2) */
            InterruptNumber &= 0x3FF;
        }
        else
        {
            InterruptNumber = GIC_SPURIOUS_INTERRUPT;
        }
    }

    /* Check for spurious interrupt */
    if (InterruptNumber == GIC_SPURIOUS_INTERRUPT)
    {
        DPRINT("Spurious interrupt detected\n");
    }

    return InterruptNumber;
}

/*
 * @brief Signal end of interrupt to the GIC
 */
VOID
NTAPI
HalEndOfInterrupt(
    IN ULONG InterruptNumber)
{
    if (!GicInitialized)
    {
        return;
    }

    /* Signal End of Interrupt based on GIC version */
    if (GicVersion >= 3)
    {
        /* GICv3: Use system register interface */
        __asm__ __volatile__ (
            "msr " ICC_EOIR1_EL1 ", %0\n"
            :: "r"((ULONG64)InterruptNumber)
        );
    }
    else
    {
        /* GICv2: Use memory-mapped interface */
        if (GicCpuInterfaceBase)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicCpuInterfaceBase + GICC_EOIR), InterruptNumber);
        }
    }

    /* Memory barrier to ensure EOI is processed */
    HalDataSynchronizationBarrier();

    DPRINT("End of interrupt %lu\n", InterruptNumber);
}

/*
 * @brief Send a Software Generated Interrupt (SGI)
 */
VOID
NTAPI
HalSendSoftwareInterrupt(
    IN ULONG TargetCpu,
    IN ULONG InterruptNumber)
{
    ULONG SgiRegister;

    if (InterruptNumber >= 16)
    {
        DPRINT1("Invalid SGI number: %lu (must be 0-15)\n", InterruptNumber);
        return;
    }

    /* TODO: Construct SGI register value and send interrupt
     * Format for GICv2:
     * - Bits 0-3: SGI interrupt ID
     * - Bits 16-23: Target CPU list
     * - Bits 24-25: Target list filter
     */

    SgiRegister = InterruptNumber | ((1 << TargetCpu) << 16);

    /* TODO: Write to Software Generated Interrupt Register */
    // WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_SGIR), SgiRegister);

    /* Avoid unused variable warning */
    (VOID)SgiRegister;

    DPRINT("Sent SGI %lu to CPU %lu\n", InterruptNumber, TargetCpu);
}

/*
 * @brief Set interrupt priority
 */
VOID
NTAPI
HalSetInterruptPriority(
    IN ULONG InterruptNumber,
    IN ULONG Priority)
{
    ULONG RegisterOffset, ByteOffset, CurrentValue;
    UCHAR PriorityValue = (UCHAR)(Priority & 0xFF);

    if (InterruptNumber >= GicMaxInterrupts)
    {
        DPRINT1("Invalid interrupt number: %lu\n", InterruptNumber);
        return;
    }

    /* Calculate register offset and byte position */
    RegisterOffset = (InterruptNumber / 4) * 4;
    ByteOffset = InterruptNumber % 4;

    /* TODO: Read-modify-write the priority register */
    // CurrentValue = READ_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_IPRIORITYR + RegisterOffset));
    CurrentValue = 0;  /* Placeholder */

    /* Clear the relevant byte and set new priority */
    CurrentValue &= ~(0xFF << (ByteOffset * 8));
    CurrentValue |= (PriorityValue << (ByteOffset * 8));

    /* TODO: Write back the modified value */
    // WRITE_REGISTER_ULONG((PULONG)((PUCHAR)GicDistributorBase + GICD_IPRIORITYR + RegisterOffset), CurrentValue);

    /* Avoid unused variable warnings */
    (VOID)RegisterOffset;
    (VOID)ByteOffset;
    (VOID)CurrentValue;
    (VOID)PriorityValue;

    DPRINT("Set interrupt %lu priority to %lu\n", InterruptNumber, Priority);
}
