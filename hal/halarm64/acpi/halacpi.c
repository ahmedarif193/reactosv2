/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 ACPI Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64-specific ACPI table signatures */
#define ACPI_MADT_SIGNATURE     0x43495041  /* 'APIC' - Multiple APIC Description Table */
#define ACPI_GTDT_SIGNATURE     0x54444D47  /* 'GTDT' - Generic Timer Description Table */
#define ACPI_IORT_SIGNATURE     0x54524F49  /* 'IORT' - IO Remapping Table */
#define ACPI_PPTT_SIGNATURE     0x54545050  /* 'PPTT' - Processor Properties Topology Table */

/* MADT Interrupt Controller Types for ARM64 */
#define ACPI_MADT_TYPE_GIC              11  /* GIC CPU Interface */
#define ACPI_MADT_TYPE_GICD             12  /* GIC Distributor */
#define ACPI_MADT_TYPE_GIC_MSI          13  /* GIC MSI Frame */
#define ACPI_MADT_TYPE_GICR             14  /* GIC Redistributor */
#define ACPI_MADT_TYPE_GIC_ITS          15  /* GIC Interrupt Translation Service */

/* GLOBALS ********************************************************************/

static BOOLEAN AcpiInitialized = FALSE;
static PVOID AcpiRootSystemDescriptionPointer = NULL;
static PVOID AcpiRsdt = NULL;
static PVOID AcpiXsdt = NULL;

/* ARM64 ACPI Tables */
static PACPI_TABLE_HEADER AcpiGtdtTable = NULL;
static PACPI_TABLE_HEADER AcpiMadtTable = NULL;
static PACPI_TABLE_HEADER AcpiIortTable = NULL;

/* ACPI Configuration Data */
static ARM64_GIC_INFO AcpiGicInfo = {0};
static ARM64_TIMER_INFO AcpiTimerInfo = {0};
static BOOLEAN AcpiGicDataAvailable = FALSE;
static BOOLEAN AcpiTimerDataAvailable = FALSE;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Initialize ARM64 ACPI support
 */
BOOLEAN
NTAPI
HalInitializeAcpi(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    DPRINT("Initializing ARM64 ACPI support\n");

    /* Try to get ACPI root table pointer from the loader block extension. */
    if (LoaderBlock && LoaderBlock->Extension && LoaderBlock->Extension->AcpiTable)
    {
        PACPI_TABLE_HEADER Root = (PACPI_TABLE_HEADER)LoaderBlock->Extension->AcpiTable;
        /* The extension provides a copy of either the RSDT or XSDT. */
        if (Root)
        {
            /* Compare signature bytes to identify the root type. */
            if (Root->Signature[0] == 'X' && Root->Signature[1] == 'S' &&
                Root->Signature[2] == 'D' && Root->Signature[3] == 'T')
            {
                AcpiXsdt = Root;
                DPRINT("ACPI XSDT provided by loader at %p, length %u\n", Root, Root->Length);
            }
            else if (Root->Signature[0] == 'R' && Root->Signature[1] == 'S' &&
                     Root->Signature[2] == 'D' && Root->Signature[3] == 'T')
            {
                AcpiRsdt = Root;
                DPRINT("ACPI RSDT provided by loader at %p, length %u\n", Root, Root->Length);
            }
            else
            {
                DPRINT1("Unknown ACPI root signature '%.4s'\n", Root->Signature);
            }
        }
    }

    /* Parse ACPI tables for ARM64-specific information
     * if available. We do not require RSDP/XSDT here. */
    if (!HalParseAcpiTables())
    {
        DPRINT1("Failed to parse ARM64 ACPI tables\n");
        return FALSE;
    }

    /* Initialize ARM64-specific ACPI components */
    if (!HalInitializeAcpiInterrupts())
    {
        DPRINT1("Failed to initialize ACPI interrupt support\n");
        return FALSE;
    }

    if (!HalInitializeAcpiTimers())
    {
        DPRINT1("Failed to initialize ACPI timer support\n");
        return FALSE;
    }

    AcpiInitialized = TRUE;
    return TRUE;
}

/*
 * @brief Parse ARM64-specific ACPI tables
 */
BOOLEAN
NTAPI
HalParseAcpiTables(VOID)
{
    DPRINT("Parsing ARM64 ACPI tables\n");

    /* Parse Multiple APIC Description Table (MADT) for ARM64
     * The MADT contains information about:
     * - GIC CPU interfaces
     * - GIC Distributor
     * - GIC Redistributors (GICv3+)
     * - GIC MSI frames
     * - GIC ITS nodes
     */
    AcpiMadtTable = HalAcpiFindTable(ACPI_MADT_SIGNATURE);
    if (AcpiMadtTable)
    {
        if (!HalParseMadtTable(AcpiMadtTable))
        {
            DPRINT1("Failed to parse MADT table\n");
            return FALSE;
        }
    }
    else
    {
        DPRINT1("MADT table not found\n");
        return FALSE;
    }

    /* Parse Generic Timer Description Table (GTDT)
     * The GTDT contains information about ARM64 Generic Timers:
     * - System counter frequency
     * - Timer interrupt numbers
     * - Timer flags and capabilities
     */
    AcpiGtdtTable = HalAcpiFindTable(ACPI_GTDT_SIGNATURE);
    if (AcpiGtdtTable)
    {
        if (!HalParseGtdtTable(AcpiGtdtTable))
        {
            DPRINT1("Failed to parse GTDT table\n");
            /* This is not fatal, continue with defaults */
        }
    }
    else
    {
        DPRINT1("GTDT table not found, using defaults\n");
    }

    /* TODO: Parse IO Remapping Table (IORT) if present
     * The IORT describes IOMMU topology and device relationships
     */
    AcpiIortTable = HalAcpiFindTable(ACPI_IORT_SIGNATURE);
    if (AcpiIortTable)
    {
        HalParseIortTable(AcpiIortTable);
    }

    return TRUE;
}

/*
 * @brief Validate ACPI table checksum
 */
static
BOOLEAN
HalValidateAcpiTableChecksum(
    IN PACPI_TABLE_HEADER Table)
{
    PUCHAR BytePtr;
    UCHAR Checksum = 0;
    ULONG i;

    if (!Table)
        return FALSE;

    BytePtr = (PUCHAR)Table;
    for (i = 0; i < Table->Length; i++)
    {
        Checksum += BytePtr[i];
    }

    return (Checksum == 0);
}

/*
 * @brief Find ACPI table by signature
 */
PACPI_TABLE_HEADER
NTAPI
HalAcpiFindTable(
    IN ULONG Signature)
{
    PACPI_TABLE_HEADER Table;
    PULONG TableArray;
    ULONG TableCount, i;
    PHYSICAL_ADDRESS TablePhysical;

    DPRINT("Looking for ACPI table with signature 0x%08lx ('%.4s')\n",
           Signature, (PCHAR)&Signature);

    if (!AcpiRsdt && !AcpiXsdt)
    {
        DPRINT1("No RSDT/XSDT available\n");
        return NULL;
    }

    /* Use XSDT if available (64-bit), otherwise use RSDT (32-bit) */
    if (AcpiXsdt)
    {
        PACPI_TABLE_HEADER Xsdt = (PACPI_TABLE_HEADER)AcpiXsdt;
        PULONG64 XTableArray = (PULONG64)((PUCHAR)AcpiXsdt + sizeof(ACPI_TABLE_HEADER));

        TableCount = (Xsdt->Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(ULONG64);

        for (i = 0; i < TableCount; i++)
        {
            TablePhysical.QuadPart = XTableArray[i];
            Table = (PACPI_TABLE_HEADER)MmMapIoSpace(TablePhysical, sizeof(ACPI_TABLE_HEADER), MmNonCached);

            if (Table && Table->Signature == Signature)
            {
                /* Found the table, map the full table */
                MmUnmapIoSpace(Table, sizeof(ACPI_TABLE_HEADER));
                Table = (PACPI_TABLE_HEADER)MmMapIoSpace(TablePhysical, Table->Length, MmNonCached);

                if (Table && HalValidateAcpiTableChecksum(Table))
                {
                    DPRINT("Found ACPI table '%.4s' at physical address 0x%016llx\n",
                           (PCHAR)&Signature, TablePhysical.QuadPart);
                    return Table;
                }
                else
                {
                    DPRINT1("ACPI table checksum validation failed\n");
                    if (Table)
                        MmUnmapIoSpace(Table, Table->Length);
                }
            }

            if (Table)
                MmUnmapIoSpace(Table, sizeof(ACPI_TABLE_HEADER));
        }
    }
    else if (AcpiRsdt)
    {
        PACPI_TABLE_HEADER Rsdt = (PACPI_TABLE_HEADER)AcpiRsdt;
        TableArray = (PULONG)((PUCHAR)AcpiRsdt + sizeof(ACPI_TABLE_HEADER));

        TableCount = (Rsdt->Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(ULONG);

        for (i = 0; i < TableCount; i++)
        {
            TablePhysical.QuadPart = TableArray[i];
            Table = (PACPI_TABLE_HEADER)MmMapIoSpace(TablePhysical, sizeof(ACPI_TABLE_HEADER), MmNonCached);

            if (Table && Table->Signature == Signature)
            {
                /* Found the table, map the full table */
                MmUnmapIoSpace(Table, sizeof(ACPI_TABLE_HEADER));
                Table = (PACPI_TABLE_HEADER)MmMapIoSpace(TablePhysical, Table->Length, MmNonCached);

                if (Table && HalValidateAcpiTableChecksum(Table))
                {
                    DPRINT("Found ACPI table '%.4s' at physical address 0x%08lx\n",
                           (PCHAR)&Signature, TableArray[i]);
                    return Table;
                }
                else
                {
                    DPRINT1("ACPI table checksum validation failed\n");
                    if (Table)
                        MmUnmapIoSpace(Table, Table->Length);
                }
            }

            if (Table)
                MmUnmapIoSpace(Table, sizeof(ACPI_TABLE_HEADER));
        }
    }

    DPRINT("ACPI table '%.4s' not found\n", (PCHAR)&Signature);
    return NULL;
}

/*
 * @brief Parse MADT (Multiple APIC Description Table) for ARM64
 */
BOOLEAN
NTAPI
HalParseMadtTable(
    IN PACPI_TABLE_HEADER MadtTable)
{
    PUCHAR TableEnd, Current;
    PACPI_SUBTABLE_HEADER SubTable;

    DPRINT("Parsing ARM64 MADT table\n");

    /* TODO: Parse MADT subtables for ARM64 interrupt controllers */
    TableEnd = (PUCHAR)MadtTable + MadtTable->Length;
    Current = (PUCHAR)MadtTable + sizeof(ACPI_TABLE_MADT);

    while (Current < TableEnd)
    {
        SubTable = (PACPI_SUBTABLE_HEADER)Current;

        switch (SubTable->Type)
        {
            case ACPI_MADT_TYPE_GIC:
            {
                /* TODO: Parse GIC CPU Interface structure */
                DPRINT("Found GIC CPU Interface entry\n");
                HalParseGicCpuInterface((PACPI_MADT_GIC_CPU_INTERFACE)SubTable);
                break;
            }

            case ACPI_MADT_TYPE_GICD:
            {
                /* TODO: Parse GIC Distributor structure */
                DPRINT("Found GIC Distributor entry\n");
                HalParseGicDistributor((PACPI_MADT_GIC_DISTRIBUTOR)SubTable);
                break;
            }

            case ACPI_MADT_TYPE_GICR:
            {
                /* TODO: Parse GIC Redistributor structure (GICv3+) */
                DPRINT("Found GIC Redistributor entry\n");
                HalParseGicRedistributor((PACPI_MADT_GIC_REDISTRIBUTOR)SubTable);
                break;
            }

            case ACPI_MADT_TYPE_GIC_ITS:
            {
                /* TODO: Parse GIC ITS structure (GICv3+) */
                DPRINT("Found GIC ITS entry\n");
                HalParseGicIts((PACPI_MADT_GIC_ITS)SubTable);
                break;
            }

            default:
                DPRINT("Unknown MADT subtable type: %u\n", SubTable->Type);
                break;
        }

        Current += SubTable->Length;
    }

    return TRUE;
}

/*
 * @brief Parse GTDT (Generic Timer Description Table)
 */
BOOLEAN
NTAPI
HalParseGtdtTable(
    IN PACPI_TABLE_HEADER GtdtTable)
{
    PACPI_TABLE_GTDT Gtdt = (PACPI_TABLE_GTDT)GtdtTable;

    DPRINT("Parsing ARM64 GTDT table\n");

    DPRINT("GTDT Counter Frequency: %u Hz\n", Gtdt->CounterFrequency);
    DPRINT("GTDT Secure EL1 Timer IRQ: %u (Flags: 0x%x)\n",
           Gtdt->SecureEL1Interrupt, Gtdt->SecureEL1Flags);
    DPRINT("GTDT Non-Secure EL1 Timer IRQ: %u (Flags: 0x%x)\n",
           Gtdt->NonSecureEL1Interrupt, Gtdt->NonSecureEL1Flags);
    DPRINT("GTDT Virtual Timer IRQ: %u (Flags: 0x%x)\n",
           Gtdt->VirtualTimerInterrupt, Gtdt->VirtualTimerFlags);
    DPRINT("GTDT Non-Secure EL2 Timer IRQ: %u (Flags: 0x%x)\n",
           Gtdt->NonSecureEL2Interrupt, Gtdt->NonSecureEL2Flags);

    /* Store timer configuration from GTDT */
    if (Gtdt->CounterFrequency != 0)
    {
        AcpiTimerInfo.Frequency = Gtdt->CounterFrequency;
    }

    /* Extract timer interrupt numbers */
    AcpiTimerInfo.PhysicalTimerIRQ = Gtdt->NonSecureEL1Interrupt;
    AcpiTimerInfo.VirtualTimerIRQ = Gtdt->VirtualTimerInterrupt;
    AcpiTimerInfo.HypervisorTimerIRQ = Gtdt->NonSecureEL2Interrupt;

    /* Check if secure timer is available */
    AcpiTimerInfo.SecureTimerPresent = (Gtdt->SecureEL1Interrupt != 0) ? TRUE : FALSE;

    AcpiTimerDataAvailable = TRUE;

    DPRINT("ARM64 GTDT configuration extracted successfully\n");
    return TRUE;
}

/*
 * @brief Parse IORT (IO Remapping Table)
 */
BOOLEAN
NTAPI
HalParseIortTable(
    IN PACPI_TABLE_HEADER IortTable)
{
    UNREFERENCED_PARAMETER(IortTable);

    DPRINT("Parsing ARM64 IORT table\n");

    /* TODO: Parse IORT for IOMMU configuration
     * - SMMU (System MMU) nodes
     * - Named component nodes
     * - Root complex nodes
     * - ID mapping structures
     */

    return TRUE;
}

/*
 * @brief Parse GIC CPU Interface from MADT
 */
VOID
NTAPI
HalParseGicCpuInterface(
    IN PACPI_MADT_GIC_CPU_INTERFACE GicCpu)
{
    DPRINT("GIC CPU Interface: CPU=%u, UID=%u, Flags=0x%08x\n",
           GicCpu->CpuInterfaceNumber, GicCpu->Uid, GicCpu->Flags);
    DPRINT("  Base Address: 0x%016llx\n", GicCpu->BaseAddress);
    DPRINT("  GICV Address: 0x%016llx\n", GicCpu->GicvBaseAddress);
    DPRINT("  GICH Address: 0x%016llx\n", GicCpu->GichBaseAddress);
    DPRINT("  GICR Address: 0x%016llx\n", GicCpu->GicrBaseAddress);
    DPRINT("  MPIDR: 0x%016llx\n", GicCpu->Mpidr);

    /* Store GIC CPU interface information for the primary CPU */
    if (GicCpu->CpuInterfaceNumber == 0 || !AcpiGicDataAvailable)
    {
        AcpiGicInfo.CpuInterfaceBase.QuadPart = GicCpu->BaseAddress;
        AcpiGicInfo.CpuInterfaceSize = 0x2000;  /* Standard GICv2 CPU interface size */

        /* For GICv3+, use the redistributor base */
        if (GicCpu->GicrBaseAddress)
        {
            AcpiGicInfo.RedistributorBase.QuadPart = GicCpu->GicrBaseAddress;
            AcpiGicInfo.RedistributorSize = 0x20000;  /* Standard redistributor size */
            AcpiGicInfo.Version = 3;  /* Indicate GICv3+ */
        }
        else
        {
            AcpiGicInfo.Version = 2;  /* GICv2 */
        }

        AcpiGicDataAvailable = TRUE;
    }
}

/*
 * @brief Parse GIC Distributor from MADT
 */
VOID
NTAPI
HalParseGicDistributor(
    IN PACPI_MADT_GIC_DISTRIBUTOR GicDistributor)
{
    DPRINT("GIC Distributor: ID=%u, Address=0x%016llx, GlobalIrqBase=%u, Version=%u\n",
           GicDistributor->GicId, GicDistributor->BaseAddress,
           GicDistributor->GlobalIrqBase, GicDistributor->Version);

    /* Store GIC Distributor information */
    AcpiGicInfo.DistributorBase.QuadPart = GicDistributor->BaseAddress;
    AcpiGicInfo.DistributorSize = 0x10000;  /* Standard distributor size */

    /* Use the version from the MADT if specified */
    if (GicDistributor->Version != 0)
    {
        AcpiGicInfo.Version = GicDistributor->Version;
    }

    /* Assume standard ARM64 interrupt count if not specified */
    AcpiGicInfo.MaxInterrupts = 256;
    AcpiGicInfo.MSISupport = (GicDistributor->Version >= 3) ? TRUE : FALSE;
    AcpiGicInfo.LPISupport = (GicDistributor->Version >= 3) ? TRUE : FALSE;

    AcpiGicDataAvailable = TRUE;
}

/*
 * @brief Parse GIC Redistributor from MADT (GICv3+)
 */
VOID
NTAPI
HalParseGicRedistributor(
    IN PACPI_MADT_GIC_REDISTRIBUTOR GicRedistributor)
{
    DPRINT("GIC Redistributor: Address=0x%016llx, Length=0x%08x\n",
           GicRedistributor->BaseAddress, GicRedistributor->Length);

    if (AcpiGicInfo.RedistributorBase.QuadPart == 0)
    {
        AcpiGicInfo.RedistributorBase.QuadPart = GicRedistributor->BaseAddress;
        AcpiGicInfo.RedistributorSize = GicRedistributor->Length;
        AcpiGicDataAvailable = TRUE;
    }
}

/*
 * @brief Parse GIC ITS from MADT (GICv3+)
 */
VOID
NTAPI
HalParseGicIts(
    IN PACPI_MADT_GIC_ITS GicIts)
{
    DPRINT("GIC ITS: ID=%u, Address=0x%016llx\n",
           GicIts->ItsId, GicIts->BaseAddress);

    /* TODO: Register GIC ITS information (GICv3+)
     * - Store ITS base address
     * - Configure MSI-X interrupt translation
     * - Set up ITS command interface
     * - Initialize ITS tables (Device, Collection, etc.)
     */
}

/*
 * @brief Initialize ACPI-based interrupt support
 */
BOOLEAN
NTAPI
HalInitializeAcpiInterrupts(VOID)
{
    DPRINT("Initializing ACPI interrupt support for ARM64\n");

    /* TODO: Use ACPI information to initialize interrupts
     * - Configure GIC based on MADT information
     * - Set up interrupt routing
     * - Initialize IOMMU if present in IORT
     * - Configure MSI/MSI-X support
     */

    return TRUE;
}

/*
 * @brief Initialize ACPI-based timer support
 */
BOOLEAN
NTAPI
HalInitializeAcpiTimers(VOID)
{
    DPRINT("Initializing ACPI timer support for ARM64\n");

    /* TODO: Use GTDT information to configure timers
     * - Set timer frequencies from GTDT
     * - Configure timer interrupt numbers
     * - Set up platform timers if present
     * - Initialize watchdog timers
     */

    return TRUE;
}

/*
 * @brief Get ACPI device information
 */
NTSTATUS
NTAPI
HalAcpiGetDeviceInfo(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PACPI_DEVICE_INFO DeviceInfo)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(DeviceInfo);

    /* TODO: Implement ACPI device information retrieval
     * - Lookup device in ACPI namespace
     * - Extract device properties and resources
     * - Return formatted device information
     */

    DPRINT1("HalAcpiGetDeviceInfo not implemented for ARM64\n");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @brief Evaluate ACPI method
 */
NTSTATUS
NTAPI
HalAcpiEvaluateMethod(
    IN PDEVICE_OBJECT DeviceObject,
    IN PSTRING MethodName,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferSize,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferSize,
    OUT PULONG ActualOutputSize OPTIONAL)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MethodName);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferSize);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferSize);
    UNREFERENCED_PARAMETER(ActualOutputSize);

    /* TODO: Implement ACPI method evaluation
     * - Parse AML code for the method
     * - Execute method with provided parameters
     * - Return method results
     */

    DPRINT1("HalAcpiEvaluateMethod not implemented for ARM64\n");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @brief Get ACPI GIC configuration data
 */
BOOLEAN
NTAPI
HalAcpiGetGicConfiguration(
    OUT PARM64_GIC_INFO GicInfo)
{
    if (!AcpiGicDataAvailable || !GicInfo)
    {
        return FALSE;
    }

    /* Copy the ACPI-discovered GIC configuration */
    RtlCopyMemory(GicInfo, &AcpiGicInfo, sizeof(ARM64_GIC_INFO));

    DPRINT("Providing ACPI GIC configuration: Version=%lu, Distributor=0x%016llx\n",
           GicInfo->Version, GicInfo->DistributorBase.QuadPart);

    return TRUE;
}

/*
 * @brief Get ACPI Timer configuration data
 */
BOOLEAN
NTAPI
HalAcpiGetTimerConfiguration(
    OUT PARM64_TIMER_INFO TimerInfo)
{
    if (!AcpiTimerDataAvailable || !TimerInfo)
    {
        return FALSE;
    }

    /* Copy the ACPI-discovered timer configuration */
    RtlCopyMemory(TimerInfo, &AcpiTimerInfo, sizeof(ARM64_TIMER_INFO));

    DPRINT("Providing ACPI Timer configuration: Frequency=%llu Hz, Physical IRQ=%lu\n",
           TimerInfo->Frequency, TimerInfo->PhysicalTimerIRQ);

    return TRUE;
}

/*
 * @brief Check if ACPI is available and initialized
 */
BOOLEAN
NTAPI
HalAcpiIsAvailable(VOID)
{
    return AcpiInitialized;
}
