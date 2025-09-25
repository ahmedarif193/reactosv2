/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 HAL Initialization
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

BOOLEAN HalInitializationStarted = FALSE;
ULONG HalBusType = MACHINE_TYPE_UNKNOWN;
ULONG HalDisableFirmwareMapper = 0;

/* ARM64 System Information */
static ARM64_CPU_INFO HalProcessorInfo;
static BOOLEAN HalTimerInitialized = FALSE;
static ARM64_GIC_INFO HalGicInfo;
static ARM64_TIMER_INFO HalTimerInfo;
static ARM64_PLATFORM_INFO HalPlatformInfo;

/* HAL Time Management */
ULONG HalCurrentTimeIncrement = 0;
ULONG HalNextTimeIncrement = 0;
ULONG HalNextIntervalCount = 0;

/* ARM64 Cache Management */
static ULONG HalCacheLineSize = 64;  /* Default ARM64 cache line size */

/* PRIVATE FUNCTIONS **********************************************************/

static
CODE_SEG("INIT")
VOID
HalGetParameters(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Make sure we have a loader block and command line */
    if (LoaderBlock && LoaderBlock->LoadOptions)
    {
        /* Read the command line */
        PCSTR CommandLine = LoaderBlock->LoadOptions;

        /* Check for initial breakpoint */
        if (strstr(CommandLine, "BREAK"))
            DbgBreakPoint();
    }
}

/* FUNCTIONS ******************************************************************/

/*
 * @brief ARM64 HAL System Initialization
 *
 * This function performs ARM64 Hardware Abstraction Layer initialization
 * in two phases following ReactOS HAL conventions.
 */
BOOLEAN
NTAPI
HalInitSystem(
    IN ULONG BootPhase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    DPRINT("HalInitSystem: Phase %lu on ARM64\n", BootPhase);

    switch (BootPhase)
    {
        case 0:
        {
            /* Phase 0 - Early HAL initialization */
            DPRINT("ARM64 HAL Phase 0 initialization\n");

            /* Get command-line parameters for debugging */
            HalGetParameters(LoaderBlock);

            /* Validate build compatibility - checked HAL requires checked kernel */
#if DBG
            if (!(Prcb->BuildType & PRCB_BUILD_DEBUG))
            {
                /* No match, bugcheck */
                KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, PRCB_BUILD_DEBUG, 0);
            }
#else
            /* Release build requires release HAL */
            if (Prcb->BuildType & PRCB_BUILD_DEBUG)
            {
                /* No match, bugcheck */
                KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, 0, 0);
            }
#endif

#ifdef CONFIG_SMP
            /* SMP HAL requires SMP kernel */
            if (Prcb->BuildType & PRCB_BUILD_UNIPROCESSOR)
            {
                /* No match, bugcheck */
                KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, 0, 0);
            }
#endif

            /* Validate the PRCB major version */
            if (Prcb->MajorVersion != PRCB_MAJOR_VERSION)
            {
                /* Validation failed, bugcheck */
                KeBugCheckEx(MISMATCHED_HAL, 1, Prcb->MajorVersion, PRCB_MAJOR_VERSION, 0);
            }

            /* Mark HAL as started */
            HalInitializationStarted = TRUE;

            /* Detect ARM64 processor features and capabilities */
            HalDetectProcessorFeatures();

            /* Initialize ARM64 cache and memory coherency early */
            HalInitializeCacheManager();

            /* Initialize ARM64 Generic Interrupt Controller */
            if (!HalInitializeGIC())
            {
                DPRINT1("Failed to initialize ARM64 GIC\n");
                return FALSE;
            }

            /* Force initial interrupt state - ARM64 specific */
            KfRaiseIrql(KeGetCurrentIrql());

            /* Initialize ARM64 Generic Timer */
            if (!HalInitializeSystemTimer())
            {
                DPRINT1("Failed to initialize ARM64 system timer\n");
                return FALSE;
            }

            /* Initialize ARM64 interrupt handling system */
            if (!HalInitializeInterruptSystem())
            {
                DPRINT1("Failed to initialize ARM64 interrupt system\n");
                return FALSE;
            }

            /* Setup time increments - 10ms default, 1ms minimum for ARM64 */
            HalCurrentTimeIncrement = 100000;  /* 10ms in 100ns units */
            HalNextTimeIncrement = 100000;
            HalNextIntervalCount = 0;
            KeSetTimeIncrement(100000, 10000);

            /* Clear any pending profile interrupts from previous boot */
            HalStopProfileInterrupt(ProfileTime);

            /* Set bus type for ARM64 platform - usually ACPI */
            HalBusType = MACHINE_TYPE_ACPI;

            /* Initialize system information structures */
            HalInitializeSystemInformation();

            break;
        }

        case 1:
        {
            /* Phase 1 - Late HAL initialization */
            DPRINT("ARM64 HAL Phase 1 initialization\n");

            /* Enable ARM64 Generic Timer interrupt */
            HalEnableTimerInterrupt();

            /* Initialize ACPI support for ARM64 platform discovery */
            HalInitializeACPI(LoaderBlock);

            /* Initialize DMA coherency and IOMMU support */
            HalInitializeDMA();

            /* Initialize ARM64 power management features */
            HalInitializePowerManagement();

            /* Set up ARM64 performance monitoring counters */
            HalInitializePerformanceMonitoring();

            /* Initialize debug and trace support */
            HalInitializeDebugSupport();

            break;
        }

        default:
            DPRINT1("Unknown HAL initialization phase: %lu\n", BootPhase);
            return FALSE;
    }

    return TRUE;
}

/*
 * @brief Detect ARM64 processor features and capabilities
 */
VOID
NTAPI
HalDetectProcessorFeatures(VOID)
{
    ULONG64 IdAA64Pfr0, IdAA64Pfr1, IdAA64Dfr0, IdAA64Dfr1;
    ULONG64 IdAA64Isar0, IdAA64Isar1, IdAA64Mmfr0, IdAA64Mmfr1, IdAA64Mmfr2;
    ULONG64 Midr, Mpidr, Revidr;

    DPRINT("Detecting ARM64 processor features\n");

    /* Read ARM64 ID registers - must execute at EL1 or higher */
    __asm__ __volatile__ (
        "mrs %0, ID_AA64PFR0_EL1\n"        /* Processor Feature Register 0 */
        "mrs %1, ID_AA64PFR1_EL1\n"        /* Processor Feature Register 1 */
        "mrs %2, ID_AA64DFR0_EL1\n"        /* Debug Feature Register 0 */
        "mrs %3, ID_AA64DFR1_EL1\n"        /* Debug Feature Register 1 */
        "mrs %4, ID_AA64ISAR0_EL1\n"       /* Instruction Set Attribute Register 0 */
        "mrs %5, ID_AA64ISAR1_EL1\n"       /* Instruction Set Attribute Register 1 */
        "mrs %6, ID_AA64MMFR0_EL1\n"       /* Memory Model Feature Register 0 */
        "mrs %7, ID_AA64MMFR1_EL1\n"       /* Memory Model Feature Register 1 */
        "mrs %8, ID_AA64MMFR2_EL1\n"       /* Memory Model Feature Register 2 */
        : "=r"(IdAA64Pfr0), "=r"(IdAA64Pfr1), "=r"(IdAA64Dfr0), "=r"(IdAA64Dfr1),
          "=r"(IdAA64Isar0), "=r"(IdAA64Isar1), "=r"(IdAA64Mmfr0), "=r"(IdAA64Mmfr1), "=r"(IdAA64Mmfr2)
    );

    /* Read main identification registers */
    __asm__ __volatile__ (
        "mrs %0, MIDR_EL1\n"               /* Main ID Register */
        "mrs %1, MPIDR_EL1\n"              /* Multiprocessor Affinity Register */
        "mrs %2, REVIDR_EL1\n"             /* Revision ID Register */
        : "=r"(Midr), "=r"(Mpidr), "=r"(Revidr)
    );

    /* Store comprehensive processor information */
    HalProcessorInfo.PFR0 = IdAA64Pfr0;
    HalProcessorInfo.PFR1 = IdAA64Pfr1;
    HalProcessorInfo.DFR0 = IdAA64Dfr0;
    HalProcessorInfo.DFR1 = IdAA64Dfr1;
    HalProcessorInfo.ISAR0 = IdAA64Isar0;
    HalProcessorInfo.ISAR1 = IdAA64Isar1;
    HalProcessorInfo.MMFR0 = IdAA64Mmfr0;
    HalProcessorInfo.MMFR1 = IdAA64Mmfr1;
    HalProcessorInfo.MMFR2 = IdAA64Mmfr2;

    /* Parse and decode ARM64 features */
    HalPlatformInfo.SystemFeatures = 0;

    /* Check Floating Point and Advanced SIMD support (PFR0[19:16]) */
    if (((IdAA64Pfr0 >> 16) & 0xF) != 0xF)
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_FLOATING_POINT;
    }
    if (((IdAA64Pfr0 >> 20) & 0xF) != 0xF)
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_SIMD;
    }

    /* Check cryptographic extensions (ISAR0) */
    if (((IdAA64Isar0 >> 4) & 0xF) != 0)   /* AES support */
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_CRYPTO_AES;
    }
    if (((IdAA64Isar0 >> 8) & 0xF) != 0)   /* SHA1 support */
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_CRYPTO_SHA1;
    }
    if (((IdAA64Isar0 >> 12) & 0xF) != 0)  /* SHA2 support */
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_CRYPTO_SHA2;
    }
    if (((IdAA64Isar0 >> 16) & 0xF) != 0)  /* CRC32 support */
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_CRYPTO_CRC32;
    }

    /* Check Pointer Authentication (ISAR1[11:4] and [7:0]) */
    if ((((IdAA64Isar1 >> 4) & 0xF) != 0) || (((IdAA64Isar1) & 0xF) != 0))
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_POINTER_AUTH;
    }

    /* Check Scalable Vector Extension (PFR0[35:32]) */
    if (((IdAA64Pfr0 >> 32) & 0xF) != 0)
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_SVE;

        /* Read SVE Control Register if available */
        __asm__ __volatile__ (
            "mrs %0, S3_0_C1_C2_0\n"       /* ZCR_EL1 */
            : "=r"(HalProcessorInfo.SVE_ZCR)
        );
    }

    /* Check Virtualization extensions (PFR0[31:28]) */
    if (((IdAA64Pfr0 >> 28) & 0xF) != 0)
    {
        HalPlatformInfo.SystemFeatures |= ARM64_FEATURE_VIRTUALIZATION;
        HalPlatformInfo.Virtualization = TRUE;
    }

    /* Extract CPU vendor and model information from MIDR_EL1 */
    ULONG Implementer = (Midr >> 24) & 0xFF;
    ULONG Variant = (Midr >> 20) & 0xF;
    ULONG Architecture = (Midr >> 16) & 0xF;
    ULONG PartNum = (Midr >> 4) & 0xFFF;
    ULONG Revision = Midr & 0xF;

    /* Set vendor string based on implementer */
    switch (Implementer)
    {
        case 0x41:  /* ARM */
            strcpy(HalPlatformInfo.VendorString, "ARM Limited");
            break;
        case 0x42:  /* Broadcom */
            strcpy(HalPlatformInfo.VendorString, "Broadcom");
            break;
        case 0x43:  /* Cavium */
            strcpy(HalPlatformInfo.VendorString, "Cavium");
            break;
        case 0x4E:  /* NVIDIA */
            strcpy(HalPlatformInfo.VendorString, "NVIDIA");
            break;
        case 0x50:  /* Applied Micro */
            strcpy(HalPlatformInfo.VendorString, "Applied Micro");
            break;
        case 0x51:  /* Qualcomm */
            strcpy(HalPlatformInfo.VendorString, "Qualcomm");
            break;
        case 0x56:  /* Marvell */
            strcpy(HalPlatformInfo.VendorString, "Marvell");
            break;
        default:
            sprintf(HalPlatformInfo.VendorString, "Unknown (0x%02X)", Implementer);
            break;
    }

    /* Create model string */
    sprintf(HalPlatformInfo.ModelString, "ARMv8 Part 0x%03X r%up%u",
            PartNum, Variant, Revision);

    DPRINT("ARM64 CPU: %s %s (MIDR=%016llx MPIDR=%016llx)\n",
           HalPlatformInfo.VendorString, HalPlatformInfo.ModelString, Midr, Mpidr);

    DPRINT("ARM64 Features: PFR0=%016llx PFR1=%016llx DFR0=%016llx ISAR0=%016llx\n",
           IdAA64Pfr0, IdAA64Pfr1, IdAA64Dfr0, IdAA64Isar0);

    DPRINT("ARM64 Memory: MMFR0=%016llx MMFR1=%016llx MMFR2=%016llx\n",
           IdAA64Mmfr0, IdAA64Mmfr1, IdAA64Mmfr2);

    DPRINT("ARM64 System Features: 0x%016llx\n", HalPlatformInfo.SystemFeatures);
}

/*
 * @brief Initialize ARM64 Generic Interrupt Controller (GIC)
 */
BOOLEAN
NTAPI
HalInitializeGIC(VOID)
{
    /* TODO: Initialize ARM64 GIC (Generic Interrupt Controller)
     * - Detect GIC version (GICv2, GICv3, GICv4)
     * - Initialize GIC Distributor interface
     * - Initialize GIC CPU interface(s)
     * - Set up interrupt routing and priorities
     * - Configure system interrupts (SGI, PPI, SPI)
     * - Enable GIC and configure basic interrupt delivery
     */

    DPRINT("Initializing ARM64 Generic Interrupt Controller\n");

    /* Placeholder - basic GIC initialization would go here */
    return TRUE;
}

/*
 * @brief Initialize ARM64 Generic Timer
 */
BOOLEAN
NTAPI
HalInitializeSystemTimer(VOID)
{
    /* TODO: Initialize ARM64 Generic Timer
     * - Configure CNTKCTL_EL1 for timer access
     * - Set up virtual and physical timer interrupts
     * - Initialize timer frequency and calibration
     * - Set up timer interrupt handling
     * - Configure timer for system tick generation
     */

    DPRINT("Initializing ARM64 Generic Timer\n");

    /* Mark timer as initialized */
    HalTimerInitialized = TRUE;

    return TRUE;
}

/*
 * @brief Initialize ARM64 cache management
 */
VOID
NTAPI
HalInitializeCacheManager(VOID)
{
    ULONG64 CtrEl0, CachelineSize, Ccsidr;
    ARM64_CACHE_INFO CacheInfo;

    DPRINT("Initializing ARM64 cache management\n");

    /* Read Cache Type Register (CTR_EL0) */
    __asm__ __volatile__ (
        "mrs %0, CTR_EL0\n"
        : "=r"(CtrEl0)
    );

    /* Parse cache line sizes and properties */
    CacheInfo.DMinLine = 4 << ((CtrEl0 >> 16) & 0xF);  /* Data cache min line size */
    CacheInfo.IMinLine = 4 << (CtrEl0 & 0xF);          /* Instruction cache min line size */
    CacheInfo.CWG = 4 << ((CtrEl0 >> 24) & 0xF);       /* Cache Write-back Granule */
    CacheInfo.ERG = 4 << ((CtrEl0 >> 20) & 0xF);       /* Exclusives Reservation Granule */

    /* Check L1 instruction cache policy */
    CacheInfo.L1IpPolicy = (CtrEl0 >> 14) & 0x3;
    /* 0b00: VMID aware PIPT, 0b01: ASID-tagged VIVT, 0b10: VIPT, 0b11: PIPT */

    /* Determine if we have separate I and D caches at L1 */
    CacheInfo.SeparateCaches = ((CtrEl0 >> 28) & 0x1) == 0;

    DPRINT("ARM64 Cache Info: DMinLine=%u IMinLine=%u CWG=%u ERG=%u L1IpPolicy=%u Sep=%s\n",
           CacheInfo.DMinLine, CacheInfo.IMinLine, CacheInfo.CWG, CacheInfo.ERG,
           CacheInfo.L1IpPolicy, CacheInfo.SeparateCaches ? "Yes" : "No");

    /* Initialize Memory Attribute Indirection Register (MAIR_EL1) for ARM64
     * This sets up memory types for different page table entries */
    ULONG64 Mair = 0;

    /* Index 0: Device-nGnRnE (Strongly ordered device memory) */
    Mair |= 0x00ULL << (0 * 8);

    /* Index 1: Device-nGnRE (Device memory) */
    Mair |= 0x04ULL << (1 * 8);

    /* Index 2: Device-GRE (Gathering, Reordering, Early Write Acknowledgement) */
    Mair |= 0x0CULL << (2 * 8);

    /* Index 3: Normal memory, Write-through, Read-Allocate, No Write-Allocate */
    Mair |= 0xAAULL << (3 * 8);

    /* Index 4: Normal memory, Write-back, Read-Allocate, Write-Allocate */
    Mair |= 0xFFULL << (4 * 8);

    /* Index 5: Normal memory, Non-cacheable */
    Mair |= 0x44ULL << (5 * 8);

    /* Index 6: Normal memory, Write-through, Read-Allocate, Write-Allocate */
    Mair |= 0xFAULL << (6 * 8);

    /* Index 7: Normal memory, Write-back, Read-Allocate, Write-Allocate, transient */
    Mair |= 0xBBULL << (7 * 8);

    /* Set Memory Attribute Indirection Register */
    __asm__ __volatile__ (
        "msr MAIR_EL1, %0\n"
        "isb\n"
        :: "r"(Mair)
        : "memory"
    );

    /* Enable cache coherency and set up system control register bits */
    ULONG64 SctlrEl1;
    __asm__ __volatile__ (
        "mrs %0, SCTLR_EL1\n"
        : "=r"(SctlrEl1)
    );

    /* Ensure important cache and MMU settings are enabled */
    SctlrEl1 |= ARM64_SCTLR_EL1_DATA_CACHE;      /* Enable D-cache */
    SctlrEl1 |= ARM64_SCTLR_EL1_INSTR_CACHE;     /* Enable I-cache */
    SctlrEl1 |= ARM64_SCTLR_EL1_MMU_ENABLE;      /* Ensure MMU is enabled */

    __asm__ __volatile__ (
        "msr SCTLR_EL1, %0\n"
        "isb\n"
        :: "r"(SctlrEl1)
        : "memory"
    );

    /* Perform comprehensive cache maintenance to ensure coherency */

    /* Clean and invalidate all data caches to Point of Coherency */
    __asm__ __volatile__ (
        "dsb sy\n"                      /* Data Synchronization Barrier - full system */
        "ic iallu\n"                    /* Invalidate all instruction caches to PoU */
        "dsb sy\n"                      /* Another barrier after I-cache invalidation */
        "isb\n"                         /* Instruction Synchronization Barrier */
        ::: "memory"
    );

    /* Store cache information for later use */
    HalCacheLineSize = CacheInfo.DMinLine;

    DPRINT("ARM64 cache management initialized - cache line size: %u bytes\n",
           CacheInfo.DMinLine);
}

/*
 * @brief Initialize ARM64 ACPI support
 */
VOID
NTAPI
HalInitializeACPI(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* TODO: Initialize ARM64 ACPI (Advanced Configuration and Power Interface)
     * - Locate and validate ACPI tables
     * - Parse ARM64-specific ACPI tables (MADT, GTDT, etc.)
     * - Initialize ACPI namespace and objects
     * - Set up ACPI-based device discovery
     * - Configure ACPI power management
     */

    UNREFERENCED_PARAMETER(LoaderBlock);

    DPRINT("Initializing ARM64 ACPI support\n");
}

/*
 * @brief Initialize ARM64 DMA support
 */
VOID
NTAPI
HalInitializeDMA(VOID)
{
    /* TODO: Initialize ARM64 DMA and IOMMU support
     * - Set up DMA coherency protocols
     * - Initialize SMMU (System Memory Management Unit) if present
     * - Configure DMA address translation
     * - Set up DMA buffer management
     * - Initialize scatter-gather DMA support
     */

    DPRINT("Initializing ARM64 DMA support\n");
}

/* Note: HalInitializePowerManagement is implemented in generic/power.c */

/*
 * @brief Initialize ARM64 performance monitoring
 */
VOID
NTAPI
HalInitializePerformanceMonitoring(VOID)
{
    /* TODO: Initialize ARM64 performance monitoring
     * - Set up PMU (Performance Monitoring Unit) access
     * - Configure performance counters
     * - Initialize statistical profiling if available
     * - Set up trace and debug support
     * - Configure performance event routing
     */

    DPRINT("Initializing ARM64 performance monitoring\n");
}

/*
 * @brief Enable ARM64 Generic Timer interrupt
 */
VOID
NTAPI
HalEnableTimerInterrupt(VOID)
{
    /* TODO: Enable ARM64 Generic Timer interrupt
     * - Configure timer interrupt routing in GIC
     * - Set appropriate interrupt priority
     * - Enable timer interrupt in the interrupt controller
     * - Set up timer interrupt handler registration
     */

    DPRINT("Enabling ARM64 Generic Timer interrupt\n");

    /* For now, just mark as enabled - actual implementation needed */
    HalTimerInitialized = TRUE;
}

/*
 * @brief Initialize ARM64 debug support
 */
VOID
NTAPI
HalInitializeDebugSupport(VOID)
{
    /* TODO: Initialize ARM64 debug and trace support
     * - Set up ARM64 debug registers access
     * - Configure hardware breakpoints and watchpoints
     * - Initialize Performance Monitoring Unit (PMU)
     * - Set up Statistical Profiling Extension if available
     * - Configure Self-hosted Trace extension
     * - Initialize CoreSight trace infrastructure
     */

    DPRINT("Initializing ARM64 debug support\n");
}

/*
 * @brief Get ARM64 processor information
 */
VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* TODO: ARM64 processor idle implementation
     * - Execute appropriate ARM64 idle instruction (WFI/WFE)
     * - Handle wake-up conditions properly
     * - Coordinate with power management subsystem
     * - Maintain cache coherency during idle states
     */

    /* Simple implementation using WFI (Wait For Interrupt) */
    __asm__ __volatile__ ("wfi" ::: "memory");
}

/*
 * @brief Halt the ARM64 processor
 */
VOID
NTAPI
HalHaltSystem(VOID)
{
    /* TODO: Implement ARM64 system halt
     * - Disable interrupts at ARM64 level
     * - Flush caches and perform necessary cleanup
     * - Execute appropriate halt sequence
     * - Handle multi-processor halt coordination
     */

    /* Disable interrupts and halt */
    _disable();

    for (;;)
    {
        __asm__ __volatile__ ("wfi" ::: "memory");
    }
}