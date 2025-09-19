/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>
#include <fs/fat.h>
#include <uefildr.h>

#include <debug.h>

DBG_DEFAULT_CHANNEL(DISK);

#define MaxDriveNumber 0xFF
static PARTITION_STYLE DiskPartitionType[MaxDriveNumber + 1];

/* BRFR signature at disk offset 0x600 */
#define XBOX_SIGNATURE_SECTOR 3
#define XBOX_SIGNATURE        ('B' | ('R' << 8) | ('F' << 16) | ('R' << 24))

/* Default hardcoded partition number to boot from Xbox disk */
#define FATX_DATA_PARTITION 1

static struct
{
    ULONG SectorCountBeforePartition;
    ULONG PartitionSectorCount;
    UCHAR SystemIndicator;
} XboxPartitions[] =
{
    /* This is in the \Device\Harddisk0\Partition.. order used by the Xbox kernel */
    { 0x0055F400, 0x0098F800, PARTITION_FAT32  }, /* Store , E: */
    { 0x00465400, 0x000FA000, PARTITION_FAT_16 }, /* System, C: */
    { 0x00000400, 0x00177000, PARTITION_FAT_16 }, /* Cache1, X: */
    { 0x00177400, 0x00177000, PARTITION_FAT_16 }, /* Cache2, Y: */
    { 0x002EE400, 0x00177000, PARTITION_FAT_16 }  /* Cache3, Z: */
};

static BOOLEAN
DiskReadBootRecord(
    IN UCHAR DriveNumber,
    IN ULONGLONG LogicalSectorNumber,
    OUT PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    /* Read master boot record */
    if (!UefiDiskReadLogicalSectors(DriveNumber, LogicalSectorNumber, 1, DiskReadBuffer))
    {
        return FALSE;
    }
    RtlCopyMemory(BootRecord, DiskReadBuffer, sizeof(MASTER_BOOT_RECORD));

    TRACE("Dumping partition table for drive 0x%x:\n", DriveNumber);
    TRACE("Boot record logical start sector = %d\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x.\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < 4; Index++)
    {
        TRACE("-------------------------------------------\n");
        TRACE("Partition %d\n", (Index + 1));
        TRACE("BootIndicator: 0x%x\n", BootRecord->PartitionTable[Index].BootIndicator);
        TRACE("StartHead: 0x%x\n", BootRecord->PartitionTable[Index].StartHead);
        TRACE("StartSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].StartSector);
        TRACE("StartCylinder: 0x%x\n", BootRecord->PartitionTable[Index].StartCylinder);
        TRACE("SystemIndicator: 0x%x\n", BootRecord->PartitionTable[Index].SystemIndicator);
        TRACE("EndHead: 0x%x\n", BootRecord->PartitionTable[Index].EndHead);
        TRACE("EndSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].EndSector);
        TRACE("EndCylinder: 0x%x\n", BootRecord->PartitionTable[Index].EndCylinder);
        TRACE("SectorCountBeforePartition: 0x%x\n", BootRecord->PartitionTable[Index].SectorCountBeforePartition);
        TRACE("PartitionSectorCount: 0x%x\n", BootRecord->PartitionTable[Index].PartitionSectorCount);
    }

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

static BOOLEAN
DiskGetFirstPartitionEntry(
    IN PMASTER_BOOT_RECORD MasterBootRecord,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it's not an extended or unused partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_ENTRY_UNUSED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_EXTENDED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_XINT13_EXTENDED))
        {
            RtlCopyMemory(PartitionTableEntry, &MasterBootRecord->PartitionTable[Index], sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
DiskGetFirstExtendedPartitionEntry(
    IN PMASTER_BOOT_RECORD MasterBootRecord,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it an extended partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_EXTENDED) ||
            (MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_XINT13_EXTENDED))
        {
            RtlCopyMemory(PartitionTableEntry, &MasterBootRecord->PartitionTable[Index], sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
DiskGetActivePartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG ActivePartition)
{
    ULONG BootablePartitionCount = 0;
    ULONG CurrentPartitionNumber;
    ULONG Index;
    MASTER_BOOT_RECORD MasterBootRecord;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;

    *ActivePartition = 0;

    /* Read master boot record */
    if (!DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        return FALSE;
    }

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;

            /* Test if this is the bootable partition */
            if (ThisPartitionTableEntry->BootIndicator == 0x80)
            {
                BootablePartitionCount++;
                *ActivePartition = CurrentPartitionNumber;

                /* Copy the partition table entry */
                RtlCopyMemory(PartitionTableEntry, ThisPartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
            }
        }
    }

    /* Make sure there was only one bootable partition */
    if (BootablePartitionCount == 0)
    {
        ERR("No bootable (active) partitions found.\n");
        return FALSE;
    }
    else if (BootablePartitionCount != 1)
    {
        ERR("Too many bootable (active) partitions found.\n");
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
DiskGetMbrPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    PARTITION_TABLE_ENTRY ExtendedPartitionTableEntry;
    ULONG ExtendedPartitionNumber;
    ULONG ExtendedPartitionOffset;
    ULONG Index;
    ULONG CurrentPartitionNumber;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;

    /* Read master boot record */
    if (!DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        return FALSE;
    }

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;
        }

        if (PartitionNumber == CurrentPartitionNumber)
        {
            RtlCopyMemory(PartitionTableEntry, ThisPartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    /*
     * They want an extended partition entry so we will need
     * to loop through all the extended partitions on the disk
     * and return the one they want.
     */
    ExtendedPartitionNumber = PartitionNumber - CurrentPartitionNumber - 1;

    /*
     * Set the initial relative starting sector to 0.
     * This is because extended partition starting
     * sectors a numbered relative to their parent.
     */
    ExtendedPartitionOffset = 0;

    for (Index = 0; Index <= ExtendedPartitionNumber; Index++)
    {
        /* Get the extended partition table entry */
        if (!DiskGetFirstExtendedPartitionEntry(&MasterBootRecord, &ExtendedPartitionTableEntry))
        {
            return FALSE;
        }

        /* Adjust the relative starting sector of the partition */
        ExtendedPartitionTableEntry.SectorCountBeforePartition += ExtendedPartitionOffset;
        if (ExtendedPartitionOffset == 0)
        {
            /* Set the start of the parrent extended partition */
            ExtendedPartitionOffset = ExtendedPartitionTableEntry.SectorCountBeforePartition;
        }
        /* Read the partition boot record */
        if (!DiskReadBootRecord(DriveNumber, ExtendedPartitionTableEntry.SectorCountBeforePartition, &MasterBootRecord))
        {
            return FALSE;
        }

        /* Get the first real partition table entry */
        if (!DiskGetFirstPartitionEntry(&MasterBootRecord, PartitionTableEntry))
        {
            return FALSE;
        }

        /* Now correct the start sector of the partition */
        PartitionTableEntry->SectorCountBeforePartition += ExtendedPartitionTableEntry.SectorCountBeforePartition;
    }

    /*
     * When we get here we should have the correct entry already
     * stored in PartitionTableEntry, so just return TRUE.
     */
    return TRUE;
}

static BOOLEAN
DiskGetBrfrPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    /*
     * Get partition entry of an Xbox-standard BRFR partitioned disk.
     */
    if (PartitionNumber >= 1 && PartitionNumber <= sizeof(XboxPartitions) / sizeof(XboxPartitions[0]) &&
        UefiDiskReadLogicalSectors(DriveNumber, XBOX_SIGNATURE_SECTOR, 1, DiskReadBuffer))
    {
        if (*((PULONG)DiskReadBuffer) != XBOX_SIGNATURE)
        {
            /* No magic Xbox partitions */
            return FALSE;
        }

        RtlZeroMemory(PartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
        PartitionTableEntry->SystemIndicator = XboxPartitions[PartitionNumber - 1].SystemIndicator;
        PartitionTableEntry->SectorCountBeforePartition = XboxPartitions[PartitionNumber - 1].SectorCountBeforePartition;
        PartitionTableEntry->PartitionSectorCount = XboxPartitions[PartitionNumber - 1].PartitionSectorCount;
        return TRUE;
    }

    /* Partition does not exist */
    return FALSE;
}

VOID
DiskDetectPartitionType(
    IN UCHAR DriveNumber)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    ULONG Index;
    ULONG PartitionCount = 0;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;
    BOOLEAN GPTProtect = FALSE;
    PARTITION_TABLE_ENTRY PartitionTableEntry;

    /* First, check if this is a raw FAT32 volume (e.g., ESP without partition table) */
    if (UefiDiskReadLogicalSectors(DriveNumber, 0, 1, DiskReadBuffer))
    {
        PFAT32_BOOTSECTOR Fat32BootSector = (PFAT32_BOOTSECTOR)DiskReadBuffer;
        PFAT_BOOTSECTOR FatBootSector = (PFAT_BOOTSECTOR)DiskReadBuffer;

        /* Check for FAT32 signature */
        if (Fat32BootSector->BootSectorMagic == 0xAA55 &&
            Fat32BootSector->BytesPerSector >= 512 &&
            Fat32BootSector->SectorsPerCluster > 0 &&
            Fat32BootSector->NumberOfFats > 0 &&
            (Fat32BootSector->FileSystemType[0] == 'F' &&
             Fat32BootSector->FileSystemType[1] == 'A' &&
             Fat32BootSector->FileSystemType[2] == 'T'))
        {
            /* This looks like a FAT32 boot sector, not an MBR */
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
            TRACE("Drive 0x%X detected as RAW FAT32 volume (no partition table)\n", DriveNumber);
            return;
        }

        /* Also check for FAT16/FAT12 */
        if (FatBootSector->BootSectorMagic == 0xAA55 &&
            FatBootSector->BytesPerSector >= 512 &&
            FatBootSector->SectorsPerCluster > 0 &&
            FatBootSector->NumberOfFats > 0 &&
            FatBootSector->RootDirEntries > 0 &&  /* FAT12/16 has root dir entries */
            (FatBootSector->FileSystemType[0] == 'F' &&
             FatBootSector->FileSystemType[1] == 'A' &&
             FatBootSector->FileSystemType[2] == 'T'))
        {
            /* This looks like a FAT16/FAT12 boot sector, not an MBR */
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
            TRACE("Drive 0x%X detected as RAW FAT volume (no partition table)\n", DriveNumber);
            return;
        }
    }

    /* Probe for Master Boot Record */
    if (DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_MBR;

        /* Check for GUID Partition Table */
        for (Index = 0; Index < 4; Index++)
        {
            ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

            if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED)
            {
                PartitionCount++;

                if (Index == 0 && ThisPartitionTableEntry->SystemIndicator == PARTITION_GPT)
                {
                    GPTProtect = TRUE;
                }
            }
        }

        if (PartitionCount == 1 && GPTProtect)
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_GPT;
        }
        TRACE("Drive 0x%X partition type %s\n", DriveNumber, DiskPartitionType[DriveNumber] == PARTITION_STYLE_MBR ? "MBR" : "GPT");
        return;
    }

    /* Probe for Xbox-BRFR partitioning */
    if (DiskGetBrfrPartitionEntry(DriveNumber, FATX_DATA_PARTITION, &PartitionTableEntry))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_BRFR;
        TRACE("Drive 0x%X partition type Xbox-BRFR\n", DriveNumber);
        return;
    }

    /* Failed to detect partitions, assume partitionless disk */
    DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
    TRACE("Drive 0x%X partition type RAW (no partition table)\n", DriveNumber);
}

BOOLEAN
DiskGetBootPartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG BootPartition)
{
    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetActivePartitionEntry(DriveNumber, PartitionTableEntry, BootPartition);
        }
        case PARTITION_STYLE_GPT:
        {
            FIXME("DiskGetBootPartitionEntry() unimplemented for GPT\n");
            return FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            /* For raw disks (e.g., ESP without partition table), use the whole disk */
            TRACE("DiskGetBootPartitionEntry() for RAW disk\n");
            RtlZeroMemory(PartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
            /* Get disk geometry to fill in the partition entry */
            GEOMETRY DiskGeometry;
            if (UefiDiskGetDriveGeometry(DriveNumber, &DiskGeometry))
            {
                PartitionTableEntry->SystemIndicator = PARTITION_FAT32;  /* Assume FAT32 for ESP */
                PartitionTableEntry->SectorCountBeforePartition = 0;  /* Start at beginning */
                PartitionTableEntry->PartitionSectorCount = DiskGeometry.Sectors;
                *BootPartition = 0;  /* Partition 0 means raw disk */
                return TRUE;
            }
            return FALSE;
        }
        case PARTITION_STYLE_BRFR:
        {
            if (DiskGetBrfrPartitionEntry(DriveNumber, FATX_DATA_PARTITION, PartitionTableEntry))
            {
                *BootPartition = FATX_DATA_PARTITION;
                return TRUE;
            }
            return FALSE;
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}

BOOLEAN
DiskGetPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetMbrPartitionEntry(DriveNumber, PartitionNumber, PartitionTableEntry);
        }
        case PARTITION_STYLE_GPT:
        {
            FIXME("DiskGetPartitionEntry() unimplemented for GPT\n");
            return FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            /* For raw disks, partition 0 means the whole disk, partition 1 can also mean the whole disk for compatibility */
            if (PartitionNumber == 0 || PartitionNumber == 1)
            {
                TRACE("DiskGetPartitionEntry() for RAW disk, partition %lu\n", PartitionNumber);
                RtlZeroMemory(PartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
                /* Get disk geometry to fill in the partition entry */
                GEOMETRY DiskGeometry;
                if (UefiDiskGetDriveGeometry(DriveNumber, &DiskGeometry))
                {
                    PartitionTableEntry->SystemIndicator = PARTITION_FAT32;  /* Assume FAT32 for ESP */
                    PartitionTableEntry->SectorCountBeforePartition = 0;  /* Start at beginning */
                    PartitionTableEntry->PartitionSectorCount = DiskGeometry.Sectors;
                    return TRUE;
                }
            }
            return FALSE;
        }
        case PARTITION_STYLE_BRFR:
        {
            return DiskGetBrfrPartitionEntry(DriveNumber, PartitionNumber, PartitionTableEntry);
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}
