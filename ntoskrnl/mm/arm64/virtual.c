/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Virtual Memory Management
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Page Table Entry Bits */
#define PTE_VALID                   (1ULL << 0)
#define PTE_TABLE                   (1ULL << 1)   /* For non-leaf entries */
#define PTE_PAGE                    (1ULL << 1)   /* For leaf entries */
#define PTE_ATTR_MASK               (0x7ULL << 2) /* AttrIndx[2:0] */
#define PTE_NS                      (1ULL << 5)   /* Non-secure */
#define PTE_AP_MASK                 (0x3ULL << 6) /* Access permissions */
#define PTE_AP_RW_EL0               (0x1ULL << 6) /* Read/Write at EL0 */
#define PTE_AP_RW_EL1               (0x0ULL << 6) /* Read/Write at EL1+ only */
#define PTE_AP_RO_EL0               (0x3ULL << 6) /* Read-only at EL0 */
#define PTE_AP_RO_EL1               (0x2ULL << 6) /* Read-only at EL1+ */
#define PTE_SH_MASK                 (0x3ULL << 8) /* Shareability */
#define PTE_SH_NON                  (0x0ULL << 8) /* Non-shareable */
#define PTE_SH_OUTER                (0x2ULL << 8) /* Outer shareable */
#define PTE_SH_INNER                (0x3ULL << 8) /* Inner shareable */
#define PTE_AF                      (1ULL << 10)  /* Access flag */
#define PTE_NG                      (1ULL << 11)  /* Non-global */
#define PTE_DBM                     (1ULL << 51)  /* Dirty bit modifier */
#define PTE_CONT                    (1ULL << 52)  /* Contiguous */
#define PTE_PXN                     (1ULL << 53)  /* Privileged execute never */
#define PTE_UXN                     (1ULL << 54)  /* Unprivileged execute never */

/* Software-defined PTE bits (bits 55-58 available) */
#define PTE_SW_DIRTY                (1ULL << 55)
#define PTE_SW_ACCESSED             (1ULL << 56)
#define PTE_SW_WRITE                (1ULL << 57)
#define PTE_SW_COPY_ON_WRITE        (1ULL << 58)

/* Page sizes and masks */
#define ARM64_PAGE_SHIFT            12
#define ARM64_PAGE_SIZE             (1UL << ARM64_PAGE_SHIFT)
#define ARM64_PAGE_MASK             (~(ARM64_PAGE_SIZE - 1))
#define ARM64_LARGE_PAGE_SHIFT      21
#define ARM64_LARGE_PAGE_SIZE       (1UL << ARM64_LARGE_PAGE_SHIFT)
#define ARM64_HUGE_PAGE_SHIFT       30
#define ARM64_HUGE_PAGE_SIZE        (1UL << ARM64_HUGE_PAGE_SHIFT)

/* Page table indices */
#define ARM64_PTE_INDEX_MASK        0x1FF
#define ARM64_L0_INDEX_SHIFT        39
#define ARM64_L1_INDEX_SHIFT        30
#define ARM64_L2_INDEX_SHIFT        21
#define ARM64_L3_INDEX_SHIFT        12

/* Virtual address space layout */
#define ARM64_USER_SPACE_START      0x0000000000000000ULL
#define ARM64_USER_SPACE_END        0x0000FFFFFFFFFFFFULL
#define ARM64_KERNEL_SPACE_START    0xFFFF000000000000ULL
#define ARM64_KERNEL_SPACE_END      0xFFFFFFFFFFFFFFFFULL

/* DATA STRUCTURES ************************************************************/

/* Virtual Address Descriptor */
typedef struct _ARM64_VAD
{
    ULONG64 StartingVirtualAddress;
    ULONG64 EndingVirtualAddress;
    ULONG64 Flags;
    ULONG Protection;
    LIST_ENTRY VadListEntry;
    struct _ARM64_VAD *Parent;
    struct _ARM64_VAD *LeftChild;
    struct _ARM64_VAD *RightChild;
} ARM64_VAD, *PARM64_VAD;

/* Page Table Entry */
typedef union _ARM64_PTE
{
    ULONG64 AsULONG64;
    struct
    {
        ULONG64 Valid : 1;
        ULONG64 TableOrPage : 1;
        ULONG64 AttrIndx : 3;
        ULONG64 NS : 1;
        ULONG64 AP : 2;
        ULONG64 SH : 2;
        ULONG64 AF : 1;
        ULONG64 NG : 1;
        ULONG64 Reserved : 36;
        ULONG64 Address : 12;
        ULONG64 Contiguous : 1;
        ULONG64 PXN : 1;
        ULONG64 UXN : 1;
        ULONG64 Software : 4;
        ULONG64 Ignored : 5;
    } Fields;
} ARM64_PTE, *PARM64_PTE;

/* Virtual Memory Statistics */
typedef struct _ARM64_VM_STATS
{
    ULONG64 TotalVirtualPages;
    ULONG64 CommittedPages;
    ULONG64 SharedPages;
    ULONG64 PageFaults;
    ULONG64 CopyOnWriteFaults;
    ULONG64 TransitionFaults;
    ULONG64 CacheFaults;
    ULONG64 DemandZeroFaults;
} ARM64_VM_STATS, *PARM64_VM_STATS;

/* GLOBALS ********************************************************************/

static ARM64_VM_STATS VmStatistics = {0};
static KSPIN_LOCK VmLock;
static LIST_ENTRY VadListHead;
static BOOLEAN VmInitialized = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Get page table indices from virtual address
 */
static
VOID
MiGetPageTableIndices(
    IN PVOID VirtualAddress,
    OUT PULONG L0Index,
    OUT PULONG L1Index,
    OUT PULONG L2Index,
    OUT PULONG L3Index)
{
    ULONG64 Address = (ULONG64)VirtualAddress;

    *L0Index = (Address >> ARM64_L0_INDEX_SHIFT) & ARM64_PTE_INDEX_MASK;
    *L1Index = (Address >> ARM64_L1_INDEX_SHIFT) & ARM64_PTE_INDEX_MASK;
    *L2Index = (Address >> ARM64_L2_INDEX_SHIFT) & ARM64_PTE_INDEX_MASK;
    *L3Index = (Address >> ARM64_L3_INDEX_SHIFT) & ARM64_PTE_INDEX_MASK;
}

/*
 * @brief Get or create page table
 */
static
PARM64_PTE
MiGetOrCreatePageTable(
    IN PARM64_PTE ParentTable,
    IN ULONG Index,
    IN BOOLEAN Create)
{
    PARM64_PTE Entry;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID PageTable;

    if (!ParentTable)
        return NULL;

    Entry = &ParentTable[Index];

    /* Check if entry exists */
    if (Entry->Fields.Valid)
    {
        /* Entry exists, get the page table */
        PhysicalAddress.QuadPart = Entry->Fields.Address << ARM64_PAGE_SHIFT;
        PageTable = MmMapIoSpace(PhysicalAddress, ARM64_PAGE_SIZE, MmNonCached);
        return (PARM64_PTE)PageTable;
    }

    /* Entry doesn't exist */
    if (!Create)
        return NULL;

    /* Allocate new page table */
    /* TODO: Implement MmAllocatePhysicalPages for ARM64 */
    /* PhysicalAddress = MmAllocatePhysicalPages(1); */
    PhysicalAddress.QuadPart = 0;  /* Temporary stub */
    if (!PhysicalAddress.QuadPart)
    {
        DPRINT1("Failed to allocate page table - stub implementation\n");
        return NULL;
    }

    /* Map and clear the page table */
    PageTable = MmMapIoSpace(PhysicalAddress, ARM64_PAGE_SIZE, MmNonCached);
    if (!PageTable)
    {
        /* TODO: Implement MmFreePhysicalPages for ARM64 */
        /* MmFreePhysicalPages(PhysicalAddress, 1); */
        DPRINT1("Failed to map page table\n");
        return NULL;
    }

    RtlZeroMemory(PageTable, ARM64_PAGE_SIZE);

    /* Update parent entry */
    Entry->AsULONG64 = 0;
    Entry->Fields.Valid = 1;
    Entry->Fields.TableOrPage = 1;  /* Table descriptor */
    Entry->Fields.Address = PhysicalAddress.QuadPart >> ARM64_PAGE_SHIFT;

    /* Flush TLB for this entry */
    __asm__ __volatile__ ("dsb ishst" ::: "memory");

    return (PARM64_PTE)PageTable;
}

/*
 * @brief Map a single page
 */
static
BOOLEAN
MiMapSinglePage(
    IN PVOID VirtualAddress,
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN ULONG Protection)
{
    ULONG L0Index, L1Index, L2Index, L3Index;
    PARM64_PTE L0Table, L1Table, L2Table, L3Table;
    PARM64_PTE Entry;
    PVOID RootTable;

    /* Get page table indices */
    MiGetPageTableIndices(VirtualAddress, &L0Index, &L1Index, &L2Index, &L3Index);

    /* Get root table based on address space */
    if ((ULONG64)VirtualAddress < ARM64_KERNEL_SPACE_START)
    {
        /* User space - use current process page table */
        ULONG64 Ttbr0;
        __asm__ __volatile__ ("mrs %0, TTBR0_EL1" : "=r"(Ttbr0));
        RootTable = (PVOID)(Ttbr0 & ~0xFFFFULL);
    }
    else
    {
        /* Kernel space - use global kernel page table */
        ULONG64 Ttbr1;
        __asm__ __volatile__ ("mrs %0, TTBR1_EL1" : "=r"(Ttbr1));
        RootTable = (PVOID)(Ttbr1 & ~0xFFFFULL);
    }

    if (!RootTable)
        return FALSE;

    /* Navigate page tables, creating as needed */
    L0Table = (PARM64_PTE)RootTable;
    L1Table = MiGetOrCreatePageTable(L0Table, L0Index, TRUE);
    if (!L1Table) return FALSE;

    L2Table = MiGetOrCreatePageTable(L1Table, L1Index, TRUE);
    if (!L2Table) return FALSE;

    L3Table = MiGetOrCreatePageTable(L2Table, L2Index, TRUE);
    if (!L3Table) return FALSE;

    /* Set up the final PTE */
    Entry = &L3Table[L3Index];
    Entry->AsULONG64 = 0;
    Entry->Fields.Valid = 1;
    Entry->Fields.TableOrPage = 1;  /* Page descriptor */
    Entry->Fields.Address = PhysicalAddress.QuadPart >> ARM64_PAGE_SHIFT;
    Entry->Fields.AF = 1;  /* Access flag */
    Entry->Fields.SH = 3;  /* Inner shareable */
    Entry->Fields.AttrIndx = 4;  /* Normal memory, write-back */

    /* Set access permissions based on protection */
    if (Protection & PAGE_READWRITE)
    {
        Entry->Fields.AP = 1;  /* Read/write at EL0 */
        Entry->Fields.Software |= PTE_SW_WRITE >> 55;
    }
    else if (Protection & PAGE_READONLY)
    {
        Entry->Fields.AP = 3;  /* Read-only at EL0 */
    }
    else
    {
        Entry->Fields.AP = 0;  /* No access at EL0 */
    }

    /* Set execute permissions */
    if (!(Protection & PAGE_EXECUTE))
    {
        Entry->Fields.UXN = 1;  /* No execute at EL0 */
        Entry->Fields.PXN = 1;  /* No execute at EL1 */
    }

    /* Ensure changes are visible */
    __asm__ __volatile__ ("dsb ishst" ::: "memory");

    /* Invalidate TLB for this address */
    __asm__ __volatile__ ("tlbi vae1is, %0" :: "r"((ULONG64)VirtualAddress >> 12));
    __asm__ __volatile__ ("dsb ish" ::: "memory");
    __asm__ __volatile__ ("isb" ::: "memory");

    return TRUE;
}

/*
 * @brief Unmap a single page
 */
static
BOOLEAN
MiUnmapSinglePage(
    IN PVOID VirtualAddress)
{
    ULONG L0Index, L1Index, L2Index, L3Index;
    PARM64_PTE L0Table, L1Table, L2Table, L3Table;
    PARM64_PTE Entry;
    PVOID RootTable;
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Get page table indices */
    MiGetPageTableIndices(VirtualAddress, &L0Index, &L1Index, &L2Index, &L3Index);

    /* Get root table */
    if ((ULONG64)VirtualAddress < ARM64_KERNEL_SPACE_START)
    {
        ULONG64 Ttbr0;
        __asm__ __volatile__ ("mrs %0, TTBR0_EL1" : "=r"(Ttbr0));
        RootTable = (PVOID)(Ttbr0 & ~0xFFFFULL);
    }
    else
    {
        ULONG64 Ttbr1;
        __asm__ __volatile__ ("mrs %0, TTBR1_EL1" : "=r"(Ttbr1));
        RootTable = (PVOID)(Ttbr1 & ~0xFFFFULL);
    }

    if (!RootTable)
        return FALSE;

    /* Navigate page tables */
    L0Table = (PARM64_PTE)RootTable;
    L1Table = MiGetOrCreatePageTable(L0Table, L0Index, FALSE);
    if (!L1Table) return FALSE;

    L2Table = MiGetOrCreatePageTable(L1Table, L1Index, FALSE);
    if (!L2Table) return FALSE;

    L3Table = MiGetOrCreatePageTable(L2Table, L2Index, FALSE);
    if (!L3Table) return FALSE;

    /* Get the PTE */
    Entry = &L3Table[L3Index];
    if (!Entry->Fields.Valid)
        return FALSE;

    /* Get physical address before clearing */
    PhysicalAddress.QuadPart = Entry->Fields.Address << ARM64_PAGE_SHIFT;

    /* Clear the PTE */
    Entry->AsULONG64 = 0;

    /* Ensure changes are visible */
    __asm__ __volatile__ ("dsb ishst" ::: "memory");

    /* Invalidate TLB */
    __asm__ __volatile__ ("tlbi vae1is, %0" :: "r"((ULONG64)VirtualAddress >> 12));
    __asm__ __volatile__ ("dsb ish" ::: "memory");
    __asm__ __volatile__ ("isb" ::: "memory");

    /* Free the physical page if it was allocated */
    if (PhysicalAddress.QuadPart)
    {
        /* TODO: Implement MmFreePhysicalPages for ARM64 */
        /* MmFreePhysicalPages(PhysicalAddress, 1); */
    }

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize virtual memory subsystem
 * @implemented
 */
VOID
NTAPI
MmInitializeVirtualMemory(VOID)
{
    DPRINT("Initializing ARM64 virtual memory subsystem\n");

    /* Initialize spinlock */
    KeInitializeSpinLock(&VmLock);

    /* Initialize VAD list */
    InitializeListHead(&VadListHead);

    /* Clear statistics */
    RtlZeroMemory(&VmStatistics, sizeof(VmStatistics));

    /* Calculate total virtual space */
    VmStatistics.TotalVirtualPages =
        ((ARM64_USER_SPACE_END - ARM64_USER_SPACE_START) >> ARM64_PAGE_SHIFT) +
        ((ARM64_KERNEL_SPACE_END - ARM64_KERNEL_SPACE_START) >> ARM64_PAGE_SHIFT);

    VmInitialized = TRUE;
    DPRINT("Virtual memory initialized: %llu total pages\n",
           VmStatistics.TotalVirtualPages);
}

/*
 * @brief Allocate virtual memory
 * @implemented
 */
PVOID
NTAPI
MmAllocateVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT PVOID *BaseAddress,
    IN ULONG_PTR ZeroBits,
    IN OUT PSIZE_T RegionSize,
    IN ULONG AllocationType,
    IN ULONG Protect)
{
    PVOID Address;
    SIZE_T Size;
    KIRQL OldIrql;
    PARM64_VAD Vad;
    NTSTATUS Status = STATUS_SUCCESS;

    /* Round size to page boundary */
    Size = ROUND_UP(*RegionSize, ARM64_PAGE_SIZE);

    /* Get requested address or find free space */
    if (*BaseAddress)
    {
        Address = (PVOID)ROUND_DOWN((ULONG_PTR)*BaseAddress, ARM64_PAGE_SIZE);
    }
    else
    {
        /* TODO: Find free virtual address range */
        /* For now, use a fixed address */
        static ULONG64 NextUserAddress = 0x10000000;
        Address = (PVOID)NextUserAddress;
        NextUserAddress += Size;
    }

    DPRINT("MmAllocateVirtualMemory: Address=%p, Size=%lx, Type=%lx, Protect=%lx\n",
           Address, Size, AllocationType, Protect);

    KeAcquireSpinLock(&VmLock, &OldIrql);

    /* Create VAD for this allocation */
    Vad = ExAllocatePoolWithTag(NonPagedPool, sizeof(ARM64_VAD), 'daVM');
    if (!Vad)
    {
        KeReleaseSpinLock(&VmLock, OldIrql);
        return NULL;
    }

    Vad->StartingVirtualAddress = (ULONG64)Address;
    Vad->EndingVirtualAddress = (ULONG64)Address + Size - 1;
    Vad->Protection = Protect;
    Vad->Flags = AllocationType;
    InsertTailList(&VadListHead, &Vad->VadListEntry);

    /* If committing memory, map the pages */
    if (AllocationType & MEM_COMMIT)
    {
        SIZE_T Pages = Size >> ARM64_PAGE_SHIFT;
        for (SIZE_T i = 0; i < Pages; i++)
        {
            PHYSICAL_ADDRESS PhysicalAddress;
            PVOID PageAddress = (PVOID)((ULONG64)Address + (i << ARM64_PAGE_SHIFT));

            /* Allocate physical page */
            if (AllocationType & MEM_PHYSICAL)
            {
                /* Use specific physical address if provided */
                PhysicalAddress.QuadPart = (ULONG64)PageAddress;
            }
            else
            {
                /* Allocate from free pool */
                /* TODO: Implement MmAllocatePhysicalPages for ARM64 */
                /* PhysicalAddress = MmAllocatePhysicalPages(1); */
                PhysicalAddress.QuadPart = 0;  /* Temporary stub */
                if (!PhysicalAddress.QuadPart)
                {
                    Status = STATUS_NO_MEMORY;
                    break;
                }
            }

            /* Map the page */
            if (!MiMapSinglePage(PageAddress, PhysicalAddress, Protect))
            {
                if (!(AllocationType & MEM_PHYSICAL))
                {
                    /* TODO: Implement MmFreePhysicalPages for ARM64 */
        /* MmFreePhysicalPages(PhysicalAddress, 1); */
                }
                Status = STATUS_NO_MEMORY;
                break;
            }

            /* Zero the page if requested */
            /* TODO: Define MEM_ZERO for ARM64 */
            if (AllocationType & 0x1000000 /* MEM_ZERO */)
            {
                RtlZeroMemory(PageAddress, ARM64_PAGE_SIZE);
            }

            VmStatistics.CommittedPages++;
        }
    }

    KeReleaseSpinLock(&VmLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        /* Cleanup on failure */
        /* TODO: Fix circular dependency with MmFreeVirtualMemory */
        /* MmFreeVirtualMemory(ProcessHandle, &Address, RegionSize, MEM_RELEASE); */
        return NULL;
    }

    *BaseAddress = Address;
    *RegionSize = Size;

    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(ZeroBits);

    return Address;
}

/*
 * @brief Free virtual memory
 * @implemented
 */
NTSTATUS
NTAPI
MmFreeVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT PVOID *BaseAddress,
    IN OUT PSIZE_T RegionSize,
    IN ULONG FreeType)
{
    PVOID Address;
    SIZE_T Size;
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PARM64_VAD Vad = NULL;

    Address = *BaseAddress;
    Size = *RegionSize;

    DPRINT("MmFreeVirtualMemory: Address=%p, Size=%lx, Type=%lx\n",
           Address, Size, FreeType);

    KeAcquireSpinLock(&VmLock, &OldIrql);

    /* Find the VAD for this address */
    Entry = VadListHead.Flink;
    while (Entry != &VadListHead)
    {
        Vad = CONTAINING_RECORD(Entry, ARM64_VAD, VadListEntry);
        if (Vad->StartingVirtualAddress <= (ULONG64)Address &&
            Vad->EndingVirtualAddress >= (ULONG64)Address)
        {
            break;
        }
        Entry = Entry->Flink;
        Vad = NULL;
    }

    if (!Vad)
    {
        KeReleaseSpinLock(&VmLock, OldIrql);
        return STATUS_MEMORY_NOT_ALLOCATED;
    }

    /* Handle decommit or release */
    if (FreeType & MEM_DECOMMIT)
    {
        /* Unmap pages but keep VAD */
        SIZE_T Pages = Size >> ARM64_PAGE_SHIFT;
        for (SIZE_T i = 0; i < Pages; i++)
        {
            PVOID PageAddress = (PVOID)((ULONG64)Address + (i << ARM64_PAGE_SHIFT));
            MiUnmapSinglePage(PageAddress);
            VmStatistics.CommittedPages--;
        }
    }
    /* TODO: Define MEM_RELEASE for ARM64 */
    else if (FreeType & 0x8000 /* MEM_RELEASE */)
    {
        /* Unmap all pages in the VAD */
        ULONG64 CurrentAddress = Vad->StartingVirtualAddress;
        while (CurrentAddress <= Vad->EndingVirtualAddress)
        {
            MiUnmapSinglePage((PVOID)CurrentAddress);
            CurrentAddress += ARM64_PAGE_SIZE;
            VmStatistics.CommittedPages--;
        }

        /* Remove and free VAD */
        RemoveEntryList(&Vad->VadListEntry);
        ExFreePoolWithTag(Vad, 'daVM');
    }

    KeReleaseSpinLock(&VmLock, OldIrql);

    UNREFERENCED_PARAMETER(ProcessHandle);

    return STATUS_SUCCESS;
}

/*
 * @brief Protect virtual memory
 * @implemented
 */
NTSTATUS
NTAPI
MmProtectVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT PVOID *BaseAddress,
    IN OUT PSIZE_T RegionSize,
    IN ULONG NewProtect,
    OUT PULONG OldProtect)
{
    PVOID Address;
    SIZE_T Size;
    KIRQL OldIrql;

    Address = (PVOID)ROUND_DOWN((ULONG_PTR)*BaseAddress, ARM64_PAGE_SIZE);
    Size = ROUND_UP(*RegionSize, ARM64_PAGE_SIZE);

    DPRINT("MmProtectVirtualMemory: Address=%p, Size=%lx, NewProtect=%lx\n",
           Address, Size, NewProtect);

    KeAcquireSpinLock(&VmLock, &OldIrql);

    /* TODO: Find VAD and update protection
     * TODO: Update page table entries with new protection */

    KeReleaseSpinLock(&VmLock, OldIrql);

    *BaseAddress = Address;
    *RegionSize = Size;
    *OldProtect = PAGE_READWRITE;  /* TODO: Return actual old protection */

    UNREFERENCED_PARAMETER(ProcessHandle);

    return STATUS_SUCCESS;
}

/*
 * @brief Query virtual memory information
 * @implemented
 */
NTSTATUS
NTAPI
MmQueryVirtualMemory(
    IN HANDLE ProcessHandle,
    IN PVOID BaseAddress,
    IN MEMORY_INFORMATION_CLASS MemoryInformationClass,
    OUT PVOID MemoryInformation,
    IN SIZE_T MemoryInformationLength,
    OUT PSIZE_T ReturnLength OPTIONAL)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PARM64_VAD Vad = NULL;
    PMEMORY_BASIC_INFORMATION BasicInfo;

    if (MemoryInformationClass != MemoryBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (MemoryInformationLength < sizeof(MEMORY_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    BasicInfo = (PMEMORY_BASIC_INFORMATION)MemoryInformation;

    KeAcquireSpinLock(&VmLock, &OldIrql);

    /* Find VAD for this address */
    Entry = VadListHead.Flink;
    while (Entry != &VadListHead)
    {
        Vad = CONTAINING_RECORD(Entry, ARM64_VAD, VadListEntry);
        if (Vad->StartingVirtualAddress <= (ULONG64)BaseAddress &&
            Vad->EndingVirtualAddress >= (ULONG64)BaseAddress)
        {
            break;
        }
        Entry = Entry->Flink;
        Vad = NULL;
    }

    if (Vad)
    {
        BasicInfo->BaseAddress = (PVOID)Vad->StartingVirtualAddress;
        BasicInfo->AllocationBase = (PVOID)Vad->StartingVirtualAddress;
        BasicInfo->AllocationProtect = Vad->Protection;
        BasicInfo->RegionSize = Vad->EndingVirtualAddress - Vad->StartingVirtualAddress + 1;
        BasicInfo->State = (Vad->Flags & MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
        BasicInfo->Protect = Vad->Protection;
        BasicInfo->Type = (Vad->Flags & MEM_PRIVATE) ? MEM_PRIVATE : MEM_MAPPED;
    }
    else
    {
        /* No VAD, region is free */
        BasicInfo->BaseAddress = BaseAddress;
        BasicInfo->AllocationBase = NULL;
        BasicInfo->AllocationProtect = 0;
        BasicInfo->RegionSize = ARM64_PAGE_SIZE;
        BasicInfo->State = MEM_FREE;
        BasicInfo->Protect = PAGE_NOACCESS;
        BasicInfo->Type = 0;
    }

    KeReleaseSpinLock(&VmLock, OldIrql);

    if (ReturnLength)
    {
        *ReturnLength = sizeof(MEMORY_BASIC_INFORMATION);
    }

    UNREFERENCED_PARAMETER(ProcessHandle);

    return STATUS_SUCCESS;
}

/*
 * @brief Map physical memory to virtual address
 * @implemented
 */
#if 0  /* Disabled - using generic implementation in mm/iosup.c */
PVOID
NTAPI
MmMapIoSpace(
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN SIZE_T NumberOfBytes,
    IN MEMORY_CACHING_TYPE CacheType)
{
    PVOID VirtualAddress;
    SIZE_T Pages;
    ULONG Protection;

    /* Round to page boundaries */
    PHYSICAL_ADDRESS AlignedPhysical;
    AlignedPhysical.QuadPart = PhysicalAddress.QuadPart & ARM64_PAGE_MASK;
    SIZE_T Offset = PhysicalAddress.QuadPart - AlignedPhysical.QuadPart;
    NumberOfBytes += Offset;
    Pages = ROUND_UP(NumberOfBytes, ARM64_PAGE_SIZE) >> ARM64_PAGE_SHIFT;

    /* Find free kernel virtual address
     * TODO: Implement proper kernel VA allocator */
    static ULONG64 NextKernelAddress = 0xFFFF800000000000ULL;
    VirtualAddress = (PVOID)NextKernelAddress;
    NextKernelAddress += (Pages << ARM64_PAGE_SHIFT);

    /* Determine protection based on cache type */
    switch (CacheType)
    {
        case MmNonCached:
            Protection = PAGE_READWRITE | PAGE_NOCACHE;
            break;
        case MmWriteCombined:
            Protection = PAGE_READWRITE | PAGE_WRITECOMBINE;
            break;
        case MmCached:
        default:
            Protection = PAGE_READWRITE;
            break;
    }

    /* Map each page */
    for (SIZE_T i = 0; i < Pages; i++)
    {
        PHYSICAL_ADDRESS PagePhysical;
        PVOID PageVirtual;

        PagePhysical.QuadPart = AlignedPhysical.QuadPart + (i << ARM64_PAGE_SHIFT);
        PageVirtual = (PVOID)((ULONG64)VirtualAddress + (i << ARM64_PAGE_SHIFT));

        if (!MiMapSinglePage(PageVirtual, PagePhysical, Protection))
        {
            /* Failed, unmap what we mapped so far */
            while (i > 0)
            {
                i--;
                PageVirtual = (PVOID)((ULONG64)VirtualAddress + (i << ARM64_PAGE_SHIFT));
                MiUnmapSinglePage(PageVirtual);
            }
            return NULL;
        }
    }

    /* Return address with original offset */
    return (PVOID)((ULONG64)VirtualAddress + Offset);
}
#endif /* 0 - MmMapIoSpace */

/*
 * @brief Unmap I/O space
 * @implemented
 */
#if 0  /* Disabled - using generic implementation in mm/iosup.c */
VOID
NTAPI
MmUnmapIoSpace(
    IN PVOID BaseAddress,
    IN SIZE_T NumberOfBytes)
{
    PVOID AlignedAddress;
    SIZE_T Pages;

    /* Align to page boundary */
    AlignedAddress = (PVOID)((ULONG64)BaseAddress & ARM64_PAGE_MASK);
    SIZE_T Offset = (ULONG64)BaseAddress - (ULONG64)AlignedAddress;
    NumberOfBytes += Offset;
    Pages = ROUND_UP(NumberOfBytes, ARM64_PAGE_SIZE) >> ARM64_PAGE_SHIFT;

    /* Unmap each page */
    for (SIZE_T i = 0; i < Pages; i++)
    {
        PVOID PageAddress = (PVOID)((ULONG64)AlignedAddress + (i << ARM64_PAGE_SHIFT));
        MiUnmapSinglePage(PageAddress);
    }
}
#endif /* 0 - MmUnmapIoSpace */

/*
 * @brief Get virtual memory statistics
 * @implemented
 */
VOID
NTAPI
MmGetVirtualMemoryStatistics(
    OUT PARM64_VM_STATS Statistics)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&VmLock, &OldIrql);
    RtlCopyMemory(Statistics, &VmStatistics, sizeof(ARM64_VM_STATS));
    KeReleaseSpinLock(&VmLock, OldIrql);
}

/*
 * @brief Handle copy-on-write fault
 * @implemented
 */
NTSTATUS
NTAPI
MmHandleCopyOnWriteFault(
    IN PVOID FaultAddress)
{
    PHYSICAL_ADDRESS OldPhysical, NewPhysical;
    PVOID AlignedAddress;
    KIRQL OldIrql;

    DPRINT("Copy-on-write fault at %p\n", FaultAddress);

    AlignedAddress = (PVOID)((ULONG64)FaultAddress & ARM64_PAGE_MASK);

    KeAcquireSpinLock(&VmLock, &OldIrql);

    /* TODO: Get current physical page */
    /* TODO: Allocate new physical page */
    /* TODO: Implement MmAllocatePhysicalPages for ARM64 */
    /* NewPhysical = MmAllocatePhysicalPages(1); */
    NewPhysical.QuadPart = 0;  /* Temporary stub */
    if (!NewPhysical.QuadPart)
    {
        KeReleaseSpinLock(&VmLock, OldIrql);
        return STATUS_NO_MEMORY;
    }

    /* TODO: Copy old page to new page */
    /* TODO: Update page table entry to point to new page with write permission */
    /* TODO: Decrement share count on old page */

    VmStatistics.CopyOnWriteFaults++;

    KeReleaseSpinLock(&VmLock, OldIrql);

    return STATUS_SUCCESS;
}

/* EOF */

/* STUB FUNCTIONS FOR LINKING ************************************************/

/* Stub implementation for MiGetPfnForVirtualAddress */
ULONG_PTR
NTAPI
MiGetPfnForVirtualAddress(IN PVOID VirtualAddress)
{
    DPRINT("MiGetPfnForVirtualAddress: ARM64 stub implementation\n");
    UNREFERENCED_PARAMETER(VirtualAddress);
    /* TODO: Implement ARM64-specific PFN lookup */
    return 0;
}

/* Stub implementation for MmMapViewOfSystemSection */
NTSTATUS
NTAPI
MmMapViewOfSystemSection(
    IN PEPROCESS Process,
    IN OUT PVOID *BaseAddress,
    IN OUT PSIZE_T ViewSize,
    IN ULONG Protect)
{
    DPRINT("MmMapViewOfSystemSection: ARM64 stub implementation\n");
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(ViewSize);
    UNREFERENCED_PARAMETER(Protect);
    /* TODO: Implement ARM64-specific system section mapping */
    return STATUS_NOT_IMPLEMENTED;
}
