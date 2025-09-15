/*
 * AMD64 Memory Management Internal Header
 */

#ifndef _AMD64_MM_H
#define _AMD64_MM_H

/* Page sizes */
#define PAGE_SIZE_4KB   0x1000
#define PAGE_SIZE_2MB   0x200000
#define PAGE_SIZE_1GB   0x40000000ULL

/* PML4 self-mapping index */
#define PML4_SELF_MAP_INDEX 0x1ED

/* Page table access macros - these return indices, not pointers */
#define MiGetPml4Index(Address) (((ULONG_PTR)(Address) >> 39) & 0x1FF)
#define MiGetPdptIndex(Address) (((ULONG_PTR)(Address) >> 30) & 0x1FF)
#define MiGetPdeIndex(Address)  (((ULONG_PTR)(Address) >> 21) & 0x1FF)
#define MiGetPteIndex(Address)  (((ULONG_PTR)(Address) >> 12) & 0x1FF)

/* Function declarations for page table access - implemented in page.c */
PMMPTE NTAPI MiGetPml4Entry(PVOID Address);
PMMPTE NTAPI MiGetPdptEntry(PVOID Address);
PMMPTE NTAPI MiGetPdeEntry(PVOID Address);
PMMPTE NTAPI MiGetPteEntry(PVOID Address);
PMMPTE NTAPI MiGetPdeAddress(PVOID Address);

/* Helper macros */
#define MiPfnToSystemAddress(Pfn) ((PVOID)((ULONG_PTR)(Pfn) << PAGE_SHIFT))

/* Function declarations */
VOID NTAPI MiFlushTlbRange(_In_ PVOID Address, _In_ ULONG NumberOfPages);
VOID NTAPI MiFlushEntireTlb(VOID);
VOID NTAPI MiFlushTlb(_In_ PVOID Address);

/* Correct function signatures matching ReactOS conventions */
BOOLEAN
NTAPI
MmDeleteVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page);

BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page);

VOID
NTAPI
MmSetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect);

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase);

#endif /* _AMD64_MM_H */