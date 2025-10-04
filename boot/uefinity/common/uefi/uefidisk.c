/*
 * Uefinity - Multi-Architecture UEFI Bootloader
 * Copyright (C) 2024 Ahmed ARIF (Arif193@gmail.com)
 * Based on FreeLoader UEFI Support
 *
 * PURPOSE:     UEFI Disk Access Functions (UEFI-only, no BIOS)
 * COPYRIGHT:   Original Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
// AGENT-MODIFIED: Include header for UefiEnumerateArcDisks
#include <uefi/uefiarcname.h>

#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'
/* UEFI uses 0-based disk numbering (no BIOS 0x80 offset) */
#define FIRST_UEFI_DISK 0
#define FIRST_PARTITION 1

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
} DISKCONTEXT;

typedef struct _INTERNAL_UEFI_DISK
{
    UCHAR ArcDriveNumber;
    UCHAR NumOfPartitions;
    UCHAR UefiRootNumber;
    BOOLEAN IsThisTheBootDrive;
} INTERNAL_UEFI_DISK, *PINTERNAL_UEFI_DISK;

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern EFI_HANDLE PublicBootHandle; /* Freeldr itself */

/* Made to match BIOS */
PVOID DiskReadBuffer;
UCHAR PcBiosDiskCount;

UCHAR FrldrBootDrive;
ULONG FrldrBootPartition;
SIZE_T DiskReadBufferSize;
PVOID Buffer;

static const CHAR Hex[] = "0123456789abcdef";
static CHAR PcDiskIdentifier[32][20];

/* UEFI-specific */
static ULONG UefiBootRootIdentifier;
static ULONG OffsetToBoot;
static ULONG PublicBootArcDisk;
static INTERNAL_UEFI_DISK* InternalUefiDisk = NULL;
static EFI_GUID bioGuid = BLOCK_IO_PROTOCOL;
static EFI_BLOCK_IO* bio;
static EFI_HANDLE* handles = NULL;
static ULONG GlobalSystemHandleCount = 0; /* For ARM64 boot path detection */

/* FUNCTIONS *****************************************************************/

PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    TRACE("GetHarddiskIdentifier: DriveNumber: %d\n", DriveNumber);
    return PcDiskIdentifier[DriveNumber - FIRST_UEFI_DISK];
}

static LONG lReportError = 0; // >= 0: display errors; < 0: hide errors.

LONG
DiskReportError(BOOLEAN bShowError)
{
    /* Set the reference count */
    if (bShowError) ++lReportError;
    else            --lReportError;
    return lReportError;
}

static
BOOLEAN
UefiGetBootPartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG BootPartition)
{
    ULONG PartitionNum;

    TRACE("UefiGetBootPartitionEntry: DriveNumber: %d\n", DriveNumber - FIRST_UEFI_DISK);
    /* UefiBootRoot is the offset into the array of handles where the raw disk of the boot drive is.
     * Partitions start with 1 in ARC, but UEFI root drive identitfier is also first partition. */
    PartitionNum = (OffsetToBoot - UefiBootRootIdentifier);
    if (PartitionNum == 0)
    {
        TRACE("Boot PartitionNumber is 0\n");
        /* The OffsetToBoot is equal to the RootIdentifier */
        PartitionNum = FIRST_PARTITION;
    }

    *BootPartition = PartitionNum;
    TRACE("UefiGetBootPartitionEntry: Boot Partition is: %d\n", PartitionNum);
    return TRUE;
}

static
ARC_STATUS
UefiDiskClose(ULONG FileId)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    FrLdrTempFree(Context, TAG_HW_DISK_CONTEXT);
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskGetFileInformation(ULONG FileId, FILEINFORMATION *Information)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    RtlZeroMemory(Information, sizeof(*Information));

    /*
     * The ARC specification mentions that for partitions, StartingAddress and
     * EndingAddress are the start and end positions of the partition in terms
     * of byte offsets from the start of the disk.
     * CurrentAddress is the current offset into (i.e. relative to) the partition.
     */
    Information->StartingAddress.QuadPart = Context->SectorOffset * Context->SectorSize;
    Information->EndingAddress.QuadPart   = (Context->SectorOffset + Context->SectorCount) * Context->SectorSize;
    Information->CurrentAddress.QuadPart  = Context->SectorNumber * Context->SectorSize;

    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskOpen(CHAR *Path, OPENMODE OpenMode, ULONG *FileId)
{
    DISKCONTEXT* Context;
    UCHAR DriveNumber;
    ULONG DrivePartition, SectorSize;
    ULONGLONG SectorOffset = 0;
    ULONGLONG SectorCount = 0;
    ULONG UefiDriveNumber = 0;
    PARTITION_TABLE_ENTRY PartitionTableEntry;

    TRACE("UefiDiskOpen: File ID: %d, Path: %s\n", FileId, Path);

    if (DiskReadBufferSize == 0)
    {
        ERR("DiskOpen(): DiskReadBufferSize is 0, something is wrong.\n");
        ASSERT(FALSE);
        return ENOMEM;
    }

    if (!DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition))
        return EINVAL;

    TRACE("Opening disk: DriveNumber: %d, DrivePartition: %d\n", DriveNumber, DrivePartition);
    UefiDriveNumber = DriveNumber - FIRST_UEFI_DISK;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    SectorSize = bio->Media->BlockSize;

    if (DrivePartition != 0xff && DrivePartition != 0)
    {
        /* Try to get partition entry */
        if (!DiskGetPartitionEntry(DriveNumber, DrivePartition, &PartitionTableEntry))
        {
            /* For UEFI boot, if partition 1 fails, try treating it as a raw disk */
            if (DrivePartition == 1)
            {
                TRACE("Partition 1 not found, trying raw disk access\n");
                GEOMETRY Geometry;
                if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
                    return EINVAL;

                SectorOffset = 0;
                SectorCount = Geometry.Sectors;
            }
            else
            {
                return EINVAL;
            }
        }
        else
        {
            SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
            SectorCount = PartitionTableEntry.PartitionSectorCount;
        }
    }
    else
    {
        GEOMETRY Geometry;
        if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
            return EINVAL;

        if (SectorSize != Geometry.BytesPerSector)
        {
            ERR("SectorSize (%lu) != Geometry.BytesPerSector (%lu), expect problems!\n",
                SectorSize, Geometry.BytesPerSector);
        }

        SectorOffset = 0;
        SectorCount = Geometry.Sectors;
    }

    Context = FrLdrTempAlloc(sizeof(DISKCONTEXT), TAG_HW_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->DriveNumber = DriveNumber;
    Context->SectorSize = SectorSize;
    Context->SectorOffset = SectorOffset;
    Context->SectorCount = SectorCount;
    Context->SectorNumber = 0;
    FsSetDeviceSpecific(*FileId, Context);
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskRead(ULONG FileId, VOID *Buffer, ULONG N, ULONG *Count)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    UCHAR* Ptr = (UCHAR*)Buffer;
    ULONG Length, TotalSectors, MaxSectors, ReadSectors;
    ULONGLONG SectorOffset;
    BOOLEAN ret;

    ASSERT(DiskReadBufferSize > 0);

    TotalSectors = (N + Context->SectorSize - 1) / Context->SectorSize;
    MaxSectors   = DiskReadBufferSize / Context->SectorSize;
    SectorOffset = Context->SectorOffset + Context->SectorNumber;

    // If MaxSectors is 0, this will lead to infinite loop.
    // In release builds assertions are disabled, however we also have sanity checks in DiskOpen()
    ASSERT(MaxSectors > 0);

    ret = TRUE;

    while (TotalSectors)
    {
        ReadSectors = min(TotalSectors, MaxSectors);

        ret = MachDiskReadLogicalSectors(Context->DriveNumber,
                                         SectorOffset,
                                         ReadSectors,
                                         DiskReadBuffer);
        if (!ret)
            break;

        Length = ReadSectors * Context->SectorSize;
        Length = min(Length, N);

        RtlCopyMemory(Ptr, DiskReadBuffer, Length);

        Ptr += Length;
        N -= Length;
        SectorOffset += ReadSectors;
        TotalSectors -= ReadSectors;
    }

    *Count = (ULONG)((ULONG_PTR)Ptr - (ULONG_PTR)Buffer);
    Context->SectorNumber = SectorOffset - Context->SectorOffset;

    return (ret ? ESUCCESS : EIO);
}

static
ARC_STATUS
UefiDiskSeek(ULONG FileId, LARGE_INTEGER *Position, SEEKMODE SeekMode)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (Context->SectorNumber * Context->SectorSize);
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart & (Context->SectorSize - 1))
        return EINVAL;

    /* Convert in number of sectors */
    NewPosition.QuadPart /= Context->SectorSize;

    /* HACK: CDROMs may have a SectorCount of 0 */
    if (Context->SectorCount != 0 && NewPosition.QuadPart >= Context->SectorCount)
        return EINVAL;

    Context->SectorNumber = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL UefiDiskVtbl =
{
    UefiDiskClose,
    UefiDiskGetFileInformation,
    UefiDiskOpen,
    UefiDiskRead,
    UefiDiskSeek,
};

#ifndef _ARM64_
static
VOID
GetHarddiskInformation(UCHAR DriveNumber)
{
    PMASTER_BOOT_RECORD Mbr;
    PULONG Buffer;
    ULONG i;
    ULONG Checksum;
    ULONG Signature;
    BOOLEAN ValidPartitionTable;
    CHAR ArcName[MAX_PATH];
    PARTITION_TABLE_ENTRY PartitionTableEntry;
    PCHAR Identifier = PcDiskIdentifier[DriveNumber - FIRST_UEFI_DISK];

    /* Detect disk partition type */
    DiskDetectPartitionType(DriveNumber);

    /* Read the MBR */
    if (!MachDiskReadLogicalSectors(DriveNumber, 0ULL, 1, DiskReadBuffer))
    {
        ERR("Reading MBR failed\n");
        /* We failed, use a default identifier */
        sprintf(Identifier, "UEFIDISK%d", DriveNumber - FIRST_UEFI_DISK);
        return;
    }

    Buffer = (ULONG*)DiskReadBuffer;
    Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;

    Signature = Mbr->Signature;
    TRACE("Signature: %x\n", Signature);

    /* Calculate the MBR checksum */
    Checksum = 0;
    for (i = 0; i < 512 / sizeof(ULONG); i++)
    {
        Checksum += Buffer[i];
    }
    Checksum = ~Checksum + 1;
    TRACE("Checksum: %x\n", Checksum);

    ValidPartitionTable = (Mbr->MasterBootRecordMagic == 0xAA55);

    /* Fill out the ARC disk block */
    sprintf(ArcName, "multi(0)disk(0)rdisk(%u)", DriveNumber - FIRST_UEFI_DISK);
    AddReactOSArcDiskInfo(ArcName, Signature, Checksum, ValidPartitionTable);

    sprintf(ArcName, "multi(0)disk(0)rdisk(%u)partition(0)", DriveNumber - FIRST_UEFI_DISK);
    FsRegisterDevice(ArcName, &UefiDiskVtbl);

    /* Add partitions */
    i = FIRST_PARTITION;
    DiskReportError(FALSE);
    while (DiskGetPartitionEntry(DriveNumber, i, &PartitionTableEntry))
    {
        if (PartitionTableEntry.SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            sprintf(ArcName, "multi(0)disk(0)rdisk(%u)partition(%lu)", DriveNumber - FIRST_UEFI_DISK, i);
            FsRegisterDevice(ArcName, &UefiDiskVtbl);
        }
        i++;
    }
    DiskReportError(TRUE);

    InternalUefiDisk[DriveNumber].NumOfPartitions = i;
    /* Convert checksum and signature to identifier string */
    Identifier[0] = Hex[(Checksum >> 28) & 0x0F];
    Identifier[1] = Hex[(Checksum >> 24) & 0x0F];
    Identifier[2] = Hex[(Checksum >> 20) & 0x0F];
    Identifier[3] = Hex[(Checksum >> 16) & 0x0F];
    Identifier[4] = Hex[(Checksum >> 12) & 0x0F];
    Identifier[5] = Hex[(Checksum >> 8) & 0x0F];
    Identifier[6] = Hex[(Checksum >> 4) & 0x0F];
    Identifier[7] = Hex[Checksum & 0x0F];
    Identifier[8] = '-';
    Identifier[9] = Hex[(Signature >> 28) & 0x0F];
    Identifier[10] = Hex[(Signature >> 24) & 0x0F];
    Identifier[11] = Hex[(Signature >> 20) & 0x0F];
    Identifier[12] = Hex[(Signature >> 16) & 0x0F];
    Identifier[13] = Hex[(Signature >> 12) & 0x0F];
    Identifier[14] = Hex[(Signature >> 8) & 0x0F];
    Identifier[15] = Hex[(Signature >> 4) & 0x0F];
    Identifier[16] = Hex[Signature & 0x0F];
    Identifier[17] = '-';
    Identifier[18] = (ValidPartitionTable ? 'A' : 'X');
    Identifier[19] = 0;
    TRACE("Identifier: %s\n", Identifier);
}
#endif /* !_ARM64_ */

static
VOID
UefiSetupBlockDevices(VOID)
{
    ULONG BlockDeviceIndex;
    ULONG SystemHandleCount;
    EFI_STATUS Status;
    ULONG i;

    UINTN handle_size = 0;
    PcBiosDiskCount = 0;
    UefiBootRootIdentifier = 0;

    TRACE("UefiSetupBlockDevices: Starting block device enumeration\n");

    /* 1) Setup a list of boot handles by using the LocateHandle protocol */
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);

    /* UEFI allocation for handle array (cross-arch) */
    EFI_PHYSICAL_ADDRESS HandlesAddress = 0;
    UINTN HandlePages = (handle_size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

    Status = GlobalSystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        HandlePages,
        &HandlesAddress);

    if (EFI_ERROR(Status))
    {
        TRACE("UefiSetupBlockDevices: Failed to allocate handles buffer: %ld\n", Status);
        return;
    }

    handles = (EFI_HANDLE*)(UINTN)HandlesAddress;

    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    SystemHandleCount = handle_size / sizeof(EFI_HANDLE);
    GlobalSystemHandleCount = SystemHandleCount; /* Store for later use */

    /* UEFI allocation for internal disk array (cross-arch) */
    EFI_PHYSICAL_ADDRESS DiskArrayAddress = 0;
    UINTN DiskArraySize = sizeof(INTERNAL_UEFI_DISK) * SystemHandleCount;
    UINTN DiskArrayPages = (DiskArraySize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

    Status = GlobalSystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        DiskArrayPages,
        &DiskArrayAddress);

    if (EFI_ERROR(Status))
    {
        TRACE("UefiSetupBlockDevices: Failed to allocate disk array: %ld\n", Status);
        return;
    }

    InternalUefiDisk = (INTERNAL_UEFI_DISK*)(UINTN)DiskArrayAddress;
    RtlZeroMemory(InternalUefiDisk, DiskArraySize);

    BlockDeviceIndex = 0;
    TRACE("UefiSetupBlockDevices: Found %lu block devices, PublicBootHandle=0x%p\n",
          SystemHandleCount, PublicBootHandle);

    /* 2) Parse the handle list */
    for (i = 0; i < SystemHandleCount; ++i)
    {
        Status = GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);
        if (handles[i] == PublicBootHandle)
        {
            OffsetToBoot = i; /* Drive offset in the handles list */
            TRACE("UefiSetupBlockDevices: Found boot handle at offset %lu\n", i);
        }

        if (EFI_ERROR(Status) || 
            bio == NULL ||
            bio->Media->BlockSize == 0 ||
            bio->Media->BlockSize > 4096)
        {
            TRACE("UefiSetupBlockDevices: UEFI has found a block device that failed, skipping\n");
            continue;
        }
        if (bio->Media->LogicalPartition == FALSE)
        {
            TRACE("Found root of a HDD at handle %lu (handle=0x%p)\n", i, handles[i]);
            PcBiosDiskCount++;
            InternalUefiDisk[BlockDeviceIndex].ArcDriveNumber = BlockDeviceIndex;
            InternalUefiDisk[BlockDeviceIndex].UefiRootNumber = i;

            /* Detect partition type and register devices accordingly */
            TRACE("Detecting partition type for drive %lu\n", BlockDeviceIndex);

            /* Detect partition type for this drive */
            DiskDetectPartitionType(BlockDeviceIndex + FIRST_UEFI_DISK);

            /* Register device nodes */
            CHAR ArcName[MAX_PATH];
            sprintf(ArcName, "multi(0)disk(0)rdisk(%lu)", (unsigned long)BlockDeviceIndex);
            FsRegisterDevice(ArcName, &UefiDiskVtbl);

            /* Always register partition(0) for raw disk access */
            sprintf(ArcName, "multi(0)disk(0)rdisk(%lu)partition(0)", (unsigned long)BlockDeviceIndex);
            FsRegisterDevice(ArcName, &UefiDiskVtbl);

            /* Register partition(1) for ESP access (works for both partitioned and raw disks) */
            sprintf(ArcName, "multi(0)disk(0)rdisk(%lu)partition(1)", (unsigned long)BlockDeviceIndex);
            FsRegisterDevice(ArcName, &UefiDiskVtbl);
            TRACE("Registered partition nodes for disk %u\n", BlockDeviceIndex);
            BlockDeviceIndex++;
        }
        else if (handles[i] == PublicBootHandle)
        {
            TRACE("Boot handle found at index %lu, checking if partition...\n", i);
            GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);

            /* For ESP boot, we're likely booting from a partition */
            if (bio->Media->LogicalPartition == TRUE)
            {
                TRACE("Boot device is a partition (ESP)\n");
                /* Find the root disk for this partition */
                ULONG j;
                for (j = i; j > 0; j--)
                {
                    EFI_BLOCK_IO* rootBio;
                    Status = GlobalSystemTable->BootServices->HandleProtocol(handles[j-1], &bioGuid, (void**)&rootBio);
                    if (!EFI_ERROR(Status) && rootBio && rootBio->Media->LogicalPartition == FALSE)
                    {
                        TRACE("Found root disk at index %lu for boot partition\n", j-1);
                        UefiBootRootIdentifier = j-1;
                        /* Find which disk this is */
                        for (ULONG k = 0; k < BlockDeviceIndex; k++)
                        {
                            if (InternalUefiDisk[k].UefiRootNumber == (j-1))
                            {
                                InternalUefiDisk[k].IsThisTheBootDrive = TRUE;
                                PublicBootArcDisk = k;
                                TRACE("Boot drive is ARC disk %lu\n", k);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            else if (bio->Media->LogicalPartition == FALSE)
            {
                ULONG j;

                TRACE("Found root disk as boot device at index %u\n", i);
                UefiBootRootIdentifier = i;

                for (j = 0; j <= PcBiosDiskCount; ++j)
                {
                    /* Now only of the root drive number is equal to this drive we found above */
                    if (InternalUefiDisk[j].UefiRootNumber == UefiBootRootIdentifier)
                    {
                        InternalUefiDisk[j].IsThisTheBootDrive = TRUE;
                        PublicBootArcDisk = j;
                        TRACE("Found Boot drive\n");
                    }
                }
            }
        }
    }

    TRACE("UefiSetupBlockDevices: Enumeration complete. PcBiosDiskCount=%d, PublicBootArcDisk=%lu\n",
          PcBiosDiskCount, PublicBootArcDisk);
}

static
BOOLEAN
UefiSetBootpath(VOID)
{
   TRACE("UefiSetBootpath: Setting up boot path. UefiBootRootIdentifier=%lu, PublicBootArcDisk=%lu\n",
         UefiBootRootIdentifier, PublicBootArcDisk);

   if (handles == NULL)
   {
       ERR("UefiSetBootpath: handles array is NULL!\n");
       return FALSE;
   }

   GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
   if (bio == NULL)
   {
       ERR("UefiSetBootpath: Failed to get block I/O protocol\n");
       return FALSE;
   }

   FrldrBootDrive = (FIRST_UEFI_DISK + PublicBootArcDisk);
   TRACE("UefiSetBootpath: FrldrBootDrive=0x%02x, RemovableMedia=%d, BlockSize=%lu\n",
         FrldrBootDrive, bio->Media->RemovableMedia, bio->Media->BlockSize);

   if (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048)
   {
        /* Boot Partition 0xFF is the magic value that indicates booting from CD-ROM (see isoboot.S) */
        FrldrBootPartition = 0xFF;
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%u)", PublicBootArcDisk);
   }
   else
   {
        ULONG BootPartition;
        PARTITION_TABLE_ENTRY PartitionEntry;

        /* Prefer ACPI partition info when available, otherwise fall back to ESP heuristics */
        EFI_BLOCK_IO* bootBio = NULL;
        GlobalSystemTable->BootServices->HandleProtocol(PublicBootHandle, &bioGuid, (void**)&bootBio);

        if (bootBio && bootBio->Media->LogicalPartition && OffsetToBoot > UefiBootRootIdentifier)
        {
            BootPartition = OffsetToBoot - UefiBootRootIdentifier;
        }
        else if (!bootBio || !bootBio->Media->LogicalPartition)
        {
            /* Raw disk: default to partition(1) */
            BootPartition = 1;
        }
        else if (!UefiGetBootPartitionEntry(FrldrBootDrive, &PartitionEntry, &BootPartition))
        {
            /* As a last resort, default to partition(1) */
            BootPartition = 1;
        }

        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)rdisk(%u)partition(%lu)",
                           PublicBootArcDisk, BootPartition);
    }

    TRACE("UefiSetBootpath: Boot path set to: '%s'\n", FrLdrBootPath);

    /* Register the boot device with the filesystem */
    if (FrLdrBootPath[0] != '\0')
    {
        TRACE("UefiSetBootpath: Registering boot device '%s'\n", FrLdrBootPath);
        FsRegisterDevice(FrLdrBootPath, &UefiDiskVtbl);
    }
    else
    {
        ERR("UefiSetBootpath: FrLdrBootPath is empty!\n");
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
UefiInitializeBootDevices(VOID)
{
    ULONG i = 0;

    TRACE("UefiInitializeBootDevices: Starting boot device initialization\n");

    /* UEFI allocation directly for disk buffer */
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS BufferAddress = 0;

    TRACE("Allocating disk read buffer via UEFI\n");
    DiskReadBufferSize = EFI_PAGE_SIZE;

    Status = GlobalSystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        1,
        &BufferAddress);

    if (EFI_ERROR(Status))
    {
        TRACE("Failed to allocate disk read buffer: %ld\n", Status);
        return FALSE;
    }

    DiskReadBuffer = (PVOID)(UINTN)BufferAddress;
    TRACE("Allocated disk read buffer at 0x%p\n", DiskReadBuffer);

    /* Now proceed with device setup */
    UefiSetupBlockDevices();
    UefiSetBootpath();
    
    // AGENT-MODIFIED: Enumerate all ARC disks for proper Windows boot support
    UefiEnumerateArcDisks();

    /* Add it, if it's a cdrom */
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
    if (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048)
    {
        PMASTER_BOOT_RECORD Mbr;
        PULONG Buffer;
        ULONG Checksum = 0;
        ULONG Signature;

        /* Read the MBR */
        if (!MachDiskReadLogicalSectors(FrldrBootDrive, 16ULL, 1, DiskReadBuffer))
        {
            ERR("Reading MBR failed\n");
            return FALSE;
        }

        Buffer = (ULONG*)DiskReadBuffer;
        Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;

        Signature = Mbr->Signature;
        TRACE("Signature: %x\n", Signature);

        /* Calculate the MBR checksum */
        for (i = 0; i < 2048 / sizeof(ULONG); i++)
        {
            Checksum += Buffer[i];
        }
        Checksum = ~Checksum + 1;
        TRACE("Checksum: %x\n", Checksum);

        /* Fill out the ARC disk block */
        AddReactOSArcDiskInfo(FrLdrBootPath, Signature, Checksum, TRUE);

        FsRegisterDevice(FrLdrBootPath, &UefiDiskVtbl);
        PcBiosDiskCount++; // This is not accounted for in the number of pre-enumerated BIOS drives!
        TRACE("Additional boot drive detected: 0x%02X\n", (int)FrldrBootDrive);
    }
    return TRUE;
}

UCHAR
UefiGetFloppyCount(VOID)
{
    /* No floppy for you for now... */
    return 0;
}

BOOLEAN
UefiDiskReadLogicalSectors(
    IN UCHAR DriveNumber,
    IN ULONGLONG SectorNumber,
    IN ULONG SectorCount,
    OUT PVOID Buffer)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_UEFI_DISK].UefiRootNumber;
    //TODO keep this TRACE("UefiDiskReadLogicalSectors: DriveNumber: %d\n", UefiDriveNumber);
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);

    /* Devices setup */
    bio->ReadBlocks(bio, bio->Media->MediaId, SectorNumber, SectorCount * bio->Media->BlockSize, Buffer);
    return TRUE;
}

BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_UEFI_DISK].UefiRootNumber;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    Geometry->Cylinders = 1; // Not relevant for the UEFI BIO protocol
    Geometry->Heads = 1;     // Not relevant for the UEFI BIO protocol
    Geometry->SectorsPerTrack = (bio->Media->LastBlock + 1);
    Geometry->BytesPerSector = bio->Media->BlockSize;
    Geometry->Sectors = (bio->Media->LastBlock + 1);

    return TRUE;
}

ULONG
UefiDiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    ULONG UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_UEFI_DISK].UefiRootNumber;
    TRACE("UefiDiskGetCacheableBlockCount: DriveNumber: %d\n", UefiDriveNumber);

    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    return (bio->Media->LastBlock + 1);
}
