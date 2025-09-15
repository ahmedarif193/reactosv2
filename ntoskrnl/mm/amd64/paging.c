/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/mm/amd64/paging.c
 * PURPOSE:         AMD64 Advanced Paging Features and Helpers
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "../amd64/amd64_mm.h"

/* DEFINES *******************************************************************/

/* CPUID feature bits */
#define CPUID_LA57_BIT      (1 << 16)  /* Bit 16 of CPUID.07H:ECX */

/* CR4 bits */
#define CR4_LA57            (1ULL << 12)  /* 5-level paging enable */

/* 5-level paging constants */
#define PML5_INDEX_MASK     0x01FF000000000000ULL
#define PML5_INDEX_SHIFT    48

/* GLOBALS *******************************************************************/

BOOLEAN Mi5LevelPagingSupported = FALSE;
BOOLEAN Mi5LevelPagingEnabled = FALSE;
ULONG MiPagingLevels = 4;  /* Default to 4-level paging */

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Detects CPU support for 5-level paging (LA57)
 * @return TRUE if supported, FALSE otherwise
 */
static BOOLEAN
MiDetect5LevelPaging(VOID)
{
    INT CpuInfo[4];

    /* Check for extended features */
    __cpuid(CpuInfo, 7);

    /* Check ECX bit 16 for LA57 support */
    if (CpuInfo[2] & CPUID_LA57_BIT)
    {
        DPRINT1("CPU supports 5-level paging (LA57)\n");
        return TRUE;
    }

    return FALSE;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Initializes advanced paging features
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
MmInitializeAdvancedPaging(VOID)
{
    /* Detect 5-level paging support */
    Mi5LevelPagingSupported = MiDetect5LevelPaging();

    if (Mi5LevelPagingSupported)
    {
        /* For now, we don't enable 5-level paging by default */
        /* This would require significant changes to memory layout */
        DPRINT1("5-level paging supported but not enabled\n");

        /* To enable in the future:
         * 1. Set CR4.LA57 bit
         * 2. Update all page table walking code
         * 3. Adjust virtual address space layout
         * 4. Update canonical address checks
         */
    }

    DPRINT1("Paging initialized with %lu levels\n", MiPagingLevels);
}

/*
 * @implemented
 * @brief Enables 5-level paging if supported
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmEnable5LevelPaging(VOID)
{
    ULONG_PTR Cr4;

    if (!Mi5LevelPagingSupported)
    {
        DPRINT1("5-level paging not supported by CPU\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (Mi5LevelPagingEnabled)
    {
        DPRINT1("5-level paging already enabled\n");
        return STATUS_SUCCESS;
    }

    /* This requires system restart to take effect */
    /* Cannot be changed after boot */
    DPRINT1("5-level paging requires system configuration at boot time\n");
    return STATUS_NOT_IMPLEMENTED;

    /* Future implementation: */
#if 0
    /* Disable interrupts */
    _disable();

    /* Enable LA57 in CR4 */
    Cr4 = __readcr4();
    __writecr4(Cr4 | CR4_LA57);

    /* Update globals */
    Mi5LevelPagingEnabled = TRUE;
    MiPagingLevels = 5;

    /* Re-enable interrupts */
    _enable();

    DPRINT1("5-level paging enabled\n");
    return STATUS_SUCCESS;
#endif
}

/*
 * @implemented
 * @brief Checks if 5-level paging is supported
 * @return TRUE if supported, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIs5LevelPagingSupported(VOID)
{
    return Mi5LevelPagingSupported;
}

/*
 * @implemented
 * @brief Checks if 5-level paging is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIs5LevelPagingEnabled(VOID)
{
    return Mi5LevelPagingEnabled;
}

/*
 * @implemented
 * @brief Gets the number of paging levels
 * @return 4 or 5
 */
ULONG
NTAPI
MmGetPagingLevels(VOID)
{
    return MiPagingLevels;
}

/* PAGE TABLE HELPER FUNCTIONS ***********************************************/

/*
 * @implemented
 * @brief Walks the page table hierarchy to find a PTE
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address
 * @param Create - TRUE to create missing tables
 * @return Pointer to PTE or NULL
 */
PMMPTE
NTAPI
MmGetPteForAddress(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Create)
{
    PMMPTE Pml5e, Pml4e, Pdpte, Pde, Pte;
    ULONG_PTR Cr3;
    PFN_NUMBER NewPage;
    MMPTE TempPte;

    /* Get CR3 for the process */
    if (Process)
    {
        Cr3 = Process->Pcb.DirectoryTableBase[0];
    }
    else
    {
        Cr3 = __readcr3();
    }

    /* Handle 5-level paging if enabled */
    if (Mi5LevelPagingEnabled)
    {
        /* Get PML5 entry */
        Pml5e = (PMMPTE)(Cr3 & ~0xFFF);
        Pml5e += ((ULONG_PTR)Address >> PML5_INDEX_SHIFT) & 0x1FF;

        if (!Pml5e->u.Hard.Valid)
        {
            if (!Create) return NULL;

            /* Allocate PML4 */
            NewPage = MmAllocPage(MC_SYSTEM);
            if (!NewPage) return NULL;

            RtlZeroMemory(MiPfnToSystemAddress(NewPage), PAGE_SIZE);

            TempPte.u.Long = 0;
            TempPte.u.Hard.Valid = 1;
            TempPte.u.Hard.Write = 1;
            TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
            TempPte.u.Hard.PageFrameNumber = NewPage;
            *Pml5e = TempPte;
        }

        /* Continue with PML4 */
        Cr3 = Pml5e->u.Hard.PageFrameNumber << PAGE_SHIFT;
    }

    /* Standard 4-level (or continue from 5-level) page walk */
    /* Get PML4 entry */
    Pml4e = (PMMPTE)(Cr3 & ~0xFFF);
    Pml4e += ((ULONG_PTR)Address >> 39) & 0x1FF;

    if (!Pml4e->u.Hard.Valid)
    {
        if (!Create) return NULL;

        /* Allocate PDPT */
        NewPage = MmAllocPage(MC_SYSTEM);
        if (!NewPage) return NULL;

        RtlZeroMemory(MiPfnToSystemAddress(NewPage), PAGE_SIZE);

        TempPte.u.Long = 0;
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.Write = 1;
        TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
        TempPte.u.Hard.PageFrameNumber = NewPage;
        *Pml4e = TempPte;
    }

    /* Get PDPT entry */
    Pdpte = (PMMPTE)(Pml4e->u.Hard.PageFrameNumber << PAGE_SHIFT);
    Pdpte += ((ULONG_PTR)Address >> 30) & 0x1FF;

    if (!Pdpte->u.Hard.Valid)
    {
        if (!Create) return NULL;

        /* Allocate PD */
        NewPage = MmAllocPage(MC_SYSTEM);
        if (!NewPage) return NULL;

        RtlZeroMemory(MiPfnToSystemAddress(NewPage), PAGE_SIZE);

        TempPte.u.Long = 0;
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.Write = 1;
        TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
        TempPte.u.Hard.PageFrameNumber = NewPage;
        *Pdpte = TempPte;
    }

    /* Check for 1GB page */
    if (Pdpte->u.Hard.LargePage)
    {
        /* This is a 1GB page, PDPTE is the final entry */
        return Pdpte;
    }

    /* Get PD entry */
    Pde = (PMMPTE)(Pdpte->u.Hard.PageFrameNumber << PAGE_SHIFT);
    Pde += ((ULONG_PTR)Address >> 21) & 0x1FF;

    if (!Pde->u.Hard.Valid)
    {
        if (!Create) return NULL;

        /* Allocate PT */
        NewPage = MmAllocPage(MC_SYSTEM);
        if (!NewPage) return NULL;

        RtlZeroMemory(MiPfnToSystemAddress(NewPage), PAGE_SIZE);

        TempPte.u.Long = 0;
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.Write = 1;
        TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
        TempPte.u.Hard.PageFrameNumber = NewPage;
        *Pde = TempPte;
    }

    /* Check for 2MB page */
    if (Pde->u.Hard.LargePage)
    {
        /* This is a 2MB page, PDE is the final entry */
        return Pde;
    }

    /* Get PT entry */
    Pte = (PMMPTE)(Pde->u.Hard.PageFrameNumber << PAGE_SHIFT);
    Pte += ((ULONG_PTR)Address >> 12) & 0x1FF;

    return Pte;
}

/*
 * @implemented
 * @brief Determines the page size for a virtual address
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address
 * @return Page size (4KB, 2MB, 1GB) or 0 if not mapped
 */
SIZE_T
NTAPI
MmGetPageSize(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE Pml4e, Pdpte, Pde, Pte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    SIZE_T PageSize = 0;

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Walk page tables */
    Pml4e = MiGetPml4Entry(Address);
    if (!Pml4e || !Pml4e->u.Hard.Valid)
        goto Exit;

    Pdpte = MiGetPdptEntry(Address);
    if (!Pdpte || !Pdpte->u.Hard.Valid)
        goto Exit;

    /* Check for 1GB page */
    if (Pdpte->u.Hard.LargePage)
    {
        PageSize = PAGE_SIZE_1GB;
        goto Exit;
    }

    Pde = MiGetPdeEntry(Address);
    if (!Pde || !Pde->u.Hard.Valid)
        goto Exit;

    /* Check for 2MB page */
    if (Pde->u.Hard.LargePage)
    {
        PageSize = PAGE_SIZE_2MB;
        goto Exit;
    }

    Pte = MiGetPteEntry(Address);
    if (Pte && Pte->u.Hard.Valid)
    {
        PageSize = PAGE_SIZE;
    }

Exit:
    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return PageSize;
}

/*
 * @implemented
 * @brief Splits a large page into smaller pages
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address within the large page
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmSplitLargePage(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE LargePte, NewPte;
    PFN_NUMBER BasePfn, NewTablePfn;
    MMPTE TempPte, OldPte;
    SIZE_T PageSize;
    ULONG i, PageCount;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    PVOID NewTable;

    /* Get page size */
    PageSize = MmGetPageSize(Process, Address);
    if (PageSize <= PAGE_SIZE)
    {
        /* Not a large page or not mapped */
        return STATUS_INVALID_PARAMETER;
    }

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    if (PageSize == PAGE_SIZE_2MB)
    {
        /* Split 2MB page into 4KB pages */
        LargePte = MiGetPdeAddress(Address);
        if (!LargePte || !LargePte->u.Hard.Valid || !LargePte->u.Hard.LargePage)
        {
            if (ProcessAttached)
                KeUnstackDetachProcess(&ApcState);
            return STATUS_INVALID_PARAMETER;
        }

        /* Save old PTE */
        OldPte = *LargePte;
        BasePfn = OldPte.u.Hard.PageFrameNumber;

        /* Allocate new page table */
        NewTablePfn = MmAllocPage(MC_SYSTEM);
        if (!NewTablePfn)
        {
            if (ProcessAttached)
                KeUnstackDetachProcess(&ApcState);
            return STATUS_NO_MEMORY;
        }

        /* Map and initialize new page table */
        NewTable = MiPfnToSystemAddress(NewTablePfn);
        NewPte = (PMMPTE)NewTable;

        /* Create 512 4KB PTEs */
        for (i = 0; i < 512; i++)
        {
            TempPte = OldPte;
            TempPte.u.Hard.LargePage = 0;
            TempPte.u.Hard.PageFrameNumber = BasePfn + i;
            NewPte[i] = TempPte;
        }

        /* Update PDE to point to new page table */
        TempPte.u.Long = 0;
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.Write = OldPte.u.Hard.Write;
        TempPte.u.Hard.Owner = OldPte.u.Hard.Owner;
        TempPte.u.Hard.PageFrameNumber = NewTablePfn;
        *LargePte = TempPte;
    }
    else if (PageSize == PAGE_SIZE_1GB)
    {
        /* Split 1GB page - more complex, need to create PD and PTs */
        /* TODO: Implement 1GB page splitting */
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Flush TLB for the affected range */
    MiFlushTlbRange(Address, PageSize / PAGE_SIZE);

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    DPRINT("Large page split: %p, size=%lx\n", Address, PageSize);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Validates a virtual address for canonical form
 * @param Address - Virtual address to validate
 * @param Levels - Number of paging levels (4 or 5)
 * @return TRUE if canonical, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIsAddressCanonical(
    _In_ PVOID Address,
    _In_ ULONG Levels)
{
    ULONG_PTR Va = (ULONG_PTR)Address;
    ULONG_PTR SignBit, SignExtend;

    if (Levels == 4)
    {
        /* 48-bit virtual addressing */
        SignBit = 0x0000800000000000ULL;      /* Bit 47 */
        SignExtend = 0xFFFF000000000000ULL;   /* Bits 48-63 */

        if (Va & SignBit)
        {
            /* Sign bit is 1, upper bits must all be 1 */
            return (Va & SignExtend) == SignExtend;
        }
        else
        {
            /* Sign bit is 0, upper bits must all be 0 */
            return (Va & SignExtend) == 0;
        }
    }
    else if (Levels == 5)
    {
        /* 57-bit virtual addressing */
        SignBit = 0x0100000000000000ULL;      /* Bit 56 */
        SignExtend = 0xFE00000000000000ULL;   /* Bits 57-63 */

        if (Va & SignBit)
        {
            /* Sign bit is 1, upper bits must all be 1 */
            return (Va & SignExtend) == SignExtend;
        }
        else
        {
            /* Sign bit is 0, upper bits must all be 0 */
            return (Va & SignExtend) == 0;
        }
    }

    return FALSE;
}

/*
 * @implemented
 * @brief Checks if a page contains a swap entry
 */
BOOLEAN
NTAPI
MmIsPageSwapEntry(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    MMPTE TempPte;

    /* Kernel addresses never have swap entries */
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /* Get PTE for the address */
    PointerPte = MiAddressToPte(Address);
    if (!PointerPte)
        return FALSE;

    TempPte = *PointerPte;

    /* Check if this is a software PTE with swap entry */
    return (!TempPte.u.Hard.Valid && TempPte.u.Long != 0);
}

/*
 * @implemented
 * @brief Deletes a page file mapping
 */
VOID
NTAPI
MmDeletePageFileMapping(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ SWAPENTRY* SwapEntry)
{
    PMMPTE PointerPte;
    MMPTE TempPte;

    /* Get PTE for the address */
    PointerPte = MiAddressToPte(Address);
    if (!PointerPte)
    {
        *SwapEntry = 0;
        return;
    }

    TempPte = *PointerPte;

    /* Extract swap entry if present */
    if (!TempPte.u.Hard.Valid && TempPte.u.Long != 0)
    {
        *SwapEntry = (SWAPENTRY)(TempPte.u.Long >> 12);

        /* Clear the PTE */
        PointerPte->u.Long = 0;

        /* Flush TLB for this address */
        __invlpg(Address);
    }
    else
    {
        *SwapEntry = 0;
    }
}

/*
 * @implemented
 * @brief Gets page file mapping information
 */
VOID
NTAPI
MmGetPageFileMapping(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ SWAPENTRY *SwapEntry)
{
    PMMPTE PointerPte;
    MMPTE TempPte;

    /* Get PTE for the address */
    PointerPte = MiAddressToPte(Address);
    if (!PointerPte)
    {
        *SwapEntry = 0;
        return;
    }

    TempPte = *PointerPte;

    /* Extract swap entry if present */
    if (!TempPte.u.Hard.Valid && TempPte.u.Long != 0)
    {
        *SwapEntry = (SWAPENTRY)(TempPte.u.Long >> 12);
    }
    else
    {
        *SwapEntry = 0;
    }
}

/*
 * @implemented
 * @brief Checks if a page is disabled
 */
BOOLEAN
NTAPI
MmIsDisabledPage(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    MMPTE TempPte;

    /* Get PTE for the address */
    PointerPte = MiAddressToPte(Address);
    if (!PointerPte)
        return TRUE;

    TempPte = *PointerPte;

    /* Page is disabled if PTE is completely zero */
    return (TempPte.u.Long == 0);
}

/*
 * @implemented
 * @brief Sets or clears the dirty bit for a page
 */
VOID
NTAPI
MmSetDirtyBit(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Bit)
{
    PMMPTE PointerPte;
    MMPTE TempPte, OldPte;

    /* Get PTE for the address */
    PointerPte = MiAddressToPte(Address);
    if (!PointerPte)
        return;

    /* Update dirty bit atomically */
    do
    {
        OldPte = *PointerPte;
        if (!OldPte.u.Hard.Valid)
            return;

        TempPte = OldPte;
        TempPte.u.Hard.Dirty = Bit ? 1 : 0;

    } while (InterlockedCompareExchange64((PLONG64)PointerPte,
                                          TempPte.u.Long,
                                          OldPte.u.Long) != OldPte.u.Long);

    /* Flush TLB if we cleared the dirty bit */
    if (!Bit)
    {
        __invlpg(Address);
    }
}

/*
 * @implemented
 * @brief Initializes the global kernel page directory
 */
VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID)
{
    /* This is handled during MmAmd64InitializeMemoryLayout */
    /* The kernel PML4 entries are set up there */
    /* Nothing additional needed here for AMD64 */
}

/* END OF FILE */