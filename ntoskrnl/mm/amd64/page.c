/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/mm/amd64/page.c
 * PURPOSE:         AMD64 Memory Manager Page Management
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>
#include "amd64_mm.h"

/* DEFINES *******************************************************************/

/* AMD64 Page Table Entry Bits */
#define PTE_VALID           0x0000000000000001ULL
#define PTE_WRITE           0x0000000000000002ULL
#define PTE_USER            0x0000000000000004ULL
#define PTE_WRITETHROUGH    0x0000000000000008ULL
#define PTE_CACHEDISABLE    0x0000000000000010ULL
#define PTE_ACCESSED        0x0000000000000020ULL
#define PTE_DIRTY           0x0000000000000040ULL
#define PTE_LARGEPAGE       0x0000000000000080ULL
#define PTE_GLOBAL          0x0000000000000100ULL
#define PTE_COPY            0x0000000000000200ULL  /* Software */
#define PTE_PROTOTYPE       0x0000000000000400ULL  /* Software */
#define PTE_TRANSITION      0x0000000000000800ULL  /* Software */
#define PTE_NX              0x8000000000000000ULL

/* AMD64 Page Frame Number Mask (bits 12-51) */
#define PTE_PFN_MASK        0x000FFFFFFFFFF000ULL
#define PTE_PFN_SHIFT       12

/* Protection combinations */
#define PTE_READONLY        (PTE_VALID | PTE_NX)
#define PTE_READWRITE       (PTE_VALID | PTE_WRITE | PTE_NX)
#define PTE_EXECUTE         (PTE_VALID)
#define PTE_EXECUTE_READ    (PTE_VALID)
#define PTE_EXECUTE_READWRITE (PTE_VALID | PTE_WRITE)
#define PTE_WRITECOPY       (PTE_VALID | PTE_WRITE | PTE_COPY | PTE_NX)
#define PTE_EXECUTE_WRITECOPY (PTE_VALID | PTE_WRITE | PTE_COPY)

/* Cache control */
#define PTE_ENABLE_CACHE    0
#define PTE_DISABLE_CACHE   (PTE_CACHEDISABLE | PTE_WRITETHROUGH)
#define PTE_WRITECOMBINED_CACHE PTE_WRITETHROUGH

/* GLOBALS *******************************************************************/

/* Windows Protection to AMD64 PTE Mask Translation Table */
const ULONGLONG MmProtectToPteMask[32] = {
    /* 0x00 - PAGE_NOACCESS */
    0,
    /* 0x01 - PAGE_READONLY */
    PTE_READONLY | PTE_ENABLE_CACHE,
    /* 0x02 - PAGE_EXECUTE */
    PTE_EXECUTE | PTE_ENABLE_CACHE,
    /* 0x04 - PAGE_EXECUTE_READ */
    PTE_EXECUTE_READ | PTE_ENABLE_CACHE,
    /* 0x04 - PAGE_READWRITE */
    PTE_READWRITE | PTE_ENABLE_CACHE,
    /* 0x05 - PAGE_WRITECOPY */
    PTE_WRITECOPY | PTE_ENABLE_CACHE,
    /* 0x06 - PAGE_EXECUTE_READWRITE */
    PTE_EXECUTE_READWRITE | PTE_ENABLE_CACHE,
    /* 0x07 - PAGE_EXECUTE_WRITECOPY */
    PTE_EXECUTE_WRITECOPY | PTE_ENABLE_CACHE,
    /* 0x08 - PAGE_NOACCESS | PAGE_NOCACHE */
    0,
    /* 0x09 - PAGE_READONLY | PAGE_NOCACHE */
    PTE_READONLY | PTE_DISABLE_CACHE,
    /* 0x0A - PAGE_EXECUTE | PAGE_NOCACHE */
    PTE_EXECUTE | PTE_DISABLE_CACHE,
    /* 0x0B - PAGE_EXECUTE_READ | PAGE_NOCACHE */
    PTE_EXECUTE_READ | PTE_DISABLE_CACHE,
    /* 0x0C - PAGE_READWRITE | PAGE_NOCACHE */
    PTE_READWRITE | PTE_DISABLE_CACHE,
    /* 0x0D - PAGE_WRITECOPY | PAGE_NOCACHE */
    PTE_WRITECOPY | PTE_DISABLE_CACHE,
    /* 0x0E - PAGE_EXECUTE_READWRITE | PAGE_NOCACHE */
    PTE_EXECUTE_READWRITE | PTE_DISABLE_CACHE,
    /* 0x0F - PAGE_EXECUTE_WRITECOPY | PAGE_NOCACHE */
    PTE_EXECUTE_WRITECOPY | PTE_DISABLE_CACHE,
    /* 0x10 - PAGE_NOACCESS | PAGE_GUARD */
    0,
    /* 0x11 - PAGE_READONLY | PAGE_GUARD */
    PTE_READONLY | PTE_ENABLE_CACHE,
    /* 0x12 - PAGE_EXECUTE | PAGE_GUARD */
    PTE_EXECUTE | PTE_ENABLE_CACHE,
    /* 0x13 - PAGE_EXECUTE_READ | PAGE_GUARD */
    PTE_EXECUTE_READ | PTE_ENABLE_CACHE,
    /* 0x14 - PAGE_READWRITE | PAGE_GUARD */
    PTE_READWRITE | PTE_ENABLE_CACHE,
    /* 0x15 - PAGE_WRITECOPY | PAGE_GUARD */
    PTE_WRITECOPY | PTE_ENABLE_CACHE,
    /* 0x16 - PAGE_EXECUTE_READWRITE | PAGE_GUARD */
    PTE_EXECUTE_READWRITE | PTE_ENABLE_CACHE,
    /* 0x17 - PAGE_EXECUTE_WRITECOPY | PAGE_GUARD */
    PTE_EXECUTE_WRITECOPY | PTE_ENABLE_CACHE,
    /* 0x18 - PAGE_NOACCESS | PAGE_WRITECOMBINE */
    0,
    /* 0x19 - PAGE_READONLY | PAGE_WRITECOMBINE */
    PTE_READONLY | PTE_WRITECOMBINED_CACHE,
    /* 0x1A - PAGE_EXECUTE | PAGE_WRITECOMBINE */
    PTE_EXECUTE | PTE_WRITECOMBINED_CACHE,
    /* 0x1B - PAGE_EXECUTE_READ | PAGE_WRITECOMBINE */
    PTE_EXECUTE_READ | PTE_WRITECOMBINED_CACHE,
    /* 0x1C - PAGE_READWRITE | PAGE_WRITECOMBINE */
    PTE_READWRITE | PTE_WRITECOMBINED_CACHE,
    /* 0x1D - PAGE_WRITECOPY | PAGE_WRITECOMBINE */
    PTE_WRITECOPY | PTE_WRITECOMBINED_CACHE,
    /* 0x1E - PAGE_EXECUTE_READWRITE | PAGE_WRITECOMBINE */
    PTE_EXECUTE_READWRITE | PTE_WRITECOMBINED_CACHE,
    /* 0x1F - PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOMBINE */
    PTE_EXECUTE_WRITECOPY | PTE_WRITECOMBINED_CACHE
};

/* AMD64 PTE to Windows Protection Translation Table */
const ULONG MmProtectToValue[32] = {
    PAGE_NOACCESS,
    PAGE_READONLY,
    PAGE_EXECUTE,
    PAGE_EXECUTE_READ,
    PAGE_READWRITE,
    PAGE_WRITECOPY,
    PAGE_EXECUTE_READWRITE,
    PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_NOCACHE | PAGE_READONLY,
    PAGE_NOCACHE | PAGE_EXECUTE,
    PAGE_NOCACHE | PAGE_EXECUTE_READ,
    PAGE_NOCACHE | PAGE_READWRITE,
    PAGE_NOCACHE | PAGE_WRITECOPY,
    PAGE_NOCACHE | PAGE_EXECUTE_READWRITE,
    PAGE_NOCACHE | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_GUARD | PAGE_READONLY,
    PAGE_GUARD | PAGE_EXECUTE,
    PAGE_GUARD | PAGE_EXECUTE_READ,
    PAGE_GUARD | PAGE_READWRITE,
    PAGE_GUARD | PAGE_WRITECOPY,
    PAGE_GUARD | PAGE_EXECUTE_READWRITE,
    PAGE_GUARD | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_WRITECOMBINE | PAGE_READONLY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READ,
    PAGE_WRITECOMBINE | PAGE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_WRITECOPY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_WRITECOPY
};

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Validates if an address is canonical (bits 48-63 match bit 47)
 * @param Address - Virtual address to validate
 * @return TRUE if canonical, FALSE otherwise
 */
static BOOLEAN
MiIsCanonicalAddress(PVOID Address)
{
    ULONG_PTR Va = (ULONG_PTR)Address;
    ULONG_PTR SignExtend = (Va & 0x0000800000000000ULL) ? 0xFFFF000000000000ULL : 0;
    return ((Va & 0xFFFF000000000000ULL) == SignExtend);
}

/*
 * @brief Gets the PML4 entry for a given address
 * @param Address - Virtual address
 * @return Pointer to PML4 entry
 */
PMMPTE NTAPI
MiGetPml4Entry(PVOID Address)
{
    ULONG_PTR Index = ((ULONG_PTR)Address >> 39) & 0x1FF;
    PMMPTE Pml4Base = (PMMPTE)__readcr3();
    return &Pml4Base[Index];
}

/*
 * @brief Gets the PDPT entry for a given address
 * @param Address - Virtual address
 * @return Pointer to PDPT entry, NULL if not mapped
 */
PMMPTE NTAPI
MiGetPdptEntry(PVOID Address)
{
    PMMPTE Pml4e = MiGetPml4Entry(Address);
    if (!(Pml4e->u.Hard.Valid))
        return NULL;

    PMMPTE PdptBase = (PMMPTE)(Pml4e->u.Hard.PageFrameNumber << PAGE_SHIFT);
    ULONG_PTR Index = ((ULONG_PTR)Address >> 30) & 0x1FF;
    return &PdptBase[Index];
}

/*
 * @brief Gets the PD entry for a given address
 * @param Address - Virtual address
 * @return Pointer to PD entry, NULL if not mapped
 */
PMMPTE NTAPI
MiGetPdeEntry(PVOID Address)
{
    PMMPTE Pdpte = MiGetPdptEntry(Address);
    if (!Pdpte || !(Pdpte->u.Hard.Valid))
        return NULL;

    PMMPTE PdBase = (PMMPTE)(Pdpte->u.Hard.PageFrameNumber << PAGE_SHIFT);
    ULONG_PTR Index = ((ULONG_PTR)Address >> 21) & 0x1FF;
    return &PdBase[Index];
}

/*
 * @brief Gets the PT entry for a given address
 * @param Address - Virtual address
 * @return Pointer to PT entry, NULL if not mapped
 */
PMMPTE NTAPI
MiGetPteEntry(PVOID Address)
{
    PMMPTE Pde = MiGetPdeEntry(Address);
    if (!Pde || !(Pde->u.Hard.Valid))
        return NULL;

    /* Check for large page (2MB) */
    if (Pde->u.Hard.LargePage)
        return Pde;  /* PDE is the final entry for 2MB pages */

    PMMPTE PtBase = (PMMPTE)(Pde->u.Hard.PageFrameNumber << PAGE_SHIFT);
    ULONG_PTR Index = ((ULONG_PTR)Address >> 12) & 0x1FF;
    return &PtBase[Index];
}

/*
 * @brief Gets the PDE address for a given virtual address
 * @param Address - Virtual address
 * @return Pointer to PDE address
 */
PMMPTE NTAPI
MiGetPdeAddress(PVOID Address)
{
    /* For compatibility, just return the PDE entry */
    return MiGetPdeEntry(Address);
}

/*
 * @brief Ensures all page table levels exist for an address
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address
 * @return Pointer to PTE, NULL on failure
 */
PMMPTE NTAPI
MiGetPteForAddress(PEPROCESS Process, PVOID Address)
{
    PMMPTE Pml4e, Pdpte, Pde, Pte;
    MMPTE TempPte;
    PFN_NUMBER NewPage;

    /* Validate canonical address */
    if (!MiIsCanonicalAddress(Address))
    {
        DPRINT1("Non-canonical address: %p\n", Address);
        return NULL;
    }

    /* Get or create PML4 entry */
    Pml4e = MiGetPml4Entry(Address);
    if (!Pml4e->u.Hard.Valid)
    {
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

    /* Get or create PDPT entry */
    Pdpte = MiGetPdptEntry(Address);
    if (!Pdpte->u.Hard.Valid)
    {
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

    /* Get or create PD entry */
    Pde = MiGetPdeEntry(Address);
    if (!Pde->u.Hard.Valid)
    {
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

    /* Return PTE */
    return MiGetPteEntry(Address);
}

/* LARGE PAGE SUPPORT ********************************************************/

/*
 * @implemented
 * @brief Creates a large page mapping (2MB or 1GB)
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to map (must be aligned)
 * @param Size - Size of mapping (2MB or 1GB)
 * @param flProtect - Protection flags
 * @param Page - Physical page number to map
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreateLargePageMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG flProtect,
    _In_ PFN_NUMBER Page)
{
    PMMPTE Pte = NULL;
    MMPTE TempPte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    ULONG ProtectionMask;

    DPRINT("MmCreateLargePageMapping(%p, %p, %lx, %lu, %lx)\n",
           Process, Address, Size, flProtect, Page);

    /* Validate size */
    if (Size == PAGE_SIZE_2MB)
    {
        /* 2MB page - check alignment */
        if ((ULONG_PTR)Address & (PAGE_SIZE_2MB - 1))
        {
            DPRINT1("Address %p not aligned to 2MB\n", Address);
            return STATUS_INVALID_PARAMETER;
        }
        if (Page & ((PAGE_SIZE_2MB / PAGE_SIZE) - 1))
        {
            DPRINT1("PFN %lx not aligned for 2MB page\n", Page);
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (Size == PAGE_SIZE_1GB)
    {
        /* 1GB page - check alignment and CPU support */
        if (!(KeFeatureBits & KF_LARGE_PAGE))
        {
            DPRINT1("CPU does not support 1GB pages\n");
            return STATUS_NOT_SUPPORTED;
        }
        if ((ULONG_PTR)Address & (PAGE_SIZE_1GB - 1))
        {
            DPRINT1("Address %p not aligned to 1GB\n", Address);
            return STATUS_INVALID_PARAMETER;
        }
        if (Page & ((PAGE_SIZE_1GB / PAGE_SIZE) - 1))
        {
            DPRINT1("PFN %lx not aligned for 1GB page\n", Page);
            return STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        DPRINT1("Invalid size %lx for large page\n", Size);
        return STATUS_INVALID_PARAMETER;
    }

    /* Get protection mask */
    ProtectionMask = MiMakeProtectionMask(flProtect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("Invalid protection %lx\n", flProtect);
        return STATUS_INVALID_PARAMETER;
    }

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    if (Size == PAGE_SIZE_2MB)
    {
        /* Get PDE for 2MB page */
        Pte = MiGetPdeEntry(Address);
        if (!Pte)
        {
            /* Need to create page tables up to PD level */
            MiGetPteForAddress(Process, Address);
            Pte = MiGetPdeEntry(Address);
        }

        if (!Pte)
        {
            if (ProcessAttached)
                KeUnstackDetachProcess(&ApcState);
            return STATUS_NO_MEMORY;
        }

        /* Build 2MB PDE */
        TempPte.u.Long = MmProtectToPteMask[ProtectionMask];
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.LargePage = 1;  /* PS bit for 2MB page */
        TempPte.u.Hard.PageFrameNumber = Page;
        TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
        TempPte.u.Hard.Global = (Address >= MmSystemRangeStart) ? 1 : 0;

        /* Write PDE */
        *Pte = TempPte;
    }
    else /* 1GB page */
    {
        /* Get PDPTE for 1GB page */
        Pte = MiGetPdptEntry(Address);
        if (!Pte)
        {
            /* Need to create page tables up to PDPT level */
            PMMPTE Pml4e = MiGetPml4Entry(Address);
            if (!Pml4e->u.Hard.Valid)
            {
                /* Allocate PDPT */
                PFN_NUMBER NewPage = MmAllocPage(MC_SYSTEM);
                if (!NewPage)
                {
                    if (ProcessAttached)
                        KeUnstackDetachProcess(&ApcState);
                    return STATUS_NO_MEMORY;
                }

                RtlZeroMemory(MiPfnToSystemAddress(NewPage), PAGE_SIZE);

                TempPte.u.Long = 0;
                TempPte.u.Hard.Valid = 1;
                TempPte.u.Hard.Write = 1;
                TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
                TempPte.u.Hard.PageFrameNumber = NewPage;
                *Pml4e = TempPte;
            }
            Pte = MiGetPdptEntry(Address);
        }

        /* Build 1GB PDPTE */
        TempPte.u.Long = MmProtectToPteMask[ProtectionMask];
        TempPte.u.Hard.Valid = 1;
        TempPte.u.Hard.LargePage = 1;  /* PS bit for 1GB page */
        TempPte.u.Hard.PageFrameNumber = Page;
        TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;
        TempPte.u.Hard.Global = (Address >= MmSystemRangeStart) ? 1 : 0;

        /* Write PDPTE */
        *Pte = TempPte;
    }

    /* Flush TLB for the range */
    if (Size == PAGE_SIZE_2MB)
    {
        /* Flush 2MB range */
        MiFlushTlbRange(Address, PAGE_SIZE_2MB / PAGE_SIZE);
    }
    else
    {
        /* Flush 1GB range (more expensive) */
        MiFlushEntireTlb();
    }

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    DPRINT("Large page mapping created: %p, size=%lx\n", Address, Size);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Checks if large pages are supported
 * @param Size - Size to check (2MB or 1GB)
 * @return TRUE if supported, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIsLargePageSupported(
    _In_ SIZE_T Size)
{
    if (Size == PAGE_SIZE_2MB)
    {
        /* 2MB pages always supported on AMD64 */
        return TRUE;
    }
    else if (Size == PAGE_SIZE_1GB)
    {
        /* Check CPU feature */
        return (KeFeatureBits & KF_LARGE_PAGE) != 0;
    }
    return FALSE;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Gets the physical frame number for a virtual address in a process
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to query
 * @return Physical frame number, 0 if not mapped
 */
PFN_NUMBER
NTAPI
MmGetPfnForProcess(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE Pte;
    PFN_NUMBER Pfn = 0;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    /* Validate address */
    if (!MiIsCanonicalAddress(Address))
        return 0;

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE */
    Pte = MiGetPteEntry(Address);
    if (Pte && Pte->u.Hard.Valid)
    {
        Pfn = Pte->u.Hard.PageFrameNumber;
    }

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return Pfn;
}

/*
 * @implemented
 * @brief Creates a virtual to physical mapping
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to map
 * @param flProtect - Protection flags
 * @param Page - Physical page number to map
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreateVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect,
    _In_ PFN_NUMBER Page)
{
    PMMPTE Pte;
    MMPTE TempPte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    ULONG ProtectionMask;

    DPRINT("MmCreateVirtualMapping(%p, %p, %lu, %lx)\n",
           Process, Address, flProtect, Page);

    /* Validate parameters */
    ASSERT(((ULONG_PTR)Address % PAGE_SIZE) == 0);
    if (!MmIsPageInUse(Page))
    {
        DPRINT1("Page %lx is not in use\n", Page);
        return STATUS_INVALID_PARAMETER;
    }

    /* Get protection mask */
    ProtectionMask = MiMakeProtectionMask(flProtect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("Invalid protection %lx\n", flProtect);
        return STATUS_INVALID_PARAMETER;
    }

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get or create PTE */
    Pte = MiGetPteForAddress(Process, Address);
    if (!Pte)
    {
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);
        return STATUS_NO_MEMORY;
    }

    /* Check if already mapped */
    if (Pte->u.Hard.Valid)
    {
        DPRINT1("Address %p already mapped\n", Address);
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    /* Build PTE */
    TempPte.u.Long = MmProtectToPteMask[ProtectionMask];
    TempPte.u.Hard.Valid = 1;
    TempPte.u.Hard.PageFrameNumber = Page;
    TempPte.u.Hard.Owner = (Process != NULL) ? 1 : 0;

    /* Write PTE */
    *Pte = TempPte;

    /* Flush TLB for this page */
    __invlpg(Address);

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Creates a virtual mapping without validation (unsafe)
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to map
 * @param flProtect - Protection flags
 * @param Page - Physical page number to map
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreateVirtualMappingUnsafe(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect,
    _In_ PFN_NUMBER Page)
{
    /* For now, just call the safe version */
    /* TODO: Optimize for performance-critical paths */
    return MmCreateVirtualMapping(Process, Address, flProtect, Page);
}

/*
 * @implemented
 * @brief Creates a physical mapping with special attributes
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to map
 * @param flProtect - Protection flags
 * @param Page - Physical page number to map
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreatePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect,
    _In_ PFN_NUMBER Page)
{
    /* Physical mappings typically disable caching */
    ULONG AdjustedProtect = flProtect | PAGE_NOCACHE;
    return MmCreateVirtualMapping(Process, Address, AdjustedProtect, Page);
}

/*
 * @implemented
 * @brief Deletes a virtual mapping
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to unmap
 * @param WasDirty - Returns if page was dirty
 * @param Page - Returns the physical page that was mapped
 * @return STATUS_SUCCESS or error code
 */
BOOLEAN
NTAPI
MmDeleteVirtualMapping(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    PMMPTE Pte;
    MMPTE OldPte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    DPRINT("MmDeleteVirtualMapping(%p, %p, %p, %p)\n",
           Process, Address, WasDirty, Page);

    /* Validate parameters */
    ASSERT(((ULONG_PTR)Address % PAGE_SIZE) == 0);

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE */
    Pte = MiGetPteEntry(Address);
    if (!Pte || !Pte->u.Hard.Valid)
    {
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);

        if (WasDirty) *WasDirty = FALSE;
        if (Page) *Page = 0;
        return FALSE;
    }

    /* Save old PTE */
    OldPte = *Pte;

    /* Clear PTE */
    Pte->u.Long = 0;

    /* Flush TLB */
    __invlpg(Address);

    /* Return information */
    if (WasDirty)
        *WasDirty = OldPte.u.Hard.Dirty ? TRUE : FALSE;

    if (Page)
        *Page = OldPte.u.Hard.PageFrameNumber;

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return TRUE;
}

/*
 * @implemented
 * @brief Deletes a physical mapping
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to unmap
 * @param WasDirty - Returns if page was dirty
 * @param Page - Returns the physical page that was mapped
 * @return STATUS_SUCCESS or error code
 */
BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    /* Same as regular delete for AMD64 */
    return MmDeleteVirtualMapping(Process, Address, WasDirty, Page);
}

/*
 * @implemented
 * @brief Checks if a page is present (mapped)
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to check
 * @return TRUE if mapped, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIsPagePresent(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE Pte;
    BOOLEAN Present = FALSE;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE and check if valid */
    Pte = MiGetPteEntry(Address);
    if (Pte && Pte->u.Hard.Valid)
    {
        Present = TRUE;
    }

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return Present;
}

/*
 * @implemented
 * @brief Checks if a page is swapped to pagefile
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to check
 * @return TRUE if swapped, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIsPageSwapped(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE Pte;
    BOOLEAN Swapped = FALSE;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE */
    Pte = MiGetPteEntry(Address);
    if (Pte && !Pte->u.Hard.Valid)
    {
        /* Check for transition or pagefile PTE */
        if (Pte->u.Trans.Transition || Pte->u.Soft.PageFileHigh)
        {
            Swapped = TRUE;
        }
    }

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return Swapped;
}

/*
 * @implemented
 * @brief Gets the protection flags for a page
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to query
 * @return Windows protection flags
 */
ULONG
NTAPI
MmGetPageProtect(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE Pte;
    ULONG Protect = PAGE_NOACCESS;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    ULONGLONG PteValue;
    ULONG i;

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE */
    Pte = MiGetPteEntry(Address);
    if (Pte && Pte->u.Hard.Valid)
    {
        /* Extract protection bits */
        PteValue = Pte->u.Long & (PTE_WRITE | PTE_NX | PTE_CACHEDISABLE | PTE_WRITETHROUGH);

        /* Find matching protection */
        for (i = 0; i < 32; i++)
        {
            if ((MmProtectToPteMask[i] & (PTE_WRITE | PTE_NX | PTE_CACHEDISABLE | PTE_WRITETHROUGH)) == PteValue)
            {
                Protect = MmProtectToValue[i];
                break;
            }
        }
    }

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return Protect;
}

/*
 * @implemented
 * @brief Sets the protection flags for a page
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to modify
 * @param flProtect - New protection flags
 * @return STATUS_SUCCESS or error code
 */
VOID
NTAPI
MmSetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect)
{
    PMMPTE Pte;
    MMPTE OldPte, NewPte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;
    ULONG ProtectionMask;

    DPRINT("MmSetPageProtect(Process %p, Address %p, flProtect %x)\n",
           Process, Address, flProtect);

    /* Get protection mask */
    ProtectionMask = MiMakeProtectionMask(flProtect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("Invalid protection %lx\n", flProtect);
        return;
    }

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get PTE */
    Pte = MiGetPteEntry(Address);
    if (!Pte || !Pte->u.Hard.Valid)
    {
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);
        return;
    }

    /* Save old PTE */
    OldPte = *Pte;

    /* Build new PTE with updated protection */
    NewPte = OldPte;
    NewPte.u.Long &= ~(PTE_WRITE | PTE_NX | PTE_CACHEDISABLE | PTE_WRITETHROUGH);
    NewPte.u.Long |= MmProtectToPteMask[ProtectionMask] & (PTE_WRITE | PTE_NX | PTE_CACHEDISABLE | PTE_WRITETHROUGH);

    /* Write new PTE */
    *Pte = NewPte;

    /* Flush TLB */
    __invlpg(Address);

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

}

/*
 * @implemented
 * @brief Creates a pagefile-backed mapping
 * @param Process - Process context (NULL for kernel)
 * @param Address - Virtual address to create mapping
 * @param SwapEntry - Swap entry information
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreatePageFileMapping(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ SWAPENTRY SwapEntry)
{
    PMMPTE Pte;
    MMPTE TempPte;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    DPRINT("MmCreatePageFileMapping(%p, %p, %lu)\n",
           Process, Address, SwapEntry);

    /* Validate parameters */
    ASSERT(((ULONG_PTR)Address % PAGE_SIZE) == 0);

    /* Attach to process if needed */
    if (Process && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Get or create PTE */
    Pte = MiGetPteForAddress(Process, Address);
    if (!Pte)
    {
        if (ProcessAttached)
            KeUnstackDetachProcess(&ApcState);
        return STATUS_NO_MEMORY;
    }

    /* Build pagefile PTE */
    TempPte.u.Long = 0;
    TempPte.u.Soft.PageFileHigh = SwapEntry;
    TempPte.u.Soft.Valid = 0;
    TempPte.u.Soft.Prototype = 0;
    TempPte.u.Soft.Transition = 0;

    /* Write PTE */
    *Pte = TempPte;

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Creates a new process address space
 * @param Process - Process to create address space for
 * @return STATUS_SUCCESS or error code
 */
BOOLEAN
NTAPI
MmCreateProcessAddressSpace_OLD(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    PFN_NUMBER Pml4Page;
    PMMPTE Pml4;
    PMMPTE CurrentPml4;
    ULONG i;

    DPRINT("MmCreateProcessAddressSpace(%p)\n", Process);

    /* Allocate PML4 page */
    Pml4Page = MmAllocPage(MC_SYSTEM);
    if (!Pml4Page)
    {
        DPRINT1("Failed to allocate PML4 page\n");
        return FALSE;
    }

    /* Map PML4 page */
    Pml4 = MiPfnToSystemAddress(Pml4Page);
    RtlZeroMemory(Pml4, PAGE_SIZE);

    /* Get current PML4 */
    CurrentPml4 = (PMMPTE)(__readcr3() & ~0xFFF);

    /* Copy kernel entries (upper half of address space) */
    for (i = 256; i < 512; i++)
    {
        Pml4[i] = CurrentPml4[i];
    }

    /* Set the self-mapping entry */
    Pml4[PML4_SELF_MAP_INDEX].u.Long = 0;
    Pml4[PML4_SELF_MAP_INDEX].u.Hard.Valid = 1;
    Pml4[PML4_SELF_MAP_INDEX].u.Hard.Write = 1;
    Pml4[PML4_SELF_MAP_INDEX].u.Hard.PageFrameNumber = Pml4Page;

    /* Store PML4 physical address in process */
    DirectoryTableBase[0] = Pml4Page << PAGE_SHIFT;
    DirectoryTableBase[1] = 0;
    Process->Pcb.DirectoryTableBase[0] = DirectoryTableBase[0];
    Process->Pcb.DirectoryTableBase[1] = DirectoryTableBase[1];

    return TRUE;
}

/*
 * @implemented
 * @brief Deletes a process address space
 * @param Process - Process whose address space to delete
 * @return STATUS_SUCCESS or error code
 */
VOID
NTAPI
MmDeleteProcessAddressSpace_OLD(
    _In_ PEPROCESS Process)
{
    PFN_NUMBER Pml4Page;
    PMMPTE Pml4;
    ULONG i, j, k;
    PMMPTE Pdpt, Pd, Pt;

    DPRINT("MmDeleteProcessAddressSpace(%p)\n", Process);

    /* Get PML4 page */
    Pml4Page = Process->Pcb.DirectoryTableBase[0] >> PAGE_SHIFT;
    if (!Pml4Page)
        return;

    /* Map PML4 */
    Pml4 = MiPfnToSystemAddress(Pml4Page);

    /* Free user-space entries (lower half) */
    for (i = 0; i < 256; i++)
    {
        if (!Pml4[i].u.Hard.Valid)
            continue;

        /* Map PDPT */
        Pdpt = MiPfnToSystemAddress(Pml4[i].u.Hard.PageFrameNumber);

        for (j = 0; j < 512; j++)
        {
            if (!Pdpt[j].u.Hard.Valid)
                continue;

            /* Check for 1GB page */
            if (Pdpt[j].u.Hard.LargePage)
            {
                /* Free 1GB page - release the huge page */
                PFN_NUMBER HugePage = Pdpt[j].u.Hard.PageFrameNumber;
                ULONG PageCount = PAGE_SIZE_1GB / PAGE_SIZE;
                while (PageCount--)
                {
                    MmReleasePageMemoryConsumer(MC_SYSTEM, HugePage++);
                }
                continue;
            }

            /* Map PD */
            Pd = MiPfnToSystemAddress(Pdpt[j].u.Hard.PageFrameNumber);

            for (k = 0; k < 512; k++)
            {
                if (!Pd[k].u.Hard.Valid)
                    continue;

                /* Check for 2MB page */
                if (Pd[k].u.Hard.LargePage)
                {
                    /* Free 2MB page - release the large page */
                    PFN_NUMBER LargePage = Pd[k].u.Hard.PageFrameNumber;
                    ULONG PageCount = PAGE_SIZE_2MB / PAGE_SIZE;
                    while (PageCount--)
                    {
                        MmReleasePageMemoryConsumer(MC_SYSTEM, LargePage++);
                    }
                    continue;
                }

                /* Free PT */
                MmReleasePageMemoryConsumer(MC_SYSTEM, Pd[k].u.Hard.PageFrameNumber);
            }

            /* Free PD */
            MmReleasePageMemoryConsumer(MC_SYSTEM, Pdpt[j].u.Hard.PageFrameNumber);
        }

        /* Free PDPT */
        MmReleasePageMemoryConsumer(MC_SYSTEM, Pml4[i].u.Hard.PageFrameNumber);
    }

    /* Free PML4 */
    MmReleasePageMemoryConsumer(MC_SYSTEM, Pml4Page);

    /* Clear directory base */
    Process->Pcb.DirectoryTableBase[0] = 0;

}

/* END OF FILE */