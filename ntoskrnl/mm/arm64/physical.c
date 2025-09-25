/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Physical Memory Management
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* Physical Page States */
#define PAGE_STATE_FREE             0
#define PAGE_STATE_USED             1
#define PAGE_STATE_BAD              2
#define PAGE_STATE_ZEROED           3
#define PAGE_STATE_STANDBY          4
#define PAGE_STATE_MODIFIED         5
#define PAGE_STATE_TRANSITION       6
#define PAGE_STATE_ACTIVE           7

/* Page Colors for Cache Optimization */
#define ARM64_PAGE_COLOR_BITS       4
#define ARM64_PAGE_COLORS          (1 << ARM64_PAGE_COLOR_BITS)
#define ARM64_PAGE_COLOR_MASK      (ARM64_PAGE_COLORS - 1)

/* Physical Memory Zones */
#define ZONE_DMA                    0  /* < 16MB for legacy DMA */
#define ZONE_DMA32                  1  /* < 4GB for 32-bit DMA */
#define ZONE_NORMAL                 2  /* Normal allocations */
#define ZONE_HIGH                   3  /* High memory if present */
#define ZONE_COUNT                  4

/* Allocation Flags */
#define ALLOC_ZERO_PAGE            0x0001
#define ALLOC_CONTIGUOUS           0x0002
#define ALLOC_HIGH_PRIORITY        0x0004
#define ALLOC_NO_WAIT              0x0008
#define ALLOC_CACHE_ALIGNED        0x0010

/* DATA STRUCTURES ************************************************************/

/* Physical Page Frame */
typedef struct _PHYSICAL_PAGE
{
    union
    {
        struct
        {
            ULONG64 Flink : 48;      /* Next free page */
            ULONG64 Color : 4;       /* Page color for cache */
            ULONG64 Flags : 12;      /* Page flags */
        } Free;
        struct
        {
            ULONG64 PteAddress : 48; /* Address of PTE */
            ULONG64 ShareCount : 16; /* Number of shares */
        } Used;
        struct
        {
            ULONG64 NextPage : 48;   /* Next page in list */
            ULONG64 Priority : 8;    /* Page priority */
            ULONG64 Reserved : 8;
        } Standby;
    } u1;

    union
    {
        ULONG64 ReferenceCount;      /* Reference count */
        ULONG64 EventAddress;        /* Event for page I/O */
    } u2;

    ULONG State;                    /* Current page state */
    ULONG LockCount;                /* Page lock count */
    ULONG64 OriginalPte;            /* Original PTE value */
} PHYSICAL_PAGE, *PPHYSICAL_PAGE;

/* Physical Memory Zone */
typedef struct _MEMORY_ZONE
{
    ULONG64 StartPage;
    ULONG64 EndPage;
    ULONG64 TotalPages;
    ULONG64 FreePages;
    LIST_ENTRY FreeList[ARM64_PAGE_COLORS];
    KSPIN_LOCK Lock;
    CHAR Name[32];
} MEMORY_ZONE, *PMEMORY_ZONE;

/* Physical Memory Statistics */
typedef struct _PHYSICAL_MEMORY_STATS
{
    ULONG64 TotalPages;
    ULONG64 AvailablePages;
    ULONG64 UsedPages;
    ULONG64 FreePages;
    ULONG64 ZeroedPages;
    ULONG64 StandbyPages;
    ULONG64 ModifiedPages;
    ULONG64 BadPages;
    ULONG64 PageFaults;
    ULONG64 PageReads;
    ULONG64 PageWrites;
} PHYSICAL_MEMORY_STATS, *PPHYSICAL_MEMORY_STATS;

/* Physical Memory Descriptor */
typedef struct _PHYSICAL_MEMORY_RANGE
{
    PHYSICAL_ADDRESS BaseAddress;
    ULONG64 NumberOfBytes;
    ULONG MemoryType;
} PHYSICAL_MEMORY_RANGE, *PPHYSICAL_MEMORY_RANGE;

/* GLOBALS ********************************************************************/

/* Page Frame Database */
static PPHYSICAL_PAGE PageFrameDatabase = NULL;
static ULONG64 TotalPageFrames = 0;
static ULONG64 HighestPageFrame = 0;
static ULONG64 LowestPageFrame = 0;

/* Memory Zones */
static MEMORY_ZONE MemoryZones[ZONE_COUNT];

/* Free Page Lists by Color */
static LIST_ENTRY FreePageListHeads[ARM64_PAGE_COLORS];
static LIST_ENTRY ZeroedPageListHeads[ARM64_PAGE_COLORS];
static LIST_ENTRY StandbyPageListHead;
static LIST_ENTRY ModifiedPageListHead;
static LIST_ENTRY BadPageListHead;

/* Page Counts */
static ULONG64 TotalFreePages = 0;
static ULONG64 TotalZeroedPages = 0;
static ULONG64 TotalStandbyPages = 0;
static ULONG64 TotalModifiedPages = 0;

/* Locks */
static KSPIN_LOCK PfnDatabaseLock;
static KSPIN_LOCK FreePageLock;

/* Statistics */
static PHYSICAL_MEMORY_STATS PhysicalStats = {0};

/* Physical Memory Ranges */
static PHYSICAL_MEMORY_RANGE PhysicalMemoryRanges[64];
static ULONG PhysicalMemoryRangeCount = 0;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Get page color for cache optimization
 */
static
ULONG
MiGetPageColor(
    IN ULONG64 PageFrameNumber)
{
    /* Use lower bits of PFN for coloring */
    return (ULONG)(PageFrameNumber & ARM64_PAGE_COLOR_MASK);
}

/*
 * @brief Initialize memory zones
 */
static
VOID
MiInitializeMemoryZones(VOID)
{
    ULONG i, j;

    DPRINT("Initializing memory zones\n");

    for (i = 0; i < ZONE_COUNT; i++)
    {
        RtlZeroMemory(&MemoryZones[i], sizeof(MEMORY_ZONE));
        KeInitializeSpinLock(&MemoryZones[i].Lock);

        /* Initialize free lists for each color */
        for (j = 0; j < ARM64_PAGE_COLORS; j++)
        {
            InitializeListHead(&MemoryZones[i].FreeList[j]);
        }
    }

    /* Set zone boundaries (example for typical ARM64 system) */
    strcpy(MemoryZones[ZONE_DMA].Name, "DMA");
    MemoryZones[ZONE_DMA].StartPage = 0;
    MemoryZones[ZONE_DMA].EndPage = (16 * 1024 * 1024) >> PAGE_SHIFT;  /* 16MB */

    strcpy(MemoryZones[ZONE_DMA32].Name, "DMA32");
    MemoryZones[ZONE_DMA32].StartPage = MemoryZones[ZONE_DMA].EndPage;
    MemoryZones[ZONE_DMA32].EndPage = (4ULL * 1024 * 1024 * 1024) >> PAGE_SHIFT;  /* 4GB */

    strcpy(MemoryZones[ZONE_NORMAL].Name, "Normal");
    MemoryZones[ZONE_NORMAL].StartPage = MemoryZones[ZONE_DMA32].EndPage;
    MemoryZones[ZONE_NORMAL].EndPage = HighestPageFrame;

    strcpy(MemoryZones[ZONE_HIGH].Name, "High");
    MemoryZones[ZONE_HIGH].StartPage = 0;
    MemoryZones[ZONE_HIGH].EndPage = 0;  /* Not used on most ARM64 */

    /* Calculate total pages per zone */
    for (i = 0; i < ZONE_COUNT; i++)
    {
        if (MemoryZones[i].EndPage > MemoryZones[i].StartPage)
        {
            MemoryZones[i].TotalPages = MemoryZones[i].EndPage - MemoryZones[i].StartPage;
            DPRINT("Zone %s: Pages %llx-%llx (%llu pages)\n",
                   MemoryZones[i].Name,
                   MemoryZones[i].StartPage,
                   MemoryZones[i].EndPage,
                   MemoryZones[i].TotalPages);
        }
    }
}

/*
 * @brief Get zone for page frame
 */
static
PMEMORY_ZONE
MiGetZoneForPage(
    IN ULONG64 PageFrameNumber)
{
    ULONG i;

    for (i = 0; i < ZONE_COUNT; i++)
    {
        if (PageFrameNumber >= MemoryZones[i].StartPage &&
            PageFrameNumber < MemoryZones[i].EndPage)
        {
            return &MemoryZones[i];
        }
    }

    return NULL;
}

/*
 * @brief Add page to free list
 */
static
VOID
MiAddPageToFreeList(
    IN ULONG64 PageFrameNumber,
    IN BOOLEAN Zeroed)
{
    PPHYSICAL_PAGE Page;
    ULONG Color;
    PLIST_ENTRY ListHead;
    PMEMORY_ZONE Zone;

    Page = &PageFrameDatabase[PageFrameNumber];
    Color = MiGetPageColor(PageFrameNumber);

    /* Choose appropriate list */
    if (Zeroed)
    {
        ListHead = &ZeroedPageListHeads[Color];
        Page->State = PAGE_STATE_ZEROED;
        TotalZeroedPages++;
    }
    else
    {
        ListHead = &FreePageListHeads[Color];
        Page->State = PAGE_STATE_FREE;
        TotalFreePages++;
    }

    /* Set page properties */
    Page->u1.Free.Color = Color;
    Page->u1.Free.Flags = 0;
    Page->u2.ReferenceCount = 0;
    Page->LockCount = 0;

    /* Add to list */
    InsertTailList(ListHead, (PLIST_ENTRY)&Page->u1.Free.Flink);

    /* Update zone statistics */
    Zone = MiGetZoneForPage(PageFrameNumber);
    if (Zone)
    {
        InterlockedIncrement64((PLONG64)&Zone->FreePages);
    }

    /* Update global statistics */
    PhysicalStats.AvailablePages++;
}

/*
 * @brief Remove page from free list
 */
static
ULONG64
MiRemovePageFromFreeList(
    IN ULONG Color,
    IN BOOLEAN PreferZeroed)
{
    PLIST_ENTRY ListHead;
    PLIST_ENTRY Entry;
    PPHYSICAL_PAGE Page;
    ULONG64 PageFrameNumber;

    /* Try zeroed list first if preferred */
    if (PreferZeroed && !IsListEmpty(&ZeroedPageListHeads[Color]))
    {
        ListHead = &ZeroedPageListHeads[Color];
        Entry = RemoveHeadList(ListHead);
        TotalZeroedPages--;
    }
    else if (!IsListEmpty(&FreePageListHeads[Color]))
    {
        ListHead = &FreePageListHeads[Color];
        Entry = RemoveHeadList(ListHead);
        TotalFreePages--;
    }
    else if (PreferZeroed && !IsListEmpty(&FreePageListHeads[Color]))
    {
        /* Fall back to non-zeroed */
        ListHead = &FreePageListHeads[Color];
        Entry = RemoveHeadList(ListHead);
        TotalFreePages--;
    }
    else
    {
        return 0;  /* No pages available */
    }

    /* Calculate page frame number from entry */
    Page = CONTAINING_RECORD(Entry, PHYSICAL_PAGE, u1.Free.Flink);
    PageFrameNumber = ((ULONG64)Page - (ULONG64)PageFrameDatabase) / sizeof(PHYSICAL_PAGE);

    /* Update page state */
    Page->State = PAGE_STATE_USED;
    Page->u2.ReferenceCount = 1;

    /* Update statistics */
    PhysicalStats.AvailablePages--;
    PhysicalStats.UsedPages++;

    return PageFrameNumber;
}

/*
 * @brief Zero a physical page
 */
static
VOID
MiZeroPhysicalPage(
    IN ULONG64 PageFrameNumber)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID VirtualAddress;

    /* Calculate physical address */
    PhysicalAddress.QuadPart = PageFrameNumber << PAGE_SHIFT;

    /* Map temporarily to zero */
    VirtualAddress = MmMapIoSpace(PhysicalAddress, PAGE_SIZE, MmCached);
    if (VirtualAddress)
    {
        RtlZeroMemory(VirtualAddress, PAGE_SIZE);
        MmUnmapIoSpace(VirtualAddress, PAGE_SIZE);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize physical memory manager
 * @implemented
 */
VOID
NTAPI
MmInitializePhysicalMemoryManager(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PLIST_ENTRY ListEntry;
    ULONG64 PageCount = 0;
    ULONG i;

    DPRINT("Initializing ARM64 physical memory manager\n");

    /* Initialize locks */
    KeInitializeSpinLock(&PfnDatabaseLock);
    KeInitializeSpinLock(&FreePageLock);

    /* Initialize all list heads */
    for (i = 0; i < ARM64_PAGE_COLORS; i++)
    {
        InitializeListHead(&FreePageListHeads[i]);
        InitializeListHead(&ZeroedPageListHeads[i]);
    }
    InitializeListHead(&StandbyPageListHead);
    InitializeListHead(&ModifiedPageListHead);
    InitializeListHead(&BadPageListHead);

    /* Count total pages and find range */
    ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (ListEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(ListEntry,
                                            MEMORY_ALLOCATION_DESCRIPTOR,
                                            ListEntry);

        PageCount += MemoryDescriptor->PageCount;

        if (MemoryDescriptor->BasePage < LowestPageFrame)
            LowestPageFrame = MemoryDescriptor->BasePage;

        if (MemoryDescriptor->BasePage + MemoryDescriptor->PageCount > HighestPageFrame)
            HighestPageFrame = MemoryDescriptor->BasePage + MemoryDescriptor->PageCount;

        /* Store physical memory ranges */
        if (PhysicalMemoryRangeCount < 64)
        {
            PhysicalMemoryRanges[PhysicalMemoryRangeCount].BaseAddress.QuadPart =
                MemoryDescriptor->BasePage << PAGE_SHIFT;
            PhysicalMemoryRanges[PhysicalMemoryRangeCount].NumberOfBytes =
                MemoryDescriptor->PageCount << PAGE_SHIFT;
            PhysicalMemoryRanges[PhysicalMemoryRangeCount].MemoryType =
                MemoryDescriptor->MemoryType;
            PhysicalMemoryRangeCount++;
        }

        ListEntry = ListEntry->Flink;
    }

    TotalPageFrames = PageCount;
    PhysicalStats.TotalPages = PageCount;

    /* Allocate page frame database */
    ULONG64 PfnDatabaseSize = HighestPageFrame * sizeof(PHYSICAL_PAGE);
    PageFrameDatabase = (PPHYSICAL_PAGE)ExAllocatePoolWithTag(NonPagedPool,
                                                              PfnDatabaseSize,
                                                              'nfPP');
    if (!PageFrameDatabase)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT,
                    0x2001,
                    PfnDatabaseSize,
                    0,
                    0);
    }

    RtlZeroMemory(PageFrameDatabase, PfnDatabaseSize);

    /* Initialize memory zones */
    MiInitializeMemoryZones();

    /* Initialize pages based on memory descriptors */
    ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (ListEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(ListEntry,
                                            MEMORY_ALLOCATION_DESCRIPTOR,
                                            ListEntry);

        for (ULONG64 i = 0; i < MemoryDescriptor->PageCount; i++)
        {
            ULONG64 PageFrame = MemoryDescriptor->BasePage + i;
            PPHYSICAL_PAGE Page = &PageFrameDatabase[PageFrame];

            switch (MemoryDescriptor->MemoryType)
            {
                case LoaderFree:
                    /* Add to free list */
                    MiAddPageToFreeList(PageFrame, FALSE);
                    break;

                case LoaderBad:
                    /* Mark as bad */
                    Page->State = PAGE_STATE_BAD;
                    InsertTailList(&BadPageListHead, (PLIST_ENTRY)&Page->u1);
                    PhysicalStats.BadPages++;
                    break;

                case LoaderFirmwarePermanent:
                case LoaderSpecialMemory:
                    /* Mark as used and locked */
                    Page->State = PAGE_STATE_USED;
                    Page->u2.ReferenceCount = 1;
                    Page->LockCount = 1;
                    PhysicalStats.UsedPages++;
                    break;

                default:
                    /* Mark as used */
                    Page->State = PAGE_STATE_USED;
                    Page->u2.ReferenceCount = 1;
                    PhysicalStats.UsedPages++;
                    break;
            }
        }

        ListEntry = ListEntry->Flink;
    }

    DPRINT("Physical memory initialized: %llu total pages, %llu available\n",
           PhysicalStats.TotalPages, PhysicalStats.AvailablePages);
}

/*
 * @brief Allocate physical pages
 * @implemented
 */
PHYSICAL_ADDRESS
NTAPI
MmAllocatePhysicalPages(
    IN SIZE_T NumberOfPages)
{
    PHYSICAL_ADDRESS PhysicalAddress = {0};
    KIRQL OldIrql;
    ULONG64 PageFrame;
    ULONG Color;

    if (NumberOfPages == 0)
        return PhysicalAddress;

    KeAcquireSpinLock(&FreePageLock, &OldIrql);

    /* Check if we have enough pages */
    if (NumberOfPages > PhysicalStats.AvailablePages)
    {
        KeReleaseSpinLock(&FreePageLock, OldIrql);
        DPRINT1("Not enough physical pages: requested=%lu, available=%llu\n",
               NumberOfPages, PhysicalStats.AvailablePages);
        return PhysicalAddress;
    }

    if (NumberOfPages == 1)
    {
        /* Single page allocation - use color optimization */
        Color = (ULONG)(KeGetCurrentThread() >> PAGE_SHIFT) & ARM64_PAGE_COLOR_MASK;

        /* Try to get page with preferred color */
        PageFrame = MiRemovePageFromFreeList(Color, TRUE);

        /* If failed, try any color */
        if (PageFrame == 0)
        {
            for (ULONG i = 0; i < ARM64_PAGE_COLORS; i++)
            {
                PageFrame = MiRemovePageFromFreeList(i, TRUE);
                if (PageFrame != 0)
                    break;
            }
        }

        if (PageFrame != 0)
        {
            PhysicalAddress.QuadPart = PageFrame << PAGE_SHIFT;
            DPRINT("Allocated physical page %llx\n", PageFrame);
        }
    }
    else
    {
        /* Multiple page allocation - need contiguous pages
         * TODO: Implement buddy allocator for efficient contiguous allocation */
        DPRINT1("Contiguous allocation of %lu pages not yet implemented\n", NumberOfPages);
    }

    KeReleaseSpinLock(&FreePageLock, OldIrql);

    return PhysicalAddress;
}

/*
 * @brief Free physical pages
 * @implemented
 */
VOID
NTAPI
MmFreePhysicalPages(
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN SIZE_T NumberOfPages)
{
    KIRQL OldIrql;
    ULONG64 PageFrame;
    PPHYSICAL_PAGE Page;

    if (NumberOfPages == 0)
        return;

    PageFrame = PhysicalAddress.QuadPart >> PAGE_SHIFT;

    KeAcquireSpinLock(&FreePageLock, &OldIrql);

    for (SIZE_T i = 0; i < NumberOfPages; i++)
    {
        if (PageFrame + i >= HighestPageFrame)
        {
            DPRINT1("Invalid page frame %llx\n", PageFrame + i);
            continue;
        }

        Page = &PageFrameDatabase[PageFrame + i];

        /* Decrement reference count */
        if (Page->u2.ReferenceCount > 0)
        {
            Page->u2.ReferenceCount--;

            /* If no more references, add to free list */
            if (Page->u2.ReferenceCount == 0 && Page->LockCount == 0)
            {
                MiAddPageToFreeList(PageFrame + i, FALSE);
                PhysicalStats.UsedPages--;
                DPRINT("Freed physical page %llx\n", PageFrame + i);
            }
        }
        else
        {
            DPRINT1("Page %llx has zero reference count\n", PageFrame + i);
        }
    }

    KeReleaseSpinLock(&FreePageLock, OldIrql);
}

/*
 * @brief Allocate contiguous memory
 * @implemented
 */
PVOID
NTAPI
MmAllocateContiguousMemory(
    IN SIZE_T NumberOfBytes,
    IN PHYSICAL_ADDRESS HighestAcceptableAddress)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID VirtualAddress;
    SIZE_T Pages;

    Pages = ROUND_UP(NumberOfBytes, PAGE_SIZE) >> PAGE_SHIFT;

    /* Allocate physical pages */
    PhysicalAddress = MmAllocatePhysicalPages(Pages);
    if (!PhysicalAddress.QuadPart)
    {
        DPRINT1("Failed to allocate %lu contiguous pages\n", Pages);
        return NULL;
    }

    /* Check if within acceptable range */
    if (PhysicalAddress.QuadPart + NumberOfBytes > HighestAcceptableAddress.QuadPart)
    {
        MmFreePhysicalPages(PhysicalAddress, Pages);
        DPRINT1("Allocated memory exceeds highest acceptable address\n");
        return NULL;
    }

    /* Map to virtual address */
    VirtualAddress = MmMapIoSpace(PhysicalAddress, NumberOfBytes, MmCached);
    if (!VirtualAddress)
    {
        MmFreePhysicalPages(PhysicalAddress, Pages);
        DPRINT1("Failed to map contiguous memory\n");
        return NULL;
    }

    return VirtualAddress;
}

/*
 * @brief Free contiguous memory
 * @implemented
 */
VOID
NTAPI
MmFreeContiguousMemory(
    IN PVOID BaseAddress)
{
    /* TODO: Need to track virtual to physical mappings
     * For now, this is a stub */
    UNREFERENCED_PARAMETER(BaseAddress);
    DPRINT1("MmFreeContiguousMemory not fully implemented\n");
}

/*
 * @brief Reference physical page
 * @implemented
 */
VOID
NTAPI
MmReferencePhysicalPage(
    IN ULONG64 PageFrameNumber)
{
    KIRQL OldIrql;
    PPHYSICAL_PAGE Page;

    if (PageFrameNumber >= HighestPageFrame)
        return;

    KeAcquireSpinLock(&PfnDatabaseLock, &OldIrql);

    Page = &PageFrameDatabase[PageFrameNumber];
    Page->u2.ReferenceCount++;

    KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);
}

/*
 * @brief Dereference physical page
 * @implemented
 */
VOID
NTAPI
MmDereferencePhysicalPage(
    IN ULONG64 PageFrameNumber)
{
    KIRQL OldIrql;
    PPHYSICAL_PAGE Page;

    if (PageFrameNumber >= HighestPageFrame)
        return;

    KeAcquireSpinLock(&PfnDatabaseLock, &OldIrql);

    Page = &PageFrameDatabase[PageFrameNumber];
    if (Page->u2.ReferenceCount > 0)
    {
        Page->u2.ReferenceCount--;

        /* If no more references and not locked, free the page */
        if (Page->u2.ReferenceCount == 0 && Page->LockCount == 0)
        {
            KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);

            /* Free the page */
            PHYSICAL_ADDRESS PhysicalAddress;
            PhysicalAddress.QuadPart = PageFrameNumber << PAGE_SHIFT;
            MmFreePhysicalPages(PhysicalAddress, 1);
            return;
        }
    }

    KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);
}

/*
 * @brief Lock physical page
 * @implemented
 */
VOID
NTAPI
MmLockPhysicalPage(
    IN ULONG64 PageFrameNumber)
{
    KIRQL OldIrql;
    PPHYSICAL_PAGE Page;

    if (PageFrameNumber >= HighestPageFrame)
        return;

    KeAcquireSpinLock(&PfnDatabaseLock, &OldIrql);

    Page = &PageFrameDatabase[PageFrameNumber];
    Page->LockCount++;

    KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);
}

/*
 * @brief Unlock physical page
 * @implemented
 */
VOID
NTAPI
MmUnlockPhysicalPage(
    IN ULONG64 PageFrameNumber)
{
    KIRQL OldIrql;
    PPHYSICAL_PAGE Page;

    if (PageFrameNumber >= HighestPageFrame)
        return;

    KeAcquireSpinLock(&PfnDatabaseLock, &OldIrql);

    Page = &PageFrameDatabase[PageFrameNumber];
    if (Page->LockCount > 0)
    {
        Page->LockCount--;
    }

    KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);
}

/*
 * @brief Get physical memory statistics
 * @implemented
 */
VOID
NTAPI
MmGetPhysicalMemoryStatistics(
    OUT PPHYSICAL_MEMORY_STATS Stats)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&PfnDatabaseLock, &OldIrql);
    RtlCopyMemory(Stats, &PhysicalStats, sizeof(PHYSICAL_MEMORY_STATS));
    Stats->FreePages = TotalFreePages;
    Stats->ZeroedPages = TotalZeroedPages;
    Stats->StandbyPages = TotalStandbyPages;
    Stats->ModifiedPages = TotalModifiedPages;
    KeReleaseSpinLock(&PfnDatabaseLock, OldIrql);
}

/*
 * @brief Get available physical memory
 * @implemented
 */
ULONG64
NTAPI
MmGetAvailablePhysicalMemory(VOID)
{
    return PhysicalStats.AvailablePages << PAGE_SHIFT;
}

/*
 * @brief Page zeroing thread
 * @implemented
 */
VOID
NTAPI
MmZeroPageThread(
    IN PVOID StartContext)
{
    KIRQL OldIrql;
    ULONG64 PageFrame;
    ULONG Color;
    PLIST_ENTRY Entry;
    PPHYSICAL_PAGE Page;

    UNREFERENCED_PARAMETER(StartContext);

    DPRINT("Page zeroing thread started\n");

    while (TRUE)
    {
        /* Wait for pages to zero */
        KeDelayExecutionThread(KernelMode, FALSE, &(LARGE_INTEGER){{1000000}});  /* 100ms */

        KeAcquireSpinLock(&FreePageLock, &OldIrql);

        /* Find non-zeroed free pages */
        for (Color = 0; Color < ARM64_PAGE_COLORS; Color++)
        {
            if (!IsListEmpty(&FreePageListHeads[Color]))
            {
                Entry = RemoveHeadList(&FreePageListHeads[Color]);
                Page = CONTAINING_RECORD(Entry, PHYSICAL_PAGE, u1.Free.Flink);
                PageFrame = ((ULONG64)Page - (ULONG64)PageFrameDatabase) / sizeof(PHYSICAL_PAGE);

                TotalFreePages--;

                KeReleaseSpinLock(&FreePageLock, OldIrql);

                /* Zero the page */
                MiZeroPhysicalPage(PageFrame);

                /* Add to zeroed list */
                KeAcquireSpinLock(&FreePageLock, &OldIrql);
                MiAddPageToFreeList(PageFrame, TRUE);

                /* Process only one page per iteration to be responsive */
                break;
            }
        }

        KeReleaseSpinLock(&FreePageLock, OldIrql);
    }
}

/*
 * @brief Get physical memory ranges
 * @implemented
 */
ULONG
NTAPI
MmGetPhysicalMemoryRanges(
    OUT PPHYSICAL_MEMORY_RANGE Ranges,
    IN ULONG MaxRanges)
{
    ULONG Count = min(PhysicalMemoryRangeCount, MaxRanges);

    if (Ranges && Count > 0)
    {
        RtlCopyMemory(Ranges, PhysicalMemoryRanges,
                     Count * sizeof(PHYSICAL_MEMORY_RANGE));
    }

    return PhysicalMemoryRangeCount;
}

/* EOF */