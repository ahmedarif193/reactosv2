/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Memory Management Functions (UEFI)
 * NOTE:        Reworked for robust GetMemoryMap handling and SAFE sizing.
 * COPYRIGHT:   Original: 2022 Justin Miller
 *              Rework:   2025 Ahmed ARIF <arif193@gmail.com>
 */

#include <uefildr.h>
#include <arch/arch_interface.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

/*--------------------------------- Helpers ---------------------------------*/

#define NEXT_MEMORY_DESCRIPTOR(Descriptor, DescriptorSize) \
    ((EFI_MEMORY_DESCRIPTOR *)((char *)(Descriptor) + (DescriptorSize)))

/* Slack to absorb growth caused by allocations done while fetching the map */
#define MAP_SLACK_DESCRIPTORS 8
/* Reasonable seed when first probe doesn’t return a size */
#define MAP_FALLBACK_BYTES    (16 * 1024)
/* Extra room when converting to FreeLdr descriptors (splits/merges) */
#define FREELDR_EXTRA_DESCS   8
/* EXIT_BOOT_SERVICES stack scratch size (unchanged) */
#define EXIT_STACK_SIZE       0x1000

/*--------------------------------- Externals --------------------------------*/

extern ULONG LoaderPagesSpanned;
extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern REACTOS_INTERNAL_BGCONTEXT framebufferData;

extern char __ImageBase;

/* From your other unit */
extern ULONG
AddMemoryDescriptor(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR List,
    _In_    ULONG MaxCount,
    _In_    PFN_NUMBER BasePage,
    _In_    PFN_NUMBER PageCount,
    _In_    TYPE_OF_MEMORY MemoryType);

/*--------------------------------- Globals ---------------------------------*/

EFI_MEMORY_DESCRIPTOR *EfiMemoryMap = NULL;
static UINTN                  EfiMapBytes  = 0;
static UINTN                  EfiDescSize  = 0;

static UINT32                 gFreeldrDescCapacity = 0; /* REAL capacity we allocated */

UINT32 FreeldrDescCount = 0; /* preserved global (used by callers) */

PVOID     OsLoaderBase;
SIZE_T    OsLoaderSize;
EFI_HANDLE PublicBootHandle;

/* Declared elsewhere */
void _exituefi(VOID);

/*--------------------------------- Forward ---------------------------------*/

static VOID
PUEFI_LoadMemoryMap(
    _Out_ UINTN  *LocMapKey,
    _Out_ UINTN  *LocMapSize,
    _Out_ UINTN  *LocDescriptorSize,
    _Out_ UINT32 *LocDescriptorVersion);

static VOID
UefiSetMemory(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    _In_    ULONG_PTR BaseAddress,
    _In_    PFN_COUNT SizeInPages,
    _In_    TYPE_OF_MEMORY MemoryType);

static TYPE_OF_MEMORY
UefiConvertToFreeldrDesc(_In_ EFI_MEMORY_TYPE EfiMemoryType);

/*--------------------------------- Impl ------------------------------------*/

/* Robust, spec‑conformant memory‑map fetch:
 *  - Expect EFI_BUFFER_TOO_SMALL for the probe.
 *  - Allocate with slack, retry until EFI_SUCCESS.
 *  - Trim byte size to an exact number of descriptors.
 */
static
VOID
PUEFI_LoadMemoryMap(
    _Out_ UINTN  *LocMapKey,
    _Out_ UINTN  *LocMapSize,
    _Out_ UINTN  *LocDescriptorSize,
    _Out_ UINT32 *LocDescriptorVersion)
{
    EFI_STATUS Status;
    UINTN  MapKey = 0, MapSize = 0, DescSize = 0;
    UINT32 DescVer = 0;

    /* Initial probe: most firmwares return EFI_BUFFER_TOO_SMALL with sizes. */
    Status = GlobalSystemTable->BootServices->GetMemoryMap(&MapSize,
                                                           NULL,
                                                           &MapKey,
                                                           &DescSize,
                                                           &DescVer);

    TRACE("GetMemoryMap probe: Status=%I64x (BUFFER_TOO_SMALL=%I64x) MapSize=%I64u DescSize=%I64u\n",
          (unsigned long long)(UINTN)Status,
          (unsigned long long)(UINTN)EFI_BUFFER_TOO_SMALL,
          (unsigned long long)(UINTN)MapSize,
          (unsigned long long)(UINTN)DescSize);

    if (Status != EFI_BUFFER_TOO_SMALL && Status != EFI_SUCCESS)
    {
        /* Some implementations require non‑NULL buffer and return INVALID_PARAMETER. */
        if (MapSize == 0)  MapSize = MAP_FALLBACK_BYTES;
        if (DescSize < sizeof(EFI_MEMORY_DESCRIPTOR))
            DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);
        DescVer = 1;
        TRACE("Probe abnormal (%I64x). Using fallback MapSize=%I64u DescSize=%I64u\n",
              (unsigned long long)(UINTN)Status,
              (unsigned long long)(UINTN)MapSize,
              (unsigned long long)(UINTN)DescSize);
    }

    /* Retry loop: allocate with slack, fetch, grow if needed. */
    for (;;)
    {
        if (DescSize < sizeof(EFI_MEMORY_DESCRIPTOR))
            DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);

        UINTN Capacity = MapSize + (DescSize * MAP_SLACK_DESCRIPTORS);

        if (EfiMemoryMap)
        {
            GlobalSystemTable->BootServices->FreePool(EfiMemoryMap);
            EfiMemoryMap = NULL;
        }

        Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                               Capacity,
                                                               (VOID **)&EfiMemoryMap);
        if (EFI_ERROR(Status) || !EfiMemoryMap)
        {
            TRACE("AllocatePool(EfiMemoryMap, %I64u) failed: %I64x\n",
                  (unsigned long long)(UINTN)Capacity,
                  (unsigned long long)(UINTN)Status);
            UiMessageBoxCritical("Unable to initialize memory manager.");
            FrLdrBugCheckWithMessage(0, __FILE__, __LINE__,
                                     "AllocatePool for memory map failed: %I64x",
                                     (unsigned long long)(UINTN)Status);
        }

        UINTN TmpSize = Capacity;
        Status = GlobalSystemTable->BootServices->GetMemoryMap(&TmpSize,
                                                               EfiMemoryMap,
                                                               &MapKey,
                                                               &DescSize,
                                                               &DescVer);

        if (Status == EFI_SUCCESS)
        {
            /* Trim to a whole number of descriptors. */
            TmpSize -= (TmpSize % DescSize);

            *LocMapKey            = MapKey;
            *LocMapSize           = TmpSize;
            *LocDescriptorSize    = DescSize;
            *LocDescriptorVersion = DescVer;

            EfiMapBytes = TmpSize;
            EfiDescSize = DescSize;

            TRACE("MapKey=%I64x MapSize=%I64u DescSize=%I64u DescVer=%lu\n",
                  (unsigned long long)(UINTN)MapKey,
                  (unsigned long long)(UINTN)TmpSize,
                  (unsigned long long)(UINTN)DescSize,
                  (UINT32)DescVer);
            return;
        }

        if (Status != EFI_BUFFER_TOO_SMALL)
        {
            TRACE("GetMemoryMap failed: %I64x\n",
                  (unsigned long long)(UINTN)Status);
            UiMessageBoxCritical("Unable to initialize memory manager.");
            FrLdrBugCheckWithMessage(0, __FILE__, __LINE__,
                                     "GetMemoryMap failed: %I64x",
                                     (unsigned long long)(UINTN)Status);
        }

        /* Grew meanwhile: try again with the REQUIRED size returned in TmpSize. */
        MapSize = TmpSize;
    }
}

static
VOID
UefiSetMemory(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    _In_    ULONG_PTR BaseAddress,
    _In_    PFN_COUNT SizeInPages,
    _In_    TYPE_OF_MEMORY MemoryType)
{
    const PFN_NUMBER BasePage  = (PFN_NUMBER)(BaseAddress >> EFI_PAGE_SHIFT);
    const PFN_NUMBER PageCount = (PFN_NUMBER)SizeInPages;

    TRACE("UEFIMEM: SetMemory Base=0x%I64x Page=0x%I64x Count=0x%I64x Type=%u\n",
          (unsigned long long)(unsigned long long)BaseAddress,
          (unsigned long long)BasePage,
          (unsigned long long)PageCount,
          (unsigned)MemoryType);

    /* CRITICAL: pass the REAL capacity we allocated, not a huge constant. */
    FreeldrDescCount = AddMemoryDescriptor(MemoryMap,
                                           (ULONG)gFreeldrDescCapacity,
                                           BasePage,
                                           PageCount,
                                           MemoryType);

    TRACE("UEFIMEM: After AddMemoryDescriptor -> FreeldrDescCount=%u (cap=%u)\n",
          (unsigned)FreeldrDescCount, (unsigned)gFreeldrDescCapacity);
}

static
TYPE_OF_MEMORY
UefiConvertToFreeldrDesc(_In_ EFI_MEMORY_TYPE EfiMemoryType)
{
    switch (EfiMemoryType)
    {
        case EfiReservedMemoryType:        return LoaderReserve;
        case EfiLoaderCode:                return LoaderLoadedProgram;
        case EfiLoaderData:                return LoaderLoadedProgram;
        case EfiBootServicesCode:          return LoaderFirmwareTemporary;
        case EfiBootServicesData:          return LoaderFirmwareTemporary;
        case EfiRuntimeServicesCode:       return LoaderFirmwarePermanent;
        case EfiRuntimeServicesData:       return LoaderFirmwarePermanent;
        case EfiConventionalMemory:        return LoaderFree;
        case EfiUnusableMemory:            return LoaderBad;
        case EfiACPIReclaimMemory:         return LoaderFirmwareTemporary;
        case EfiACPIMemoryNVS:             return LoaderReserve;
        case EfiMemoryMappedIO:            return LoaderReserve;
        case EfiMemoryMappedIOPortSpace:   return LoaderReserve;
        default:                           return LoaderReserve;
    }
}

PFREELDR_MEMORY_DESCRIPTOR
UefiMemGetMemoryMap(_Out_ ULONG *MemoryMapSize /* OUT: number of entries */)
{
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_GUID EfiLoadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    UINTN  MapKey = 0, MapBytes = 0, DescSize = 0;
    UINT32 DescVersion = 0;

    EFI_STATUS Status;
    PFREELDR_MEMORY_DESCRIPTOR FreeldrMem = NULL;

    FreeldrDescCount     = 0;
    gFreeldrDescCapacity = 0;

    /* Identify our image for base/size and the boot device. */
    Status = GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                             &EfiLoadedImageProtocol,
                                                             (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage)
    {
        TRACE("HandleProtocol(LOADED_IMAGE) failed: %I64x\n",
              (unsigned long long)(UINTN)Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    OsLoaderBase     = LoadedImage->ImageBase;
    OsLoaderSize     = LoadedImage->ImageSize;
    PublicBootHandle = LoadedImage->DeviceHandle;

    TRACE("UefiMemGetMemoryMap: fetching firmware memory map…\n");
    PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);

    const UINTN EntryCountN = (DescSize ? (MapBytes / DescSize) : 0);
    const SIZE_T each = sizeof(FREELDR_MEMORY_DESCRIPTOR);

    /* Capacity = firmware entries + some headroom for splits/merges and page 0 */
    SIZE_T FreeldrEntriesCap = (SIZE_T)EntryCountN + FREELDR_EXTRA_DESCS + 1;
    if (FreeldrEntriesCap < EntryCountN)  /* overflow guard */
        FreeldrEntriesCap = (SIZE_T)EntryCountN;

    SIZE_T FreeldrBytes = each * FreeldrEntriesCap;

    TRACE("FW map: bytes=%I64u descSize=%I64u entries=%I64u | FreeLdr cap=%I64u (bytes=%I64u)\n",
          (unsigned long long)(UINTN)MapBytes,
          (unsigned long long)(UINTN)DescSize,
          (unsigned long long)(UINTN)EntryCountN,
          (unsigned long long)FreeldrEntriesCap,
          (unsigned long long)FreeldrBytes);

    Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                           FreeldrBytes,
                                                           (VOID **)&FreeldrMem);
    if (EFI_ERROR(Status) || !FreeldrMem)
    {
        TRACE("AllocatePool(FreeldrMem %I64u bytes) failed: %I64x\n",
              (unsigned long long)FreeldrBytes,
              (unsigned long long)(UINTN)Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    RtlZeroMemory(FreeldrMem, FreeldrBytes);
    gFreeldrDescCapacity = (UINT32)FreeldrEntriesCap;

    /* Walk the EFI map and translate. */
    EFI_MEMORY_DESCRIPTOR *MapEntry = (EFI_MEMORY_DESCRIPTOR *)EfiMemoryMap;
    TRACE("UEFIMEM: translating %I64u EFI descriptors\n",
          (unsigned long long)(UINTN)EntryCountN);

    for (UINTN i = 0; i < EntryCountN; ++i)
    {
        TYPE_OF_MEMORY Mt = UefiConvertToFreeldrDesc(MapEntry->Type);

        TRACE("UEFIMEM: FW[%I64u]: Type=%u -> %u, PhysStart=0x%I64x, Pages=0x%I64x\n",
              (unsigned long long)i,
              (unsigned)MapEntry->Type,
              (unsigned)Mt,
              (unsigned long long)MapEntry->PhysicalStart,
              (unsigned long long)MapEntry->NumberOfPages);

#if 0 /* DISABLED: pinning entire free ranges is unnecessary and risky */
        if (Mt == LoaderFree)
        {
            EFI_PHYSICAL_ADDRESS addr = MapEntry->PhysicalStart;
            EFI_STATUS Res =
                GlobalSystemTable->BootServices->AllocatePages(AllocateAddress,
                                                               EfiLoaderData,
                                                               (UINTN)MapEntry->NumberOfPages,
                                                               &addr);
            if (EFI_ERROR(Res))
                Mt = LoaderFirmwareTemporary; /* not pinned */
        }
#endif

        if (Mt == LoaderLoadedProgram)
        {
            UINTN end = (UINTN)(MapEntry->PhysicalStart +
                                (MapEntry->NumberOfPages << EFI_PAGE_SHIFT));
            PFN_NUMBER last = (PFN_NUMBER)(end >> EFI_PAGE_SHIFT);
            if (last > LoaderPagesSpanned)
                LoaderPagesSpanned = (ULONG)last; /* note: LoaderPagesSpanned is ULONG */
        }

        /* We do not expose LoaderReserve to our allocator. */
        if (Mt != LoaderReserve)
        {
            UefiSetMemory(FreeldrMem,
                          (ULONG_PTR)MapEntry->PhysicalStart,
                          (PFN_COUNT)MapEntry->NumberOfPages,
                          Mt);
        }

        MapEntry = NEXT_MEMORY_DESCRIPTOR(MapEntry, DescSize);
    }

    /* Reserve page 0 for Windows/NT compatibility */
    TRACE("UEFIMEM: Reserving page 0\n");
    UefiSetMemory(FreeldrMem, 0, 1, LoaderFirmwarePermanent);

    /* Ensure sentinel remains zeroed past last (buffer was zeroed) */
    *MemoryMapSize = FreeldrDescCount;

    TRACE("UEFIMEM: translation complete: %u descriptors\n", (unsigned)FreeldrDescCount);
    return FreeldrMem;
}

VOID
UefiExitBootServices(VOID)
{
    EFI_STATUS Status;
    UINTN MapKey = 0, MapBytes = 0, DescSize = 0;
    UINT32 DescVersion = 0;

    TRACE("Attempting to ExitBootServices\n");

    /* Install exception handlers now that we are about to leave
       Boot Services and stop using firmware console paths. */
    if (ArchInterface.ArchSetupExceptions)
    {
        ArchInterface.ArchSetupExceptions();
    }

    /* Per spec: fetch a fresh map/key immediately before ExitBootServices. */
    PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);

    Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);

    if (EFI_ERROR(Status))
    {
        TRACE("ExitBootServices first attempt failed: %I64x — retrying with fresh key\n",
              (unsigned long long)(UINTN)Status);
        PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);
        Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);
    }

    if (EFI_ERROR(Status))
    {
        TRACE("ExitBootServices failed: %I64x\n",
              (unsigned long long)(UINTN)Status);
        FrLdrBugCheckWithMessage(EXIT_BOOTSERVICES_FAILURE,
                                 __FILE__,
                                 __LINE__,
                                 "ExitBootServices failed: %I64x",
                                 (unsigned long long)(UINTN)Status);
    }
    else
    {
        TRACE("Exited boot services\n");
        UefiConsMarkBootServicesExited(); /* for GOP fallback in console */
    }
}

VOID
UefiPrepareForReactOS(VOID)
{
    _exituefi();
}
