/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 DMA Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* Forward declarations */
struct _DEVICE_OBJECT;
struct _IRP;
struct _WAIT_CONTEXT_BLOCK;
struct _ADAPTER_OBJECT;

/* Use PADAPTER_OBJECT from system headers - avoid direct structure access */

/* DEFINES ********************************************************************/

/* ARM64 DMA Controller Definitions */
#define ARM64_DMA_MAX_CHANNELS          16
#define ARM64_DMA_MAX_MAP_REGISTERS     256
#define ARM64_DMA_ALIGNMENT_MASK        0x3F    /* 64-byte alignment */
#define ARM64_DMA_MAX_TRANSFER_SIZE     0x10000000UL  /* 256MB */
#define ARM64_DMA_COHERENT_MASK         0x01
#define ARM64_DMA_BOUNCE_BUFFER_SIZE    PAGE_SIZE

/* DMA Channel States */
#define ARM64_DMA_CHANNEL_FREE          0
#define ARM64_DMA_CHANNEL_ALLOCATED     1
#define ARM64_DMA_CHANNEL_ACTIVE        2
#define ARM64_DMA_CHANNEL_ERROR         3

/* Cache Line Size Detection */
#define ARM64_CTR_EL0_DMINLINE_SHIFT    16
#define ARM64_CTR_EL0_DMINLINE_MASK     0xF
#define ARM64_CTR_EL0_IMINLINE_MASK     0xF

/* DATA STRUCTURES ************************************************************/

/* DMA Channel Information */
typedef struct _ARM64_DMA_CHANNEL
{
    ULONG State;
    ULONG ChannelNumber;
    PADAPTER_OBJECT AdapterObject;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
    BOOLEAN CacheCoherent;
    LIST_ENTRY ListEntry;
} ARM64_DMA_CHANNEL, *PARM64_DMA_CHANNEL;

/* DMA Adapter Object Extension */
typedef struct _ARM64_DMA_ADAPTER
{
    ULONG Version;
    ULONG Size;
    ULONG MaxChannels;
    ULONG AvailableChannels;
    ULONG MapRegistersPerChannel;
    BOOLEAN CoherentMemory;
    BOOLEAN ScatterGatherSupport;
    LIST_ENTRY ChannelList;
    KSPIN_LOCK ChannelLock;
    PHYSICAL_ADDRESS DmaBase;
    ULONG DmaPortWidth;
    ULONG MaximumLength;
} ARM64_DMA_ADAPTER, *PARM64_DMA_ADAPTER;

/* Scatter-Gather List Entry */
typedef struct _ARM64_SG_ENTRY
{
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
    ULONG Flags;
} ARM64_SG_ENTRY, *PARM64_SG_ENTRY;

/* GLOBALS ********************************************************************/

static ARM64_DMA_ADAPTER HalDmaAdapter;
static BOOLEAN HalDmaInitialized = FALSE;
static ULONG HalCacheLineSize = 64;  /* Default, will be detected */
static LIST_ENTRY HalDmaChannelList;
static KSPIN_LOCK HalDmaLock;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Initialize ARM64 DMA subsystem
 */
STATIC
VOID
HalpInitializeDmaSubsystem(VOID)
{
    ULONG64 CtrEl0;

    if (HalDmaInitialized)
        return;

    DPRINT("Initializing ARM64 DMA subsystem\n");

    /* Read cache line size from system register */
    __asm__ __volatile__ ("mrs %0, ctr_el0" : "=r"(CtrEl0));

    /* Extract D-cache minimum line size */
    ULONG DMinLine = (CtrEl0 >> ARM64_CTR_EL0_DMINLINE_SHIFT) & ARM64_CTR_EL0_DMINLINE_MASK;
    HalCacheLineSize = 4 << DMinLine;  /* Convert to bytes */

    DPRINT("Detected cache line size: %lu bytes\n", HalCacheLineSize);

    /* Initialize DMA adapter structure */
    RtlZeroMemory(&HalDmaAdapter, sizeof(ARM64_DMA_ADAPTER));
    HalDmaAdapter.Version = 1;
    HalDmaAdapter.Size = sizeof(ARM64_DMA_ADAPTER);
    HalDmaAdapter.MaxChannels = ARM64_DMA_MAX_CHANNELS;
    HalDmaAdapter.AvailableChannels = ARM64_DMA_MAX_CHANNELS;
    HalDmaAdapter.MapRegistersPerChannel = ARM64_DMA_MAX_MAP_REGISTERS / ARM64_DMA_MAX_CHANNELS;
    HalDmaAdapter.CoherentMemory = TRUE;  /* ARM64 systems typically support coherent DMA */
    HalDmaAdapter.ScatterGatherSupport = TRUE;
    HalDmaAdapter.MaximumLength = ARM64_DMA_MAX_TRANSFER_SIZE;

    /* Initialize channel list and lock */
    InitializeListHead(&HalDmaAdapter.ChannelList);
    KeInitializeSpinLock(&HalDmaAdapter.ChannelLock);

    InitializeListHead(&HalDmaChannelList);
    KeInitializeSpinLock(&HalDmaLock);

    HalDmaInitialized = TRUE;
    DPRINT("ARM64 DMA subsystem initialized successfully\n");
}

/*
 * @brief Allocate a DMA channel
 */
STATIC
PARM64_DMA_CHANNEL
HalpAllocateDmaChannel(
    IN PADAPTER_OBJECT AdapterObject)
{
    PARM64_DMA_CHANNEL Channel;
    KIRQL OldIrql;

    /* Ensure DMA subsystem is initialized */
    HalpInitializeDmaSubsystem();

    Channel = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(ARM64_DMA_CHANNEL),
                                   'AMDC');
    if (!Channel)
    {
        DPRINT1("Failed to allocate DMA channel structure\n");
        return NULL;
    }

    RtlZeroMemory(Channel, sizeof(ARM64_DMA_CHANNEL));
    Channel->State = ARM64_DMA_CHANNEL_ALLOCATED;
    Channel->AdapterObject = AdapterObject;

    KeAcquireSpinLock(&HalDmaLock, &OldIrql);

    if (HalDmaAdapter.AvailableChannels > 0)
    {
        Channel->ChannelNumber = ARM64_DMA_MAX_CHANNELS - HalDmaAdapter.AvailableChannels;
        HalDmaAdapter.AvailableChannels--;
        InsertTailList(&HalDmaChannelList, &Channel->ListEntry);
    }
    else
    {
        KeReleaseSpinLock(&HalDmaLock, OldIrql);
        ExFreePoolWithTag(Channel, 'AMDC');
        DPRINT1("No available DMA channels\n");
        return NULL;
    }

    KeReleaseSpinLock(&HalDmaLock, OldIrql);

    DPRINT("Allocated DMA channel %lu\n", Channel->ChannelNumber);
    return Channel;
}

/*
 * @brief Free a DMA channel
 */
STATIC
VOID
HalpFreeDmaChannel(
    IN PARM64_DMA_CHANNEL Channel)
{
    KIRQL OldIrql;

    if (!Channel)
        return;

    DPRINT("Freeing DMA channel %lu\n", Channel->ChannelNumber);

    KeAcquireSpinLock(&HalDmaLock, &OldIrql);

    RemoveEntryList(&Channel->ListEntry);
    HalDmaAdapter.AvailableChannels++;

    KeReleaseSpinLock(&HalDmaLock, OldIrql);

    ExFreePoolWithTag(Channel, 'AMDC');
}

/*
 * @brief Check if memory is DMA coherent
 */
STATIC
BOOLEAN
HalpIsDmaCoherent(
    IN PVOID VirtualAddress)
{
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Get physical address */
    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);

    /* On ARM64, coherency depends on memory attributes and system configuration
     * For simplicity, assume coherent memory for now - this should be determined
     * from system registers and memory mapping attributes in production
     */
    UNREFERENCED_PARAMETER(PhysicalAddress);
    return TRUE;
}

/*
 * @brief Perform ARM64-specific cache maintenance
 */
STATIC
VOID
HalpDmaCacheMaintenance(
    IN PVOID VirtualAddress,
    IN ULONG Length,
    IN BOOLEAN WriteToDevice,
    IN BOOLEAN CacheCoherent)
{
    if (CacheCoherent)
    {
        /* For coherent memory, minimal cache maintenance needed */
        HalDataSynchronizationBarrier();
        return;
    }

    /* Non-coherent memory requires explicit cache maintenance */
    if (WriteToDevice)
    {
        /* Clean cache before device reads from memory */
        HalCleanDcacheRange(VirtualAddress, Length);
    }
    else
    {
        /* Invalidate cache before CPU reads device-written data */
        HalInvalidateDcacheRange(VirtualAddress, Length);
    }

    /* Ensure cache operations complete before proceeding */
    HalDataSynchronizationBarrier();
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Flush DMA common buffer
 */
BOOLEAN
NTAPI
HalFlushCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress)
{
    /* Ensure DMA subsystem is initialized */
    HalpInitializeDmaSubsystem();

    DPRINT("HalFlushCommonBuffer: AdapterObject=%p, Length=%lu, VirtualAddress=%p\n",
           AdapterObject, Length, VirtualAddress);

    if (!VirtualAddress || !Length)
    {
        DPRINT1("Invalid parameters for buffer flush\n");
        return FALSE;
    }

    /* Check if memory is coherent */
    BOOLEAN CacheCoherent = HalpIsDmaCoherent(VirtualAddress);

    /* Perform cache maintenance */
    HalpDmaCacheMaintenance(VirtualAddress, Length, TRUE, CacheCoherent);

    return TRUE;
}

/*
 * @brief Allocate common buffer for DMA
 */
PVOID
NTAPI
HalAllocateCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    OUT PPHYSICAL_ADDRESS LogicalAddress,
    IN BOOLEAN CacheEnabled)
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    PHYSICAL_ADDRESS BoundaryAddressMultiple = {{0}};

    /* Initialize DMA subsystem if needed */
    HalpInitializeDmaSubsystem();

    /* Validate parameters */
    if (!Length || Length > ARM64_DMA_MAX_TRANSFER_SIZE)
    {
        DPRINT1("Invalid buffer length: %lu\n", Length);
        return NULL;
    }

    /* Ensure proper alignment for ARM64 cache operations */
    Length = (Length + HalCacheLineSize - 1) & ~(HalCacheLineSize - 1);

    DPRINT("HalAllocateCommonBuffer: Length=%lu, CacheEnabled=%d\n",
           Length, CacheEnabled);

    /* Allocate physically contiguous memory */
    VirtualAddress = MmAllocateContiguousMemory(Length, BoundaryAddressMultiple);
    if (!VirtualAddress)
    {
        DPRINT1("Failed to allocate contiguous memory for DMA buffer\n");
        return NULL;
    }

    /* Get the physical address of the allocated memory */
    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);
    /* For DMA buffers, we may want to remap with specific caching attributes */
    if (!CacheEnabled)
    {
        /* Unmap the cached version and remap as non-cached */
        MmUnmapIoSpace(VirtualAddress, Length);
        VirtualAddress = MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
        if (!VirtualAddress)
        {
            DPRINT1("Failed to map DMA buffer as non-cached\n");
            MmFreeContiguousMemory(VirtualAddress);
            return NULL;
        }
    }

    /* Return logical address (for ARM64, this is typically the physical address)
     * TODO: Handle SMMU/IOMMU address translation if present
     */
    LogicalAddress->QuadPart = PhysicalAddress.QuadPart;

    /* Handle ARM64-specific cache coherency */
    if (!CacheEnabled)
    {
        /* For non-cached buffers, clean and invalidate cache */
        HalFlushDcacheRange(VirtualAddress, Length);
    }
    else
    {
        /* For cached buffers, ensure memory ordering */
        HalDataSynchronizationBarrier();
    }

    UNREFERENCED_PARAMETER(AdapterObject);
    return VirtualAddress;
}

/*
 * @brief Free common buffer allocated for DMA
 */
VOID
NTAPI
HalFreeCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress,
    IN BOOLEAN CacheEnabled)
{
    /* Handle ARM64-specific cache maintenance before freeing */
    if (VirtualAddress && !CacheEnabled)
    {
        /* Clean cache to ensure any pending writes complete */
        HalCleanDcacheRange(VirtualAddress, Length);
        HalDataSynchronizationBarrier();
    }

    DPRINT("HalFreeCommonBuffer: VirtualAddress=%p, Length=%lu\n",
           VirtualAddress, Length);

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(CacheEnabled);

    if (VirtualAddress)
    {
        /* Unmap virtual address */
        MmUnmapIoSpace(VirtualAddress, Length);

        /* Free physical memory */
        MmFreeContiguousMemory(VirtualAddress);
    }
}

/*
 * @brief Flush adapter buffers for DMA coherency
 */
BOOLEAN
NTAPI
HalFlushAdapterBuffers(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl,
    IN PVOID MapRegisterBase,
    IN PVOID CurrentVa,
    IN ULONG Length,
    IN BOOLEAN WriteToDevice)
{
    BOOLEAN CacheCoherent;

    DPRINT("HalFlushAdapterBuffers: CurrentVa=%p, Length=%lu, WriteToDevice=%d\n",
           CurrentVa, Length, WriteToDevice);

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);

    /* Determine if memory is cache coherent */
    CacheCoherent = HalpIsDmaCoherent(CurrentVa);

    /* Perform ARM64-specific cache maintenance */
    HalpDmaCacheMaintenance(CurrentVa, Length, WriteToDevice, CacheCoherent);

    return TRUE;
}

/* HalCleanDcacheRange removed - implemented in generic/cache.c */

/* HalInvalidateDcacheRange removed - implemented in generic/cache.c */

/* HalFlushDcacheRange removed - implemented in generic/cache.c */

/*
 * @brief Get DMA adapter object
 */
PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters)
{
    PADAPTER_OBJECT AdapterObject;
    ULONG MapRegisters;

    DPRINT("HalGetAdapter: MaximumLength=%lu, DmaChannel=%lu\n",
           DeviceDescription->MaximumLength, DeviceDescription->DmaChannel);

    /* Initialize DMA subsystem */
    HalpInitializeDmaSubsystem();

    /* Validate device description */
    if (!DeviceDescription)
    {
        DPRINT1("Invalid device description\n");
        return NULL;
    }

    /* Calculate required map registers based on maximum transfer length */
    MapRegisters = BYTES_TO_PAGES(DeviceDescription->MaximumLength);
    if (MapRegisters > HalDmaAdapter.MapRegistersPerChannel)
    {
        MapRegisters = HalDmaAdapter.MapRegistersPerChannel;
    }

    /* For ARM64, return a simple stub adapter object.
     * The actual DMA operations will use ARM64-specific channel management.
     */
    AdapterObject = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(PVOID),
                                         'AMDA');
    if (!AdapterObject)
    {
        DPRINT1("Failed to allocate adapter object\n");
        return NULL;
    }

    /* Initialize as opaque handle - avoid accessing undefined structure fields */
    RtlZeroMemory(AdapterObject, sizeof(PVOID));

    /* Return number of available map registers */
    if (NumberOfMapRegisters)
    {
        *NumberOfMapRegisters = MapRegisters;
    }

    DPRINT("Allocated adapter object with %lu map registers\n", MapRegisters);
    return AdapterObject;
}

/*
 * @brief Put DMA adapter object
 */
VOID
NTAPI
HalPutDmaAdapter(
    IN PADAPTER_OBJECT AdapterObject)
{
    DPRINT("HalPutDmaAdapter: AdapterObject=%p\n", AdapterObject);

    if (!AdapterObject)
        return;

    /* Free any associated resources */
    /* TODO: Clean up SMMU/IOMMU mappings when implemented */

    /* Free the adapter object */
    ExFreePoolWithTag(AdapterObject, 'AMDA');
}

/*
 * @brief Allocate map registers for DMA
 */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject,
    IN PWAIT_CONTEXT_BLOCK WaitContextBlock,
    IN ULONG NumberOfMapRegisters,
    IN PDRIVER_CONTROL ExecutionRoutine)
{
    PARM64_DMA_CHANNEL Channel;
    NTSTATUS Status;

    DPRINT("HalAllocateAdapterChannel: NumberOfMapRegisters=%lu\n",
           NumberOfMapRegisters);

    if (!AdapterObject || !ExecutionRoutine)
    {
        DPRINT1("Invalid parameters for adapter channel allocation\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if requested map registers are available */
    if (NumberOfMapRegisters > HalDmaAdapter.MapRegistersPerChannel)
    {
        DPRINT1("Requested too many map registers: %lu\n", NumberOfMapRegisters);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Allocate DMA channel */
    Channel = HalpAllocateDmaChannel(AdapterObject);
    if (!Channel)
    {
        DPRINT1("Failed to allocate DMA channel\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Set channel as active */
    Channel->State = ARM64_DMA_CHANNEL_ACTIVE;

    /* Call the execution routine with proper parameters from WaitContextBlock */
    if (WaitContextBlock)
    {
        Status = ExecutionRoutine(WaitContextBlock->DeviceObject,
                                 WaitContextBlock->DeviceContext,
                                 (PVOID)Channel,  /* Use channel as map register base */
                                 WaitContextBlock->DeviceContext);
    }
    else
    {
        /* Fallback - this shouldn't happen in normal operation */
        DPRINT1("HalAllocateAdapterChannel called with NULL WaitContextBlock\n");
        HalpFreeDmaChannel(Channel);
        return STATUS_INVALID_PARAMETER;
    }

    UNREFERENCED_PARAMETER(WaitContextBlock);

    return Status;
}

/*
 * @brief Free map registers
 */
VOID
NTAPI
HalFreeAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject)
{
    PARM64_DMA_CHANNEL Channel;

    DPRINT("HalFreeAdapterChannel: AdapterObject=%p\n", AdapterObject);

    if (!AdapterObject)
        return;

    /* Find and free the associated channel
     * Note: This is a simplified approach. In production, we'd need
     * to properly track the association between adapter and channel.
     */
    /* TODO: Implement proper channel tracking and cleanup */

    UNREFERENCED_PARAMETER(Channel);
}

/*
 * @brief Read DMA counter
 */
ULONG
NTAPI
HalReadDmaCounter(
    IN PADAPTER_OBJECT AdapterObject)
{
    DPRINT("HalReadDmaCounter: AdapterObject=%p\n", AdapterObject);

    if (!AdapterObject)
        return 0;

    /* For ARM64 systems without dedicated DMA controllers,
     * transfers are typically handled by the CPU and complete immediately.
     * Return 0 to indicate transfer completion.
     *
     * TODO: For systems with dedicated DMA controllers (like PL330),
     * implement actual counter reading.
     */
    return 0;
}