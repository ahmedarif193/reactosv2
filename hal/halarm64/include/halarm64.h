/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 HAL Definitions and Declarations
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#ifndef _HALARM64_H_
#define _HALARM64_H_

/* INCLUDES *******************************************************************/

/* Basic types needed by HAL */
#ifndef _NTOSKRNL_
typedef ULONG_PTR KAFFINITY;
typedef struct _LOADER_PARAMETER_BLOCK *PLOADER_PARAMETER_BLOCK;
typedef struct _KTRAP_FRAME *PKTRAP_FRAME;
typedef struct _KEXCEPTION_FRAME *PKEXCEPTION_FRAME;
typedef struct _SYSTEM_PROCESSOR_INFORMATION *PSYSTEM_PROCESSOR_INFORMATION;
typedef struct _SYSTEM_BASIC_INFORMATION *PSYSTEM_BASIC_INFORMATION;
#endif

/* Forward declarations and ACPI types */
typedef struct _ACPI_SUBTABLE_HEADER {
    UCHAR Type;
    UCHAR Length;
} ACPI_SUBTABLE_HEADER, *PACPI_SUBTABLE_HEADER;

typedef struct _ACPI_TABLE_HEADER {
    CHAR Signature[4];
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    CHAR OemId[6];
    CHAR OemTableId[8];
    ULONG OemRevision;
    ULONG CreatorId;
    ULONG CreatorRevision;
} ACPI_TABLE_HEADER, *PACPI_TABLE_HEADER;

/* DEFINITIONS ****************************************************************/

/* ARM64 Processor Feature Flags */
#define ARM64_FEATURE_FLOATING_POINT    0x0001
#define ARM64_FEATURE_SIMD              0x0002
#define ARM64_FEATURE_CRYPTO_AES        0x0004
#define ARM64_FEATURE_CRYPTO_SHA1       0x0008
#define ARM64_FEATURE_CRYPTO_SHA2       0x0010
#define ARM64_FEATURE_CRYPTO_CRC32      0x0020
#define ARM64_FEATURE_POINTER_AUTH      0x0040
#define ARM64_FEATURE_MEMORY_TAGGING    0x0080
#define ARM64_FEATURE_SVE               0x0100
#define ARM64_FEATURE_VIRTUALIZATION    0x0200

/* ARM64 Page Sizes */
#define ARM64_PAGE_SIZE_4KB             0x01
#define ARM64_PAGE_SIZE_16KB            0x02
#define ARM64_PAGE_SIZE_64KB            0x04

/* ARM64 Cache Operations */
#define ARM64_CACHE_CLEAN               0x01
#define ARM64_CACHE_INVALIDATE          0x02
#define ARM64_CACHE_CLEAN_INVALIDATE    0x03

/* ARM64 Memory Barriers */
#define ARM64_BARRIER_SY                0x0F    /* Full system */
#define ARM64_BARRIER_ST                0x0E    /* Store */
#define ARM64_BARRIER_LD                0x0D    /* Load */
#define ARM64_BARRIER_ISH               0x0B    /* Inner Shareable */
#define ARM64_BARRIER_ISHST             0x0A    /* Inner Shareable Store */
#define ARM64_BARRIER_ISHLD             0x09    /* Inner Shareable Load */
#define ARM64_BARRIER_NSH               0x07    /* Non-shareable */
#define ARM64_BARRIER_NSHST             0x06    /* Non-shareable Store */
#define ARM64_BARRIER_NSHLD             0x05    /* Non-shareable Load */
#define ARM64_BARRIER_OSH               0x03    /* Outer Shareable */
#define ARM64_BARRIER_OSHST             0x02    /* Outer Shareable Store */
#define ARM64_BARRIER_OSHLD             0x01    /* Outer Shareable Load */

/* ARM64 Exception Levels */
#define ARM64_EL0                       0       /* User mode */
#define ARM64_EL1                       1       /* Kernel mode */
#define ARM64_EL2                       2       /* Hypervisor mode */
#define ARM64_EL3                       3       /* Secure monitor mode */

/* ARM64 Interrupt Types */
#define ARM64_IRQ_TYPE_LEVEL_LOW        0x08
#define ARM64_IRQ_TYPE_LEVEL_HIGH       0x04
#define ARM64_IRQ_TYPE_EDGE_FALLING     0x02
#define ARM64_IRQ_TYPE_EDGE_RISING      0x01
#define ARM64_IRQ_TYPE_EDGE_BOTH        0x03

/* STRUCTURES *****************************************************************/

/* ARM64 CPU Information */
typedef struct _ARM64_CPU_INFO
{
    ULONG64 PFR0;           /* Processor Feature Register 0 */
    ULONG64 PFR1;           /* Processor Feature Register 1 */
    ULONG64 DFR0;           /* Debug Feature Register 0 */
    ULONG64 DFR1;           /* Debug Feature Register 1 */
    ULONG64 ISAR0;          /* Instruction Set Attribute Register 0 */
    ULONG64 ISAR1;          /* Instruction Set Attribute Register 1 */
    ULONG64 MMFR0;          /* Memory Model Feature Register 0 */
    ULONG64 MMFR1;          /* Memory Model Feature Register 1 */
    ULONG64 MMFR2;          /* Memory Model Feature Register 2 */
    ULONG64 SVE_ZCR;        /* SVE Control Register */
} ARM64_CPU_INFO, *PARM64_CPU_INFO;

/* ARM64 Cache Information */
typedef struct _ARM64_CACHE_INFO
{
    ULONG DMinLine;         /* Data cache minimum line size */
    ULONG IMinLine;         /* Instruction cache minimum line size */
    ULONG CWG;              /* Cache Write-back Granule */
    ULONG ERG;              /* Exclusives Reservation Granule */
    ULONG L1IpPolicy;       /* L1 Instruction cache policy */
    BOOLEAN SeparateCaches; /* Separate instruction and data caches */
} ARM64_CACHE_INFO, *PARM64_CACHE_INFO;

/* ARM64 Memory Information */
typedef struct _ARM64_MEMORY_INFO
{
    ULONG PhysicalAddressBits;  /* Physical address size */
    ULONG VirtualAddressBits;   /* Virtual address size */
    ULONG PageSizes;            /* Supported page sizes */
    ULONG AsidBits;             /* ASID size */
    BOOLEAN LargePages;         /* Large page support */
    BOOLEAN SMMU;               /* SMMU present */
} ARM64_MEMORY_INFO, *PARM64_MEMORY_INFO;

/* ARM64 Timer Information */
typedef struct _ARM64_TIMER_INFO
{
    ULONG64 Frequency;          /* Timer frequency in Hz */
    ULONG PhysicalTimerIRQ;     /* Physical timer interrupt */
    ULONG VirtualTimerIRQ;      /* Virtual timer interrupt */
    ULONG HypervisorTimerIRQ;   /* Hypervisor timer interrupt */
    BOOLEAN SecureTimerPresent; /* Secure timer available */
} ARM64_TIMER_INFO, *PARM64_TIMER_INFO;

/* ARM64 GIC Information */
typedef struct _ARM64_GIC_INFO
{
    ULONG Version;              /* GIC version (2, 3, or 4) */
    PHYSICAL_ADDRESS DistributorBase;   /* Distributor base address */
    ULONG DistributorSize;      /* Distributor region size */
    PHYSICAL_ADDRESS CpuInterfaceBase;  /* CPU interface base address */
    ULONG CpuInterfaceSize;     /* CPU interface region size */
    PHYSICAL_ADDRESS RedistributorBase; /* Redistributor base (GICv3+) */
    ULONG RedistributorSize;    /* Redistributor region size */
    ULONG MaxInterrupts;        /* Maximum interrupt number */
    BOOLEAN MSISupport;         /* MSI/MSI-X support */
    BOOLEAN LPISupport;         /* LPI support (GICv3+) */
} ARM64_GIC_INFO, *PARM64_GIC_INFO;

/* ARM64 Platform Information */
typedef struct _ARM64_PLATFORM_INFO
{
    CHAR VendorString[16];      /* CPU vendor identification */
    CHAR ModelString[64];       /* CPU model string */
    ULONG SocketCount;          /* Number of processor sockets */
    ULONG CoresPerSocket;       /* Cores per socket */
    ULONG ThreadsPerCore;       /* Threads per core */
    ULONG64 SystemFeatures;     /* System feature flags */
    BOOLEAN TrustZone;          /* TrustZone support */
    BOOLEAN Virtualization;     /* Virtualization extensions */
    BOOLEAN CryptoExtensions;   /* Cryptographic extensions */
} ARM64_PLATFORM_INFO, *PARM64_PLATFORM_INFO;

/* ACPI Table Structures for ARM64 */

/* ACPI MADT (Multiple APIC Description Table) */
typedef struct _ACPI_TABLE_MADT {
    ACPI_TABLE_HEADER Header;
    ULONG LocalApicAddress;
    ULONG Flags;
} ACPI_TABLE_MADT, *PACPI_TABLE_MADT;

/* ACPI Device Information */
typedef struct _ACPI_DEVICE_INFO {
    ULONG DeviceId;
    ULONG Reserved[3];
} ACPI_DEVICE_INFO, *PACPI_DEVICE_INFO;

/* ACPI MADT GIC CPU Interface */
typedef struct _ACPI_MADT_GIC_CPU_INTERFACE
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG CpuInterfaceNumber;
    ULONG Uid;
    ULONG Flags;
    ULONG ParkingVersion;
    ULONG PerformanceInterrupt;
    ULONG64 ParkedAddress;
    ULONG64 BaseAddress;
    ULONG64 GicvBaseAddress;
    ULONG64 GichBaseAddress;
    ULONG VgicMaintenanceInterrupt;
    ULONG64 GicrBaseAddress;
    ULONG64 Mpidr;
} ACPI_MADT_GIC_CPU_INTERFACE, *PACPI_MADT_GIC_CPU_INTERFACE;

/* ACPI MADT GIC Distributor */
typedef struct _ACPI_MADT_GIC_DISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG GicId;
    ULONG64 BaseAddress;
    ULONG GlobalIrqBase;
    UCHAR Version;
    UCHAR Reserved2[3];
} ACPI_MADT_GIC_DISTRIBUTOR, *PACPI_MADT_GIC_DISTRIBUTOR;

/* ACPI MADT GIC Redistributor */
typedef struct _ACPI_MADT_GIC_REDISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG64 BaseAddress;
    ULONG Length;
} ACPI_MADT_GIC_REDISTRIBUTOR, *PACPI_MADT_GIC_REDISTRIBUTOR;

/* ACPI MADT GIC ITS */
typedef struct _ACPI_MADT_GIC_ITS
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG ItsId;
    ULONG64 BaseAddress;
    ULONG Reserved2;
} ACPI_MADT_GIC_ITS, *PACPI_MADT_GIC_ITS;

/* ACPI Generic Timer Description Table (GTDT) */
typedef struct _ACPI_TABLE_GTDT
{
    ACPI_TABLE_HEADER Header;
    ULONG64 CounterBlockAddresss;
    ULONG Reserved;
    ULONG SecureEL1Interrupt;
    ULONG SecureEL1Flags;
    ULONG NonSecureEL1Interrupt;
    ULONG NonSecureEL1Flags;
    ULONG VirtualTimerInterrupt;
    ULONG VirtualTimerFlags;
    ULONG NonSecureEL2Interrupt;
    ULONG NonSecureEL2Flags;
    ULONG64 CounterReadBlockAddress;
    ULONG PlatformTimerCount;
    ULONG PlatformTimerOffset;
    ULONG CounterFrequency;
} ACPI_TABLE_GTDT, *PACPI_TABLE_GTDT;

/* FUNCTION DECLARATIONS ******************************************************/

/* HAL Initialization */
BOOLEAN
NTAPI
HalInitializeGIC(VOID);

BOOLEAN
NTAPI
HalInitializeSystemTimer(VOID);

VOID
NTAPI
HalDetectProcessorFeatures(VOID);

VOID
NTAPI
HalInitializeCacheManager(VOID);

VOID
NTAPI
HalInitializeACPI(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
HalInitializeDMA(VOID);

NTSTATUS
NTAPI
HalInitializeIommu(VOID);

NTSTATUS
NTAPI
HalInitializePowerManagement(VOID);

NTSTATUS
NTAPI
HalInitializeProcessorPowerManagement(VOID);

VOID
NTAPI
HalInitializePerformanceMonitoring(VOID);

/* GIC Functions */
BOOLEAN
NTAPI
HalInitializeInterruptController(VOID);

BOOLEAN
NTAPI
HalInitializeGicDistributor(VOID);

BOOLEAN
NTAPI
HalInitializeGicCpuInterface(VOID);

BOOLEAN
NTAPI
HalInitializeGicv3CpuInterface(VOID);

VOID
NTAPI
HalEnableInterrupt(
    IN ULONG InterruptNumber);

VOID
NTAPI
HalDisableInterrupt(
    IN ULONG InterruptNumber);

ULONG
NTAPI
HalAcknowledgeInterrupt(VOID);

VOID
NTAPI
HalEndOfInterrupt(
    IN ULONG InterruptNumber);

VOID
NTAPI
HalSendSoftwareInterrupt(
    IN ULONG TargetCpu,
    IN ULONG InterruptNumber);

VOID
NTAPI
HalSetInterruptPriority(
    IN ULONG InterruptNumber,
    IN ULONG Priority);

/* Timer Functions */
BOOLEAN
NTAPI
HalInitializeTimer(VOID);

ULONG64
NTAPI
HalGetTimerCount(VOID);

ULONG64
NTAPI
HalGetVirtualTimerCount(VOID);

VOID
NTAPI
HalSetPhysicalTimer(
    IN ULONG64 CompareValue,
    IN BOOLEAN Enable,
    IN BOOLEAN InterruptMask);

VOID
NTAPI
HalSetVirtualTimer(
    IN ULONG64 CompareValue,
    IN BOOLEAN Enable,
    IN BOOLEAN InterruptMask);

ULONG64
NTAPI
HalGetTimerFrequency(VOID);

ULONG64
NTAPI
HalTicksToMicroseconds(
    IN ULONG64 Ticks);

ULONG64
NTAPI
HalMicrosecondsToTicks(
    IN ULONG64 Microseconds);

VOID
NTAPI
HalDelayMicroseconds(
    IN ULONG Microseconds);

BOOLEAN
NTAPI
HalTimerInterruptHandler(
    IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
HalInitializeSystemClock(
    IN ULONG ClockRate);

VOID
NTAPI
HalCalibrateTimerDelay(VOID);

VOID
NTAPI
HalStallExecution(
    IN ULONG Microseconds);

/* Interrupt Management */
BOOLEAN
NTAPI
HalInitializeInterruptSystem(VOID);

BOOLEAN
NTAPI
HalRegisterInterruptHandler(
    IN ULONG InterruptNumber,
    IN PVOID Handler,
    IN PVOID Context);

BOOLEAN
NTAPI
HalUnregisterInterruptHandler(
    IN ULONG InterruptNumber);

VOID
NTAPI
HalGetInterruptStatistics(
    OUT PULONG64 TotalInterruptCount,
    OUT PULONG64 SpuriousInterruptCount,
    OUT PULONG64 UnhandledExceptionCount);

/* Cache Management */
VOID
NTAPI
HalCleanDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length);

VOID
NTAPI
HalInvalidateDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length);

VOID
NTAPI
HalFlushDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length);

/* System Information */
VOID
NTAPI
HalInitializeSystemInformation(VOID);

VOID
NTAPI
HalGetProcessorInformation(
    OUT PSYSTEM_PROCESSOR_INFORMATION ProcessorInfo);

VOID
NTAPI
HalGetBasicInformation(
    OUT PSYSTEM_BASIC_INFORMATION BasicInfo);

ULONG64
NTAPI
HalGetProcessorCapabilities(VOID);

NTSTATUS
NTAPI
HalGetCacheInformation(
    OUT PARM64_CACHE_INFO CacheInfo);

NTSTATUS
NTAPI
HalGetMemoryInformation(
    OUT PARM64_MEMORY_INFO MemoryInfo);

/* ACPI Functions */
BOOLEAN
NTAPI
HalInitializeAcpi(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
NTAPI
HalParseAcpiTables(VOID);

PACPI_TABLE_HEADER
NTAPI
HalAcpiFindTable(
    IN ULONG Signature);

BOOLEAN
NTAPI
HalParseMadtTable(
    IN PACPI_TABLE_HEADER MadtTable);

BOOLEAN
NTAPI
HalParseGtdtTable(
    IN PACPI_TABLE_HEADER GtdtTable);

BOOLEAN
NTAPI
HalParseIortTable(
    IN PACPI_TABLE_HEADER IortTable);

VOID
NTAPI
HalParseGicCpuInterface(
    IN PACPI_MADT_GIC_CPU_INTERFACE GicCpu);

VOID
NTAPI
HalParseGicDistributor(
    IN PACPI_MADT_GIC_DISTRIBUTOR GicDistributor);

VOID
NTAPI
HalParseGicRedistributor(
    IN PACPI_MADT_GIC_REDISTRIBUTOR GicRedistributor);

VOID
NTAPI
HalParseGicIts(
    IN PACPI_MADT_GIC_ITS GicIts);

BOOLEAN
NTAPI
HalInitializeAcpiInterrupts(VOID);

BOOLEAN
NTAPI
HalInitializeAcpiTimers(VOID);

BOOLEAN
NTAPI
HalAcpiGetGicConfiguration(
    OUT PARM64_GIC_INFO GicInfo);

BOOLEAN
NTAPI
HalAcpiGetTimerConfiguration(
    OUT PARM64_TIMER_INFO TimerInfo);

BOOLEAN
NTAPI
HalAcpiIsAvailable(VOID);

/* IOMMU Functions */
NTSTATUS
NTAPI
HalRegisterSmmu(
    IN PHYSICAL_ADDRESS BaseAddress,
    IN ULONG Size);

typedef struct _ARM64_IOMMU_DOMAIN *PARM64_IOMMU_DOMAIN;

PARM64_IOMMU_DOMAIN
NTAPI
HalCreateIommuDomain(
    IN ULONG Type);

NTSTATUS
NTAPI
HalAttachDeviceToIommuDomain(
    IN PARM64_IOMMU_DOMAIN Domain,
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG StreamId);

NTSTATUS
NTAPI
HalDetachDeviceFromIommuDomain(
    IN PARM64_IOMMU_DOMAIN Domain,
    IN PDEVICE_OBJECT DeviceObject);

NTSTATUS
NTAPI
HalMapDmaBufferInIommuDomain(
    IN PARM64_IOMMU_DOMAIN Domain,
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN PVOID VirtualAddress,
    IN ULONG Length,
    IN ULONG Protection);

NTSTATUS
NTAPI
HalUnmapDmaBufferFromIommuDomain(
    IN PARM64_IOMMU_DOMAIN Domain,
    IN PVOID VirtualAddress,
    IN ULONG Length);

/* Power Management Functions */
NTSTATUS
NTAPI
HalStartProcessor(
    IN ULONG ProcessorNumber,
    IN ULONG64 Mpidr,
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId);

VOID
NTAPI
HalStopProcessor(VOID);

NTSTATUS
NTAPI
HalSuspendProcessor(
    IN ULONG PowerState,
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId);

NTSTATUS
NTAPI
HalGetCpuAffinityInfo(
    IN ULONG64 Mpidr,
    IN ULONG AffinityLevel,
    OUT PULONG AffinityState);

VOID
NTAPI
HalSystemShutdown(VOID);

VOID
NTAPI
HalSystemRestart(VOID);

NTSTATUS
NTAPI
HalSystemSuspend(
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId);

BOOLEAN
NTAPI
HalIsPsciAvailable(VOID);

ULONG
NTAPI
HalGetPsciVersion(VOID);

ULONG
NTAPI
HalGetCpuPowerState(
    IN ULONG64 Mpidr);

/* Processor Power Management Functions */
typedef struct _ARM64_PSTATE *PARM64_PSTATE;
typedef struct _ARM64_CPU_FEATURES *PARM64_CPU_FEATURES;
typedef struct _ARM64_THERMAL_INFO *PARM64_THERMAL_INFO;

NTSTATUS
NTAPI
HalRegisterProcessor(
    IN ULONG ProcessorNumber);

NTSTATUS
NTAPI
HalSetProcessorPState(
    IN ULONG ProcessorNumber,
    IN ULONG PState);

NTSTATUS
NTAPI
HalGetProcessorPStateInfo(
    IN ULONG ProcessorNumber,
    OUT PULONG CurrentPState,
    OUT PULONG AvailablePStates,
    OUT PARM64_PSTATE PStateArray,
    IN ULONG ArraySize);

NTSTATUS
NTAPI
HalGetProcessorThermalInfo(
    IN ULONG ProcessorNumber,
    OUT PARM64_THERMAL_INFO ThermalInfo);

NTSTATUS
NTAPI
HalUpdateProcessorPerformanceCounters(
    IN ULONG ProcessorNumber);

NTSTATUS
NTAPI
HalGetProcessorFeatures(
    IN ULONG ProcessorNumber,
    OUT PARM64_CPU_FEATURES Features);

NTSTATUS
NTAPI
HalEnterProcessorIdleState(
    IN ULONG ProcessorNumber,
    IN ULONG CState);

/* Inline Functions */

/* ARM64 Data Synchronization Barrier */
FORCEINLINE
VOID
HalDataSynchronizationBarrier(VOID)
{
    __asm__ __volatile__ ("dsb sy" ::: "memory");
}

/* ARM64 Data Memory Barrier */
FORCEINLINE
VOID
HalDataMemoryBarrier(VOID)
{
    __asm__ __volatile__ ("dmb sy" ::: "memory");
}

/* ARM64 Instruction Synchronization Barrier */
FORCEINLINE
VOID
HalInstructionSynchronizationBarrier(VOID)
{
    __asm__ __volatile__ ("isb" ::: "memory");
}

/* ARM64 Wait For Event */
FORCEINLINE
VOID
HalWaitForEvent(VOID)
{
    __asm__ __volatile__ ("wfe" ::: "memory");
}

/* ARM64 Wait For Interrupt */
FORCEINLINE
VOID
HalWaitForInterrupt(VOID)
{
    __asm__ __volatile__ ("wfi" ::: "memory");
}

/* ARM64 Send Event */
FORCEINLINE
VOID
HalSendEvent(VOID)
{
    __asm__ __volatile__ ("sev" ::: "memory");
}

/* ARM64 No Operation */
FORCEINLINE
VOID
HalNoOperation(VOID)
{
    __asm__ __volatile__ ("nop" ::: "memory");
}

#endif /* _HALARM64_H_ */