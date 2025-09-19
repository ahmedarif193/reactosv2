#ifndef _NTOSKRNL_INCLUDE_INTERNAL_ARM64_MM_H
#define _NTOSKRNL_INCLUDE_INTERNAL_ARM64_MM_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ARM64 Memory Management Definitions for ReactOS Kernel */

/* ARM64 Page Sizes */
#define PAGE_SIZE               0x1000      /* 4KB */
/* PAGE_SHIFT already defined in mmtypes.h, don't redefine */
#ifndef PAGE_SHIFT
#define PAGE_SHIFT              12
#endif
#define PAGES_PER_LARGE_PAGE    512         /* 2MB / 4KB */
#define LARGE_PAGE_SIZE         0x200000    /* 2MB */
#define LARGE_PAGE_SHIFT        21

/* MM Pool and Zero constants - similar to ARM */
#define MI_ZERO_PTES                           32
#define MI_MAX_ZERO_BITS                       21
#define SESSION_POOL_LOOKASIDES                 26

/* ARM64 page table constants */
#define PPE_PER_PAGE                           512  /* Page table entries per page */
#define PTE_PER_PAGE                           512  /* Page table entries per page */
#define PDE_PER_PAGE                           512  /* Page directory entries per page */
#define PDE_TOP                                0x1FFULL  /* Top-level PDE mask */

/* MM Macros for ARM64 */
#define MiAddressToPde(x)                      ((PMMPDE)(((ULONG_PTR)(x) >> 21) & ~0xFFF))
#define MI_MAKE_ACCESSED_PAGE(x)               do { (x)->u.Long |= 0x20; } while(0)  /* Set accessed bit */
#define MI_MAKE_OWNER_PAGE(x)                  do { (x)->u.Long |= 0x40; } while(0)  /* Set owner bit */

/* ARM64 compatibility macros for MMPTE_PROTOTYPE */
/* On ARM64, ProtoAddress is a single 48-bit field, not split into Low/High */
#define ProtoAddressLow ProtoAddress   /* Map ProtoAddressLow to ProtoAddress */
#define ProtoAddressHigh ProtoAddress  /* Map ProtoAddressHigh to ProtoAddress */

/* ARM64 Virtual Address Space Layout */
#define KERNEL_BASE             0xFFFF800000000000ULL
#define HYPERSPACE_BASE         0xFFFF900000000000ULL
#define SYSTEM_SPACE_BASE       0xFFFFA00000000000ULL
#define HAL_BASE                0xFFFFC00000000000ULL
#define USER_SPACE_END          0x00007FFFFFFFFFFFULL

/* ARM64 Page Table Levels */
#define ARM64_PT_LEVELS         4
#define ARM64_PT_INDEX_BITS     9
#define ARM64_PT_ENTRIES        512

/* ARM64 Page Table Entry Flags */
#define ARM64_PTE_VALID         0x0000000000000001ULL  /* Entry is valid */
#define ARM64_PTE_TYPE_MASK     0x0000000000000002ULL  
#define ARM64_PTE_TYPE_BLOCK    0x0000000000000000ULL  /* Block/Page descriptor */
#define ARM64_PTE_TYPE_TABLE    0x0000000000000002ULL  /* Table descriptor */

/* Memory Attributes */
#define ARM64_PTE_ATTR_MASK     0x00000000000000FCULL
#define ARM64_PTE_ATTR_NORMAL   0x0000000000000000ULL  /* Normal memory, Inner/Outer WB */
#define ARM64_PTE_ATTR_DEVICE   0x0000000000000004ULL  /* Device memory */
#define ARM64_PTE_ATTR_NC       0x0000000000000008ULL  /* Non-cacheable */

/* Access Permissions */
#define ARM64_PTE_AP_MASK       0x00000000000000C0ULL
#define ARM64_PTE_AP_KERNEL_RW  0x0000000000000000ULL  /* Kernel R/W, User no access */
#define ARM64_PTE_AP_RW         0x0000000000000040ULL  /* Kernel/User R/W */
#define ARM64_PTE_AP_KERNEL_RO  0x0000000000000080ULL  /* Kernel R/O, User no access */
#define ARM64_PTE_AP_RO         0x00000000000000C0ULL  /* Kernel/User R/O */

/* Shareability */
#define ARM64_PTE_SH_MASK       0x0000000000000300ULL
#define ARM64_PTE_SH_NONE       0x0000000000000000ULL  /* Non-shareable */
#define ARM64_PTE_SH_OUTER      0x0000000000000200ULL  /* Outer Shareable */
#define ARM64_PTE_SH_INNER      0x0000000000000300ULL  /* Inner Shareable */

/* Access Flag and Dirty Bit */
#define ARM64_PTE_AF            0x0000000000000400ULL  /* Access Flag */
#define ARM64_PTE_DIRTY         0x0000000000000800ULL  /* Dirty bit (custom) */

/* Execute permissions */
#define ARM64_PTE_PXN           0x0020000000000000ULL  /* Privileged Execute Never */
#define ARM64_PTE_UXN           0x0040000000000000ULL  /* User Execute Never */

/* Software bits (bits 55-58) */
#define ARM64_PTE_SW_MASK       0x0F00000000000000ULL
#define ARM64_PTE_SW_WIRED      0x0100000000000000ULL  /* Wired page */
#define ARM64_PTE_SW_PROTOTYPE  0x0200000000000000ULL  /* Prototype PTE */
#define ARM64_PTE_SW_TRANSITION 0x0400000000000000ULL  /* Transition PTE */

/* Physical address mask (bits 47:12 for 4KB pages) */
#define ARM64_PTE_ADDR_MASK     0x0000FFFFFFFFF000ULL

/* ARM64 Page Table Macros */
#define ARM64_PTE_GET_PFN(pte)  (((pte) & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT)
#define ARM64_PTE_SET_PFN(pfn)  (((ULONGLONG)(pfn)) << PAGE_SHIFT)

/* Virtual Address Breakdown (48-bit VA) */
#define ARM64_VA_BITS           48
#define ARM64_VA_MASK           0x0000FFFFFFFFFFFFULL

/* Page table index extraction */
#define ARM64_L0_INDEX(va)      (((va) >> 39) & 0x1FF)  /* bits 47:39 */
#define ARM64_L1_INDEX(va)      (((va) >> 30) & 0x1FF)  /* bits 38:30 */
#define ARM64_L2_INDEX(va)      (((va) >> 21) & 0x1FF)  /* bits 29:21 */
#define ARM64_L3_INDEX(va)      (((va) >> 12) & 0x1FF)  /* bits 20:12 */

/* Memory types for MAIR_EL1 */
#define ARM64_MAIR_DEVICE_nGnRnE    0x00    /* Device-nGnRnE */
#define ARM64_MAIR_NORMAL_NC        0x44    /* Normal Non-cacheable */
#define ARM64_MAIR_NORMAL_WB        0xFF    /* Normal Inner/Outer Write-Back */

#define ARM64_MAIR_VALUE  ((ARM64_MAIR_DEVICE_nGnRnE << 0) | \
                           (ARM64_MAIR_NORMAL_NC << 8) | \
                           (ARM64_MAIR_NORMAL_WB << 16))

/* TCR_EL1 Configuration */
#define ARM64_TCR_T0SZ(x)       ((x) & 0x3F)           /* Size offset for TTBR0 */
#define ARM64_TCR_EPD0          (1ULL << 7)            /* Disable TTBR0 walks */
#define ARM64_TCR_IRGN0_NC      (0ULL << 8)            /* TTBR0 Inner Non-cacheable */
#define ARM64_TCR_IRGN0_WB      (1ULL << 8)            /* TTBR0 Inner Write-Back */
#define ARM64_TCR_ORGN0_NC      (0ULL << 10)           /* TTBR0 Outer Non-cacheable */
#define ARM64_TCR_ORGN0_WB      (1ULL << 10)           /* TTBR0 Outer Write-Back */
#define ARM64_TCR_SH0_NONE      (0ULL << 12)           /* TTBR0 Non-shareable */
#define ARM64_TCR_SH0_INNER     (3ULL << 12)           /* TTBR0 Inner Shareable */
#define ARM64_TCR_TG0_4K        (0ULL << 14)           /* TTBR0 4KB granule */
#define ARM64_TCR_T1SZ(x)       (((x) & 0x3F) << 16)   /* Size offset for TTBR1 */
#define ARM64_TCR_A1            (1ULL << 22)            /* ASID in TTBR1 */
#define ARM64_TCR_EPD1          (1ULL << 23)            /* Disable TTBR1 walks */
#define ARM64_TCR_IRGN1_WB      (1ULL << 24)           /* TTBR1 Inner Write-Back */
#define ARM64_TCR_ORGN1_WB      (1ULL << 26)           /* TTBR1 Outer Write-Back */
#define ARM64_TCR_SH1_INNER     (3ULL << 28)           /* TTBR1 Inner Shareable */
#define ARM64_TCR_TG1_4K        (2ULL << 30)           /* TTBR1 4KB granule */
#define ARM64_TCR_IPS_48BIT     (5ULL << 32)           /* 48-bit physical address */

/* Standard TCR configuration for ReactOS */
#define ARM64_TCR_DEFAULT \
    (ARM64_TCR_T0SZ(16) | ARM64_TCR_IRGN0_WB | ARM64_TCR_ORGN0_WB | \
     ARM64_TCR_SH0_INNER | ARM64_TCR_TG0_4K | \
     ARM64_TCR_T1SZ(16) | ARM64_TCR_IRGN1_WB | ARM64_TCR_ORGN1_WB | \
     ARM64_TCR_SH1_INNER | ARM64_TCR_TG1_4K | ARM64_TCR_IPS_48BIT)

/* SCTLR_EL1 Configuration */
#define ARM64_SCTLR_M           (1ULL << 0)             /* MMU enable */
#define ARM64_SCTLR_A           (1ULL << 1)             /* Alignment check */
#define ARM64_SCTLR_C           (1ULL << 2)             /* Data cache enable */
#define ARM64_SCTLR_SA          (1ULL << 3)             /* Stack Alignment */
#define ARM64_SCTLR_I           (1ULL << 12)            /* Instruction cache enable */
#define ARM64_SCTLR_WXN         (1ULL << 19)            /* Write implies XN */
#define ARM64_SCTLR_EE          (1ULL << 25)            /* Exception Endianness */

/* Standard SCTLR configuration for ReactOS */
#define ARM64_SCTLR_DEFAULT \
    (ARM64_SCTLR_M | ARM64_SCTLR_C | ARM64_SCTLR_I | ARM64_SCTLR_SA)

/* ARM64 Memory Management Function Prototypes */

/* Page Table Management */
VOID
NTAPI
MiInitializeProcessAddressSpace(
    IN PEPROCESS Process,
    IN PETHREAD Thread,
    IN PFN_NUMBER DirectoryTableBase,
    IN HANDLE ProcessHandle
);

/* MmCreateProcessAddressSpace - declared in generic headers */

/* ARM64 MM function declarations - many are declared in generic headers */

/* ARM64 Constants */
#define BYTES_TO_PAGES(Size)    (((Size) >> PAGE_SHIFT) + (((Size) & (PAGE_SIZE - 1)) != 0))
#define PAGES_TO_BYTES(Pages)   ((Pages) << PAGE_SHIFT)
/* ROUND_TO_PAGES already defined in wdm.h, use conditional */
#ifndef ROUND_TO_PAGES
#define ROUND_TO_PAGES(Size)    (((Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#endif

/* ARM64 Memory Layout Constants */
/* MM_SYSTEM_RANGE_START already defined in DDK headers */
#ifndef MM_HAL_VA_START
#define MM_HAL_VA_START                 ((PVOID)0xFFFFFFFFFFC00000ULL)
#endif
#define MM_HYPERSPACE_START             (PVOID)HYPERSPACE_BASE
#ifndef KSEG0_BASE
#define KSEG0_BASE                      0xFFFF800000000000ULL  /* Kernel segment 0 base */
#endif
#ifndef MM_KSEG0_BASE
#define MM_KSEG0_BASE                   KSEG0_BASE
#endif

/* MM_USER_PROBE_ADDRESS and MM_HIGHEST_USER_ADDRESS already defined in DDK headers */
#define MM_SYSTEM_SPACE_START           (PVOID)SYSTEM_SPACE_BASE

/* ARM64 Paging Macros */
#define MiGetPteAddress(va) \
    ((PMMPTE)(PTE_BASE + (((ULONG_PTR)(va) >> 12) << 3)))

#define MiAddressToPte(va) MiGetPteAddress(va)  /* Alias for compatibility */

#define MiGetPdeAddress(va) \
    ((PMMPTE)(PDE_BASE + (((ULONG_PTR)(va) >> 21) << 3)))

#define MiGetPpeAddress(va) \
    ((PMMPTE)(PPE_BASE + (((ULONG_PTR)(va) >> 30) << 3)))

#define MiGetPxeAddress(va) \
    ((PMMPTE)(PXE_BASE + (((ULONG_PTR)(va) >> 39) << 3)))

/* Convert PTE to address */
#define MiPteToAddress(pte) \
    ((PVOID)((ULONG_PTR)(((ULONG_PTR)(pte) - PTE_BASE) >> 3) << 12))

/* ARM64 PTE Base Addresses */
#define PTE_BASE    0xFFFF000000000000ULL
#define PDE_BASE    0xFFFF000080000000ULL
#define PPE_BASE    0xFFFF000080400000ULL
#define PXE_BASE    0xFFFF000080404000ULL
#define PXE_SELFMAP 0xFFFF000080404000ULL  /* Self-mapping address for PXE */

/* PFN extraction macros */
#define PFN_FROM_PTE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PDE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PPE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PXE(v) ((v)->u.Hard.PageFrameNumber)

/* Cache control macros for ARM64 */
#define MI_PAGE_DISABLE_CACHE(pte) ((pte)->u.Hard.CacheType = 1)  /* Device memory */
#define MI_PAGE_WRITE_THROUGH(pte) ((pte)->u.Hard.CacheType = 2)  /* Write-through cache */
#define MI_PAGE_WRITE_COMBINED(pte) ((pte)->u.Hard.CacheType = 3) /* Write-combined */

/* MmIsRecursiveIoFault is implemented as a function in mmsup.c */

/* ARM64 Hyperspace constants */
#define MI_HYPERSPACE_PTES              256  /* Number of hyperspace PTEs */
#define MI_HYPERSPACE_END               (MM_HYPERSPACE_START + (MI_HYPERSPACE_PTES * PAGE_SIZE))
#ifndef HYPER_SPACE_END
#define HYPER_SPACE_END                 ((ULONG_PTR)0xFFFF97FFFFFFFFFFULL)
#endif

/* ARM64 additional missing constants and macros */
#define PTE_TOP                         0x1FFULL  /* Top-level PTE mask */
#define MI_DEBUG_MAPPING                0         /* Debug mapping flag */
#define MM_HIGHEST_VAD_ADDRESS          ((PVOID)0x000007FFFFFFFFFFULL)  /* Highest VAD address */
#define PDE_MAPPED_VA                   0xFFFF000080000000ULL  /* PDE mapped VA */
#define MM_EMPTY_PTE_LIST               ((ULONG)-1)  /* Empty PTE list marker */
#define MI_SYSTEM_PTE_BASE              ((PVOID)0xFFFF800000000000ULL)  /* System PTE base */
#ifndef MM_SYSTEM_PTE_BASE
#define MM_SYSTEM_PTE_BASE              MI_SYSTEM_PTE_BASE
#endif

/* ARM64 additional PTE/PDE manipulation macros */
#define MI_MAKE_DIRTY_PAGE(pte)         do { (pte)->u.Hard.NotDirty = 0; } while(0)  /* Clear NotDirty bit */

/* ARM64 function declarations */
VOID NTAPI KeFlushProcessTb(VOID);
VOID NTAPI KeInvalidateTlbEntry(IN PVOID VirtualAddress);

/* ARM64 additional missing constants */
/* MM_SHARED_USER_DATA_VA already defined in SDK headers as 0x7FFE0000ULL */
#define MI_MAX_FREE_PAGE_LISTS          4   /* Maximum free page lists */
#define MM_PTE_SOFTWARE_PROTECTION_BITS 5   /* Software protection bits */

/* ARM64 PTE/PDE relationship macros */
#define MiPteToPde(pte)                 ((PMMPDE)(((ULONG_PTR)(pte) & ~0xFFF) | 0x800))
#define MiPdeToPpe(pde)                 ((PMMPTE)(((ULONG_PTR)(pde) & ~0xFFF) | 0x400))

/* ARM64 PTE boundary checking */
#define MiIsPteOnPdeBoundary(pte)       (((ULONG_PTR)(pte) & 0xFFF) == 0)

/* ARM64 fault type checking macros */
#define MI_IS_INSTRUCTION_FETCH(addr)   FALSE  /* TODO: Implement instruction fetch detection */
#define MI_IS_NOT_PRESENT_FAULT(code)   ((code) & 0x1)  /* Check present bit */
#define MI_IS_PAGE_WRITEABLE(pte)       ((pte)->u.Hard.Writable)
#define MI_IS_PAGE_COPY_ON_WRITE(pte)   ((pte)->u.Hard.CopyOnWrite)
#define MI_IS_PAGE_DIRTY(pte)           (!(pte)->u.Hard.NotDirty)  /* Check dirty bit (inverted) */

/* ARM64 prototype PTE macros */
#define MiProtoPteToPte(proto)          ((PMMPTE)((ULONG_PTR)(proto)))

/* ARM64 mapping range constants */
#define MI_MAPPING_RANGE_START          MM_HYPERSPACE_START
#define MI_MAPPING_RANGE_END            ((ULONG_PTR)MI_MAPPING_RANGE_START + (MI_HYPERSPACE_PTES * PAGE_SIZE))

/* ARM64 additional PTE manipulation macros */
#define MI_MAKE_CLEAN_PAGE(pte)         do { (pte)->u.Hard.NotDirty = 1; } while(0)  /* Set NotDirty bit */
#define MiPdeToAddress(pde)             ((PVOID)((ULONG_PTR)(((ULONG_PTR)(pde) - PDE_BASE) >> 3) << 21))

/* ARM64 page fault type macros */
#define MI_IS_PAGE_LARGE(pte)           (!(pte)->u.Hard.NotLargePage)  /* Check large page bit (inverted) */
#define MI_IS_WRITE_ACCESS(code)        ((code) & 0x2)  /* Check write access bit */
#define MI_IS_PAGE_EXECUTABLE(pte)      (!(pte)->u.Hard.UserNoExecute && !(pte)->u.Hard.PrivilegedNoExecute)

/* ARM64 additional constants */
#define MI_NONPAGED_POOL_END            ((PVOID)0xFFFF900000000000ULL)  /* Non-paged pool end */

/* Missing constants from mminit.c for ARM64 */
#ifndef MI_DEFAULT_SYSTEM_RANGE_START
#define MI_DEFAULT_SYSTEM_RANGE_START   (PVOID)0xFFFF800000000000ULL
#endif
#ifndef MI_USER_PROBE_ADDRESS
#define MI_USER_PROBE_ADDRESS           (PVOID)0x00007FFFFFFEFFFFULL
#endif
/* MI_HIGHEST_USER_ADDRESS already defined in SDK headers as (PVOID)0x00007FFFFFFFFFFFULL */
#define MI_HIGHEST_SYSTEM_ADDRESS       (PVOID)0xFFFFFFFFFFFFFFFFULL
#define MM_EMPTY_LIST                   ((ULONG_PTR)-1)

/* System PTE tuning constants for ARM64 */
#define MI_MIN_PAGES_FOR_SYSPTE_TUNING          ((19 * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST           ((32 * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST     ((256 * 1024 * 1024) >> PAGE_SHIFT)

/* Memory allocation fragment constants for ARM64 */
#define MI_ALLOCATION_FRAGMENT                  (64 * 1024)
#define MI_MIN_ALLOCATION_FRAGMENT              (4 * 1024)
#define MI_MAX_ALLOCATION_FRAGMENT              (2 * 1024 * 1024)

/* Subsection PTE macro for ARM64 */
#define MiSubsectionPteToSubsection(x)          \
    (PMMPTE)((LONG64)(x)->u.Subsect.SubsectionAddress)

/* Additional missing constants for ARM64 MM initialization */
#define MI_PAGED_POOL_START             (PVOID)0xFFFF900000000000ULL
#define MI_MIN_INIT_PAGED_POOLSIZE      (32 * 1024 * 1024)  /* 32MB */
#define MI_SYSTEM_CACHE_WS_START        0xFFFFA00000001000ULL
#define MI_SYSTEM_CACHE_START           0xFFFFA00000000000ULL

/* Cache coloring constants for ARM64 */
#define MI_SECONDARY_COLORS             8    /* Number of secondary colors */
#define MI_MAX_SECONDARY_COLORS         16   /* Maximum secondary colors */
#define MI_MIN_SECONDARY_COLORS         2    /* Minimum secondary colors */

/* Address-to-PTE offset macro for ARM64 */
#define MiAddressToPteOffset(va)        (((ULONG_PTR)(va) >> 12) & 0x1FF)

/* ARM64 function stubs */
BOOLEAN NTAPI MiSynchronizeSystemPde(IN PMMPDE PointerPde);

#ifdef __cplusplus
}
#endif

#endif /* _NTOSKRNL_INCLUDE_INTERNAL_ARM64_MM_H */