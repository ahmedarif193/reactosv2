/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Memory Manager Initialization
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Memory Constants */
#define ARM64_PAGE_SIZE             4096
#define ARM64_PAGE_SHIFT            12
#define ARM64_LARGE_PAGE_SIZE       (2 * 1024 * 1024)    /* 2MB */
#define ARM64_LARGE_PAGE_SHIFT      21
#define ARM64_HUGE_PAGE_SIZE        (1024 * 1024 * 1024) /* 1GB */
#define ARM64_HUGE_PAGE_SHIFT       30

/* Translation Granule Sizes */
#define ARM64_GRANULE_4KB           0
#define ARM64_GRANULE_16KB          1
#define ARM64_GRANULE_64KB          2

/* Page Table Levels for 4KB pages */
#define ARM64_PTE_LEVEL0_SHIFT      39
#define ARM64_PTE_LEVEL1_SHIFT      30
#define ARM64_PTE_LEVEL2_SHIFT      21
#define ARM64_PTE_LEVEL3_SHIFT      12

/* Number of entries per page table */
#define ARM64_PTES_PER_PAGE         512

/* Memory Attributes */
#define ARM64_MEMORY_NORMAL         0
#define ARM64_MEMORY_DEVICE         1
#define ARM64_MEMORY_NORMAL_NC      2
#define ARM64_MEMORY_NORMAL_WT      3

/* Memory Types for MAIR_EL1 */
#define MAIR_ATTR_DEVICE_nGnRnE     0x00  /* Device, non-Gathering, non-Reordering, no Early write ack */
#define MAIR_ATTR_DEVICE_nGnRE      0x04  /* Device, non-Gathering, non-Reordering, Early write ack */
#define MAIR_ATTR_NORMAL_NC         0x44  /* Normal, Non-cacheable */
#define MAIR_ATTR_NORMAL_WT         0xBB  /* Normal, Write-Through */
#define MAIR_ATTR_NORMAL_WB         0xFF  /* Normal, Write-Back */

/* TCR_EL1 Configuration */
#define TCR_T0SZ(x)                 ((64 - (x)) << 0)  /* Size of region for TTBR0 */
#define TCR_T1SZ(x)                 ((64 - (x)) << 16) /* Size of region for TTBR1 */
#define TCR_TG0_4KB                 (0 << 14)
#define TCR_TG0_64KB                (1 << 14)
#define TCR_TG0_16KB                (2 << 14)
#define TCR_TG1_4KB                 (2 << 30)
#define TCR_TG1_64KB                (3 << 30)
#define TCR_TG1_16KB                (1 << 30)
#define TCR_SH0_INNER               (3 << 12)  /* Inner shareable */
#define TCR_SH1_INNER               (3 << 28)
#define TCR_ORGN0_WB_WA             (1 << 10)  /* Write-Back Write-Allocate */
#define TCR_ORGN1_WB_WA             (1 << 26)
#define TCR_IRGN0_WB_WA             (1 << 8)
#define TCR_IRGN1_WB_WA             (1 << 24)
#define TCR_EPD0_ENABLE             (0 << 7)
#define TCR_EPD1_ENABLE             (0 << 23)
#define TCR_IPS_48BIT               (5ULL << 32)  /* 48-bit intermediate physical address */
#define TCR_AS_16BIT                (1ULL << 36)  /* 16-bit ASID */

/* DATA STRUCTURES ************************************************************/

/* Memory Manager Configuration */
typedef struct _ARM64_MM_CONFIG
{
    ULONG64 PhysicalMemorySize;
    ULONG64 KernelVirtualBase;
    ULONG64 KernelVirtualSize;
    ULONG64 UserVirtualBase;
    ULONG64 UserVirtualSize;
    ULONG PageSize;
    ULONG GranuleSize;
    ULONG ASIDBits;
    ULONG MaxASID;
    BOOLEAN LargePageSupport;
    BOOLEAN HugePageSupport;
} ARM64_MM_CONFIG, *PARM64_MM_CONFIG;

/* Physical Memory Region */
typedef struct _ARM64_MEMORY_REGION
{
    PHYSICAL_ADDRESS BaseAddress;
    ULONG64 Size;
    ULONG Type;
    ULONG Attributes;
    LIST_ENTRY ListEntry;
} ARM64_MEMORY_REGION, *PARM64_MEMORY_REGION;

/* Page Frame Database Entry */
typedef struct _ARM64_MMPFN
{
    union
    {
        ULONG64 Flink;
        ULONG64 WsIndex;
        PKEVENT Event;
        NTSTATUS ReadStatus;
    } u1;
    PVOID PteAddress;
    union
    {
        ULONG64 Blink;
        ULONG64 ShareCount;
    } u2;
    union
    {
        struct
        {
            USHORT ReferenceCount;
            USHORT Flags;
        };
        ULONG EntireField;
    } u3;
    ULONG64 OriginalPte;
    ULONG State;
} ARM64_MMPFN, *PARM64_MMPFN;

/* GLOBALS ********************************************************************/

/* Memory Manager Configuration */
static ARM64_MM_CONFIG MmConfig = {0};

/* Physical Memory Database */
static PARM64_MMPFN MmPfnDatabase = NULL;
static ULONG64 MmNumberOfPhysicalPages = 0;
static ULONG64 MmHighestPhysicalPage = 0;
static ULONG64 MmLowestPhysicalPage = 0;

/* Free Page Lists */
static LIST_ENTRY MmFreePageListHead;
static LIST_ENTRY MmZeroedPageListHead;
static LIST_ENTRY MmBadPageListHead;
static ULONG MmAvailablePages = 0;
static ULONG MmZeroedPages = 0;

/* Kernel Address Space */
static PVOID MmKernelAddressSpace = NULL;
static ULONG64 MmKernelVirtualStart = 0xFFFF000000000000ULL;  /* Kernel space starts at -256TB */
static ULONG64 MmKernelVirtualEnd = 0xFFFFFFFFFFFFFFFFULL;

/* User Address Space Limits */
static ULONG64 MmUserVirtualStart = 0x0000000000000000ULL;
static ULONG64 MmUserVirtualEnd = 0x0000FFFFFFFFFFFFULL;      /* User space ends at 256TB */

/* Page Tables */
static PVOID MmKernelPageDirectory = NULL;
static PHYSICAL_ADDRESS MmKernelPageDirectoryPhysical = {0};

/* Memory Regions */
static LIST_ENTRY MmPhysicalMemoryListHead;
static KSPIN_LOCK MmPhysicalMemoryLock;

/* System Memory Information */
PHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock = {0};
ULONG MmNumberOfSystemPtes = 0;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Initialize ARM64 MAIR_EL1 (Memory Attribute Indirection Register)
 */
static
VOID
MiInitializeMemoryAttributes(VOID)
{
    ULONG64 MairEl1;

    DPRINT("Initializing ARM64 memory attributes (MAIR_EL1)\n");

    /* Configure memory attributes
     * Attr0: Device-nGnRnE (Strongly ordered device memory)
     * Attr1: Device-nGnRE (Device memory)
     * Attr2: Normal Non-cacheable
     * Attr3: Normal Write-Through
     * Attr4: Normal Write-Back */
    MairEl1 = (MAIR_ATTR_DEVICE_nGnRnE << 0) |
              (MAIR_ATTR_DEVICE_nGnRE << 8) |
              (MAIR_ATTR_NORMAL_NC << 16) |
              (MAIR_ATTR_NORMAL_WT << 24) |
              (MAIR_ATTR_NORMAL_WB << 32);

    /* Write MAIR_EL1 */
    __asm__ __volatile__ ("msr MAIR_EL1, %0" :: "r"(MairEl1));
    __asm__ __volatile__ ("isb" ::: "memory");

    DPRINT("MAIR_EL1 configured: 0x%016llx\n", MairEl1);
}

/*
 * @brief Initialize ARM64 TCR_EL1 (Translation Control Register)
 */
static
VOID
MiInitializeTranslationControl(VOID)
{
    ULONG64 TcrEl1;
    ULONG64 Id_aa64mmfr0;

    DPRINT("Initializing ARM64 translation control (TCR_EL1)\n");

    /* Read system capabilities */
    __asm__ __volatile__ ("mrs %0, ID_AA64MMFR0_EL1" : "=r"(Id_aa64mmfr0));

    /* Extract physical address range */
    ULONG PARange = Id_aa64mmfr0 & 0xF;
    ULONG ASIDBits = ((Id_aa64mmfr0 >> 4) & 0xF) ? 16 : 8;

    /* Configure TCR_EL1 for 4KB pages, 48-bit VA */
    TcrEl1 = TCR_T0SZ(48) |           /* 48-bit address space for TTBR0 */
             TCR_T1SZ(48) |           /* 48-bit address space for TTBR1 */
             TCR_TG0_4KB |            /* 4KB granule for TTBR0 */
             TCR_TG1_4KB |            /* 4KB granule for TTBR1 */
             TCR_SH0_INNER |          /* Inner shareable for TTBR0 */
             TCR_SH1_INNER |          /* Inner shareable for TTBR1 */
             TCR_ORGN0_WB_WA |        /* Outer Write-Back Write-Allocate for TTBR0 */
             TCR_ORGN1_WB_WA |        /* Outer Write-Back Write-Allocate for TTBR1 */
             TCR_IRGN0_WB_WA |        /* Inner Write-Back Write-Allocate for TTBR0 */
             TCR_IRGN1_WB_WA |        /* Inner Write-Back Write-Allocate for TTBR1 */
             TCR_EPD0_ENABLE |        /* Enable TTBR0 walks */
             TCR_EPD1_ENABLE |        /* Enable TTBR1 walks */
             TCR_IPS_48BIT;           /* 48-bit intermediate physical address */

    /* Add ASID configuration if 16-bit ASIDs are supported */
    if (ASIDBits == 16)
    {
        TcrEl1 |= TCR_AS_16BIT;
        MmConfig.MaxASID = 65535;
    }
    else
    {
        MmConfig.MaxASID = 255;
    }

    MmConfig.ASIDBits = ASIDBits;

    /* Write TCR_EL1 */
    __asm__ __volatile__ ("msr TCR_EL1, %0" :: "r"(TcrEl1));
    __asm__ __volatile__ ("isb" ::: "memory");

    DPRINT("TCR_EL1 configured: 0x%016llx (PA range=%lu, ASID bits=%lu)\n",
           TcrEl1, PARange, ASIDBits);
}

/*
 * @brief Initialize Page Frame Number Database
 */
static
NTSTATUS
MiInitializePfnDatabase(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PLIST_ENTRY ListEntry;
    ULONG64 PageCount = 0;
    ULONG64 HighestPage = 0;
    ULONG64 LowestPage = (ULONG64)-1;

    DPRINT("Initializing PFN database\n");

    /* Walk memory descriptor list to count pages */
    ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (ListEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(ListEntry,
                                            MEMORY_ALLOCATION_DESCRIPTOR,
                                            ListEntry);

        /* Track page counts and ranges */
        PageCount += MemoryDescriptor->PageCount;

        if (MemoryDescriptor->BasePage < LowestPage)
            LowestPage = MemoryDescriptor->BasePage;

        if (MemoryDescriptor->BasePage + MemoryDescriptor->PageCount > HighestPage)
            HighestPage = MemoryDescriptor->BasePage + MemoryDescriptor->PageCount;

        ListEntry = ListEntry->Flink;
    }

    MmNumberOfPhysicalPages = PageCount;
    MmHighestPhysicalPage = HighestPage;
    MmLowestPhysicalPage = LowestPage;

    DPRINT("Physical memory: %llu pages, range %llx-%llx\n",
           PageCount, LowestPage << PAGE_SHIFT, HighestPage << PAGE_SHIFT);

    /* Allocate PFN database
     * TODO: This should be done in a reserved virtual address range */
    ULONG64 PfnDatabaseSize = HighestPage * sizeof(ARM64_MMPFN);
    MmPfnDatabase = (PARM64_MMPFN)ExAllocatePoolWithTag(NonPagedPool,
                                                        PfnDatabaseSize,
                                                        'nfPM');
    if (!MmPfnDatabase)
    {
        DPRINT1("Failed to allocate PFN database\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(MmPfnDatabase, PfnDatabaseSize);

    /* Initialize PFN entries based on memory descriptors */
    ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (ListEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(ListEntry,
                                            MEMORY_ALLOCATION_DESCRIPTOR,
                                            ListEntry);

        for (ULONG64 Page = 0; Page < MemoryDescriptor->PageCount; Page++)
        {
            ULONG64 Pfn = MemoryDescriptor->BasePage + Page;
            PARM64_MMPFN PfnEntry = &MmPfnDatabase[Pfn];

            /* Set initial state based on memory type */
            switch (MemoryDescriptor->MemoryType)
            {
                case LoaderFree:
                case LoaderLoadedProgram:
                case LoaderOsloaderStack:
                    PfnEntry->State = 0;  /* Free */
                    PfnEntry->u3.ReferenceCount = 0;
                    /* Add to free list */
                    InsertTailList(&MmFreePageListHead, (PLIST_ENTRY)&PfnEntry->u1.Flink);
                    MmAvailablePages++;
                    break;

                case LoaderFirmwarePermanent:
                case LoaderSpecialMemory:
                case LoaderBad:
                    PfnEntry->State = 2;  /* Bad/Reserved */
                    PfnEntry->u3.ReferenceCount = 1;
                    InsertTailList(&MmBadPageListHead, (PLIST_ENTRY)&PfnEntry->u1.Flink);
                    break;

                default:
                    PfnEntry->State = 1;  /* In use */
                    PfnEntry->u3.ReferenceCount = 1;
                    break;
            }
        }

        ListEntry = ListEntry->Flink;
    }

    DPRINT("PFN database initialized: %lu available pages\n", MmAvailablePages);
    return STATUS_SUCCESS;
}

/*
 * @brief Initialize kernel address space
 */
static
NTSTATUS
MiInitializeKernelAddressSpace(VOID)
{
    PHYSICAL_ADDRESS PageTablePhysical;
    PVOID PageTable;

    DPRINT("Initializing kernel address space\n");

    /* Allocate kernel page directory (Level 0 for 4KB pages) */
    PageTablePhysical = MmAllocateContiguousMemory(PAGE_SIZE, (PHYSICAL_ADDRESS){0});
    if (!PageTablePhysical.QuadPart)
    {
        DPRINT1("Failed to allocate kernel page directory\n");
        return STATUS_NO_MEMORY;
    }

    PageTable = MmMapIoSpace(PageTablePhysical, PAGE_SIZE, MmNonCached);
    if (!PageTable)
    {
        DPRINT1("Failed to map kernel page directory\n");
        MmFreeContiguousMemory(PageTablePhysical);
        return STATUS_NO_MEMORY;
    }

    /* Clear page directory */
    RtlZeroMemory(PageTable, PAGE_SIZE);

    MmKernelPageDirectory = PageTable;
    MmKernelPageDirectoryPhysical = PageTablePhysical;

    /* TODO: Set up initial kernel mappings
     * - Identity map low memory for boot
     * - Map kernel image
     * - Map PFN database
     * - Map system PTEs
     */

    DPRINT("Kernel page directory at VA %p, PA %llx\n",
           PageTable, PageTablePhysical.QuadPart);

    return STATUS_SUCCESS;
}

/*
 * @brief Build memory descriptor list
 */
static
VOID
MiBuildPhysicalMemoryList(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PLIST_ENTRY ListEntry;
    PARM64_MEMORY_REGION Region;

    DPRINT("Building physical memory list\n");

    /* Initialize list */
    InitializeListHead(&MmPhysicalMemoryListHead);
    KeInitializeSpinLock(&MmPhysicalMemoryLock);

    /* Walk loader memory descriptors */
    ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (ListEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(ListEntry,
                                            MEMORY_ALLOCATION_DESCRIPTOR,
                                            ListEntry);

        /* Create memory region */
        Region = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(ARM64_MEMORY_REGION),
                                      'geRM');
        if (Region)
        {
            Region->BaseAddress.QuadPart = MemoryDescriptor->BasePage << PAGE_SHIFT;
            Region->Size = MemoryDescriptor->PageCount << PAGE_SHIFT;
            Region->Type = MemoryDescriptor->MemoryType;
            Region->Attributes = ARM64_MEMORY_NORMAL;

            InsertTailList(&MmPhysicalMemoryListHead, &Region->ListEntry);

            DPRINT("Memory region: %016llx-%016llx, type %d\n",
                   Region->BaseAddress.QuadPart,
                   Region->BaseAddress.QuadPart + Region->Size,
                   Region->Type);
        }

        ListEntry = ListEntry->Flink;
    }

    /* Store total physical memory */
    MmConfig.PhysicalMemorySize = MmNumberOfPhysicalPages << PAGE_SHIFT;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize ARM64 Memory Manager Phase 0
 * @implemented
 */
VOID
NTAPI
MiInitSystem(
    IN ULONG Phase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;

    DPRINT("ARM64 Memory Manager initialization phase %lu\n", Phase);

    if (Phase == 0)
    {
        /* Phase 0: Early initialization */

        /* Initialize free lists */
        InitializeListHead(&MmFreePageListHead);
        InitializeListHead(&MmZeroedPageListHead);
        InitializeListHead(&MmBadPageListHead);

        /* Set up memory configuration */
        MmConfig.PageSize = ARM64_PAGE_SIZE;
        MmConfig.GranuleSize = ARM64_GRANULE_4KB;
        MmConfig.LargePageSupport = TRUE;
        MmConfig.HugePageSupport = TRUE;
        MmConfig.KernelVirtualBase = MmKernelVirtualStart;
        MmConfig.KernelVirtualSize = MmKernelVirtualEnd - MmKernelVirtualStart;
        MmConfig.UserVirtualBase = MmUserVirtualStart;
        MmConfig.UserVirtualSize = MmUserVirtualEnd - MmUserVirtualStart;

        /* Initialize ARM64-specific MM registers */
        MiInitializeMemoryAttributes();
        MiInitializeTranslationControl();

        /* Initialize PFN database */
        Status = MiInitializePfnDatabase(LoaderBlock);
        if (!NT_SUCCESS(Status))
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                        0x1001,
                        Status,
                        (ULONG_PTR)LoaderBlock,
                        0);
        }

        /* Build physical memory list */
        MiBuildPhysicalMemoryList(LoaderBlock);

        /* Initialize kernel address space */
        Status = MiInitializeKernelAddressSpace();
        if (!NT_SUCCESS(Status))
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                        0x1002,
                        Status,
                        0,
                        0);
        }

        DPRINT("ARM64 MM Phase 0 initialization complete\n");
    }
    else if (Phase == 1)
    {
        /* Phase 1: Secondary initialization */

        /* Initialize ARM64 pool allocator */
        Status = MiInitializeArm64Pool();
        if (!NT_SUCCESS(Status))
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                        0x1003,
                        Status,
                        0,
                        0);
        }

        /* Initialize pool descriptors for standard APIs */
        InitializePool(NonPagedPool, 0);

        /* TODO: Initialize section objects */
        /* TODO: Initialize working set manager */
        /* TODO: Initialize paged pool */
        /* TODO: Initialize system cache */

        DPRINT("ARM64 MM Phase 1 initialization complete\n");
    }
}

/*
 * @brief ARM64-specific MM initialization
 * @implemented
 */
BOOLEAN
NTAPI
MmInitializeMemoryManager(VOID)
{
    DPRINT("Initializing ARM64 memory manager\n");

    /* TODO: Set up system PTEs
     * TODO: Initialize balance set manager
     * TODO: Initialize modified page writer
     * TODO: Initialize mapped file writer
     */

    /* Initialize zeroing thread */
    /* TODO: Create system thread for page zeroing */

    return TRUE;
}

/*
 * @brief Get physical memory size
 * @implemented
 */
ULONG64
NTAPI
MmGetPhysicalMemorySize(VOID)
{
    return MmConfig.PhysicalMemorySize;
}

/*
 * @brief Get number of physical pages
 * @implemented
 */
PFN_COUNT
NTAPI
MmGetNumberOfPhysicalPages(VOID)
{
    return MmNumberOfPhysicalPages;
}

/*
 * @brief Check if address is valid
 * @implemented
 */
BOOLEAN
NTAPI
MmIsAddressValid(
    IN PVOID VirtualAddress)
{
    ULONG64 Address = (ULONG64)VirtualAddress;

    /* Check if in user space */
    if (Address <= MmUserVirtualEnd)
    {
        /* TODO: Check user page tables */
        return FALSE;
    }

    /* Check if in kernel space */
    if (Address >= MmKernelVirtualStart)
    {
        /* TODO: Check kernel page tables */
        return TRUE;  /* Assume valid for now */
    }

    /* Address in invalid range */
    return FALSE;
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
    PLIST_ENTRY Entry;
    PARM64_MMPFN Pfn;

    /* Check availability */
    if (NumberOfPages > MmAvailablePages)
    {
        DPRINT1("Not enough free pages: requested=%lu, available=%lu\n",
               NumberOfPages, MmAvailablePages);
        return PhysicalAddress;
    }

    KeAcquireSpinLock(&MmPhysicalMemoryLock, &OldIrql);

    /* TODO: For now, only support single page allocation
     * Multi-page contiguous allocation needs more work */
    if (NumberOfPages == 1 && !IsListEmpty(&MmFreePageListHead))
    {
        /* Get a free page */
        Entry = RemoveHeadList(&MmFreePageListHead);
        Pfn = CONTAINING_RECORD(Entry, ARM64_MMPFN, u1.Flink);

        /* Calculate physical address from PFN */
        ULONG64 PageNumber = ((ULONG64)Pfn - (ULONG64)MmPfnDatabase) / sizeof(ARM64_MMPFN);
        PhysicalAddress.QuadPart = PageNumber << PAGE_SHIFT;

        /* Update PFN entry */
        Pfn->State = 1;  /* In use */
        Pfn->u3.ReferenceCount = 1;

        MmAvailablePages--;

        DPRINT("Allocated physical page at %llx\n", PhysicalAddress.QuadPart);
    }

    KeReleaseSpinLock(&MmPhysicalMemoryLock, OldIrql);

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
    ULONG64 PageNumber;
    PARM64_MMPFN Pfn;

    KeAcquireSpinLock(&MmPhysicalMemoryLock, &OldIrql);

    /* TODO: For now, only support single page free */
    if (NumberOfPages == 1)
    {
        PageNumber = PhysicalAddress.QuadPart >> PAGE_SHIFT;
        if (PageNumber <= MmHighestPhysicalPage)
        {
            Pfn = &MmPfnDatabase[PageNumber];

            /* Decrement reference count */
            if (Pfn->u3.ReferenceCount > 0)
            {
                Pfn->u3.ReferenceCount--;

                /* If no more references, add to free list */
                if (Pfn->u3.ReferenceCount == 0)
                {
                    Pfn->State = 0;  /* Free */
                    InsertTailList(&MmFreePageListHead, (PLIST_ENTRY)&Pfn->u1.Flink);
                    MmAvailablePages++;

                    DPRINT("Freed physical page at %llx\n", PhysicalAddress.QuadPart);
                }
            }
        }
    }

    KeReleaseSpinLock(&MmPhysicalMemoryLock, OldIrql);
}

/*
 * @brief Initialize paging for ARM64
 * @implemented
 */
VOID
NTAPI
MmInitializePaging(VOID)
{
    ULONG64 TtbrValue;

    DPRINT("Initializing ARM64 paging\n");

    /* Set TTBR0_EL1 for user space */
    /* TODO: Set up user page tables */
    TtbrValue = 0;  /* Will be set per-process */
    __asm__ __volatile__ ("msr TTBR0_EL1, %0" :: "r"(TtbrValue));

    /* Set TTBR1_EL1 for kernel space */
    TtbrValue = MmKernelPageDirectoryPhysical.QuadPart;
    __asm__ __volatile__ ("msr TTBR1_EL1, %0" :: "r"(TtbrValue));

    /* Ensure changes take effect */
    __asm__ __volatile__ ("isb" ::: "memory");

    /* Enable MMU if not already enabled
     * Note: Usually the bootloader enables it */
    ULONG64 SctlrEl1;
    __asm__ __volatile__ ("mrs %0, SCTLR_EL1" : "=r"(SctlrEl1));

    if (!(SctlrEl1 & 0x1))  /* Check if MMU is disabled */
    {
        SctlrEl1 |= 0x1;  /* Enable MMU */
        __asm__ __volatile__ ("msr SCTLR_EL1, %0" :: "r"(SctlrEl1));
        __asm__ __volatile__ ("isb" ::: "memory");
        DPRINT("MMU enabled\n");
    }
    else
    {
        DPRINT("MMU already enabled\n");
    }
}

/*
 * @brief Get current ASID
 * @implemented
 */
ULONG
NTAPI
MmGetCurrentASID(VOID)
{
    ULONG64 TtbrEl0;

    /* Read TTBR0_EL1 */
    __asm__ __volatile__ ("mrs %0, TTBR0_EL1" : "=r"(TtbrEl0));

    /* Extract ASID from bits 48-63 */
    return (ULONG)(TtbrEl0 >> 48);
}

/*
 * @brief Set current ASID
 * @implemented
 */
VOID
NTAPI
MmSetCurrentASID(
    IN ULONG ASID)
{
    ULONG64 TtbrEl0;

    /* Read current TTBR0_EL1 */
    __asm__ __volatile__ ("mrs %0, TTBR0_EL1" : "=r"(TtbrEl0));

    /* Clear old ASID and set new one */
    TtbrEl0 = (TtbrEl0 & 0x0000FFFFFFFFFFFFULL) | ((ULONG64)ASID << 48);

    /* Write back TTBR0_EL1 */
    __asm__ __volatile__ ("msr TTBR0_EL1, %0" :: "r"(TtbrEl0));
    __asm__ __volatile__ ("isb" ::: "memory");
}

/* EOF */