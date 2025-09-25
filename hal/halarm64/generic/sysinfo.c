/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 System Information
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

static SYSTEM_BASIC_INFORMATION SystemBasicInfo;
static SYSTEM_PROCESSOR_INFORMATION SystemProcessorInfo;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Get system basic information
 */
NTSTATUS
NTAPI
HalQuerySystemInformation(
    IN HAL_QUERY_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    OUT PVOID Buffer,
    OUT PULONG ReturnedLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("HalQuerySystemInformation: Class=%d, BufferSize=%lu\n",
           InformationClass, BufferSize);

    switch (InformationClass)
    {
        #if 0 /* HalInstalledPhysicalMemory not available in current ReactOS */
        case HalInstalledPhysicalMemory:
        {
            /* TODO: Return installed physical memory information
             * - Query memory map from bootloader/firmware
             * - Calculate total installed RAM
             * - Handle memory holes and reserved regions
             */

            if (BufferSize < sizeof(ULONG_PTR))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                /* Placeholder - should get from memory manager */
                *(PULONG_PTR)Buffer = 0x40000000;  /* 1GB default */
                if (ReturnedLength)
                    *ReturnedLength = sizeof(ULONG_PTR);
            }
            break;
        }
        #endif

        #if 0 /* HalFrameBufferInformation not available */
        case HalFrameBufferInformation:
        {
            /* TODO: Return framebuffer information if graphics are available
             * - Get framebuffer base address and size
             * - Return pixel format and dimensions
             * - Handle multiple display outputs
             */

            DPRINT1("HalFrameBufferInformation not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }
        #endif

        case HalDisplayBiosInformation:
        {
            /* TODO: Return display BIOS information
             * - On ARM64, this might be UEFI GOP information
             * - Return display capabilities and modes
             */

            DPRINT1("HalDisplayBiosInformation not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        case HalProcessorSpeedInformation:
        {
            /* TODO: Return processor speed information
             * - Get CPU frequency from firmware/ACPI
             * - Handle multiple processors with different speeds
             * - Return current and maximum frequencies
             */

            if (BufferSize < sizeof(PROCESSOR_SPEED_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                PPROCESSOR_SPEED_INFORMATION SpeedInfo = (PPROCESSOR_SPEED_INFORMATION)Buffer;

                /* Placeholder values - should get from hardware */
                SpeedInfo->ProcessorSpeed = 2000;  /* 2GHz default */

                if (ReturnedLength)
                    *ReturnedLength = sizeof(PROCESSOR_SPEED_INFORMATION);
            }
            break;
        }

        case HalCallbackInformation:
        {
            /* TODO: Return callback information
             * - Register HAL callback routines
             * - Handle power management callbacks
             * - Set up system event notifications
             */

            DPRINT1("HalCallbackInformation not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        case HalMapRegisterInformation:
        {
            /* TODO: Return map register information for DMA
             * - Report available DMA map registers
             * - Handle SMMU/IOMMU capabilities
             * - Return DMA addressing limitations
             */

            DPRINT1("HalMapRegisterInformation not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        case HalMcaLogInformation:
        {
            /* TODO: Return Machine Check Architecture log information
             * - Handle ARM64 RAS (Reliability, Availability, Serviceability)
             * - Return error logs and capabilities
             * - Manage error reporting and recovery
             */

            DPRINT1("HalMcaLogInformation not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        case HalPlatformInformation:
        {
            /* TODO: Return ARM64 platform information
             * - Board/SoC identification
             * - Firmware version and capabilities
             * - Platform-specific features
             */

            if (BufferSize < sizeof(HAL_PLATFORM_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                PHAL_PLATFORM_INFORMATION PlatformInfo = (PHAL_PLATFORM_INFORMATION)Buffer;

                /* TODO: Fill with actual ARM64 platform data */
                RtlZeroMemory(PlatformInfo, sizeof(HAL_PLATFORM_INFORMATION));
                PlatformInfo->PlatformFlags = HAL_PLATFORM_ACPI_TABLES_PRESENT;

                if (ReturnedLength)
                    *ReturnedLength = sizeof(HAL_PLATFORM_INFORMATION);
            }
            break;
        }

        default:
            DPRINT1("Unknown information class: %d\n", InformationClass);
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    return Status;
}

/*
 * @brief Set system information
 */
NTSTATUS
NTAPI
HalSetSystemInformation(
    IN HAL_SET_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    IN PVOID Buffer)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("HalSetSystemInformation: Class=%d, BufferSize=%lu\n",
           InformationClass, BufferSize);

    switch (InformationClass)
    {
        case HalProfileSourceInterval:
        {
            /* TODO: Set profiling source interval
             * - Configure ARM64 Performance Monitoring Unit (PMU)
             * - Set sampling intervals for performance counters
             * - Handle different profiling sources
             */

            DPRINT1("HalProfileSourceInterval not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        #if 0 /* HalProfileSourceIntervalSet not available */
        case HalProfileSourceIntervalSet:
        {
            /* TODO: Set profile source interval set
             * - Configure multiple profiling sources
             * - Set up event-based sampling
             * - Handle PMU overflow interrupts
             */

            DPRINT1("HalProfileSourceIntervalSet not implemented for ARM64\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }
        #endif

        default:
            DPRINT1("Unknown set information class: %d\n", InformationClass);
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);

    return Status;
}

/*
 * @brief Initialize system information
 */
VOID
NTAPI
HalInitializeSystemInformation(VOID)
{
    ULONG64 ProcessorFeatures;

    DPRINT("Initializing ARM64 system information\n");

    /* TODO: Initialize basic system information */
    RtlZeroMemory(&SystemBasicInfo, sizeof(SystemBasicInfo));
    SystemBasicInfo.Reserved = 0;
    SystemBasicInfo.TimerResolution = 156250;  /* 15.625ms typical for ARM64 */
    SystemBasicInfo.PageSize = PAGE_SIZE;
    SystemBasicInfo.NumberOfPhysicalPages = 0;  /* TODO: Get from memory manager */
    SystemBasicInfo.LowestPhysicalPageNumber = 0;
    SystemBasicInfo.HighestPhysicalPageNumber = 0;  /* TODO: Get from memory manager */
    SystemBasicInfo.AllocationGranularity = PAGE_SIZE;
    SystemBasicInfo.MinimumUserModeAddress = (ULONG_PTR)0x10000;
    SystemBasicInfo.MaximumUserModeAddress = (ULONG_PTR)0x7FFFFFFFFFFE;  /* ARM64 user space */
    SystemBasicInfo.ActiveProcessorsAffinityMask = 1;  /* TODO: Get actual processor count */
    SystemBasicInfo.NumberOfProcessors = 1;  /* TODO: Get actual processor count */

    /* TODO: Initialize processor information */
    RtlZeroMemory(&SystemProcessorInfo, sizeof(SystemProcessorInfo));
    SystemProcessorInfo.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
    SystemProcessorInfo.ProcessorLevel = 8;  /* ARMv8 */
    SystemProcessorInfo.ProcessorRevision = 0;  /* TODO: Get from ID registers */

    /* TODO: Read ARM64 processor features from ID registers */
    __asm__ __volatile__ (
        "mrs %0, ID_AA64PFR0_EL1\n"
        : "=r"(ProcessorFeatures)
    );

    /* TODO: Set processor features based on ARM64 capabilities
     * - Floating point support
     * - SIMD/NEON support
     * - Cryptographic extensions
     * - Virtualization support
     * - Security extensions
     */
    SystemProcessorInfo.ProcessorFeatureBits = 0;  /* TODO: Parse feature bits */

    DPRINT("ARM64 Processor Features: 0x%016llx\n", ProcessorFeatures);
}

/*
 * @brief Get processor information
 */
VOID
NTAPI
HalGetProcessorInformation(
    OUT PSYSTEM_PROCESSOR_INFORMATION ProcessorInfo)
{
    if (ProcessorInfo)
    {
        *ProcessorInfo = SystemProcessorInfo;
    }
}

/*
 * @brief Get basic system information
 */
VOID
NTAPI
HalGetBasicInformation(
    OUT PSYSTEM_BASIC_INFORMATION BasicInfo)
{
    if (BasicInfo)
    {
        *BasicInfo = SystemBasicInfo;
    }
}

/*
 * @brief Get ARM64-specific processor capabilities
 */
ULONG64
NTAPI
HalGetProcessorCapabilities(VOID)
{
    ULONG64 Features = 0;
    ULONG64 IdAA64Pfr0, IdAA64Isar0, IdAA64Mmfr0;

    /* TODO: Read ARM64 ID registers to determine features */
    __asm__ __volatile__ (
        "mrs %0, ID_AA64PFR0_EL1\n"
        "mrs %1, ID_AA64ISAR0_EL1\n"
        "mrs %2, ID_AA64MMFR0_EL1\n"
        : "=r"(IdAA64Pfr0), "=r"(IdAA64Isar0), "=r"(IdAA64Mmfr0)
    );

    /* TODO: Parse feature registers and set capability flags
     * - Check for floating point and SIMD support
     * - Detect cryptographic extensions
     * - Check for pointer authentication
     * - Detect memory tagging extensions
     * - Check for scalable vector extensions (SVE)
     */

    /* Example feature detection (simplified) */
    if ((IdAA64Pfr0 & 0xF) != 0xF)  /* FP field */
    {
        Features |= ARM64_FEATURE_FLOATING_POINT;
    }

    if (((IdAA64Pfr0 >> 4) & 0xF) != 0xF)  /* AdvSIMD field */
    {
        Features |= ARM64_FEATURE_SIMD;
    }

    if (((IdAA64Isar0 >> 4) & 0xF) != 0)  /* AES field */
    {
        Features |= ARM64_FEATURE_CRYPTO_AES;
    }

    if (((IdAA64Isar0 >> 8) & 0xF) != 0)  /* SHA1 field */
    {
        Features |= ARM64_FEATURE_CRYPTO_SHA1;
    }

    DPRINT("ARM64 Feature bits: 0x%016llx\n", Features);
    return Features;
}

/*
 * @brief Get cache information
 */
NTSTATUS
NTAPI
HalGetCacheInformation(
    OUT PARM64_CACHE_INFO CacheInfo)
{
    ULONG64 CacheType;
    ULONG CacheLevel;

    if (!CacheInfo)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(CacheInfo, sizeof(ARM64_CACHE_INFO));

    /* TODO: Read ARM64 cache type register */
    __asm__ __volatile__ (
        "mrs %0, CTR_EL0\n"
        : "=r"(CacheType)
    );

    /* Parse cache information from CTR_EL0 */
    CacheInfo->DMinLine = 4 << ((CacheType >> 16) & 0xF);  /* Data cache minimum line size */
    CacheInfo->IMinLine = 4 << (CacheType & 0xF);         /* Instruction cache minimum line size */

    /* TODO: Read detailed cache information from CCSIDR_EL1
     * This requires setting CSSELR_EL1 to select cache level/type
     */

    for (CacheLevel = 0; CacheLevel < 8; CacheLevel++)
    {
        /* TODO: Select cache level and read CCSIDR_EL1 */
        /* This would give us detailed information about each cache level */
    }

    DPRINT("ARM64 Cache Info: DMinLine=%lu, IMinLine=%lu\n",
           CacheInfo->DMinLine, CacheInfo->IMinLine);

    return STATUS_SUCCESS;
}

/*
 * @brief Get memory information
 */
NTSTATUS
NTAPI
HalGetMemoryInformation(
    OUT PARM64_MEMORY_INFO MemoryInfo)
{
    if (!MemoryInfo)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* TODO: Get memory information from ARM64 system
     * - Physical memory layout
     * - Memory attribute capabilities
     * - Address space information
     * - Memory protection features
     */

    RtlZeroMemory(MemoryInfo, sizeof(ARM64_MEMORY_INFO));

    /* TODO: Fill with actual memory configuration */
    MemoryInfo->PhysicalAddressBits = 48;  /* Typical ARM64 PA size */
    MemoryInfo->VirtualAddressBits = 48;   /* Typical ARM64 VA size */
    MemoryInfo->PageSizes = ARM64_PAGE_SIZE_4KB | ARM64_PAGE_SIZE_64KB; /* Supported page sizes */

    return STATUS_SUCCESS;
}