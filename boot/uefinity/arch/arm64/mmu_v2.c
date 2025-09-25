/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 MMU and memory management for ReactOS
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <debug.h>


DBG_DEFAULT_CHANNEL(WARNING);

/* ARM64 page table attributes */
#define PTE_TYPE_MASK           (3 << 0)
#define PTE_TYPE_FAULT          (0 << 0)
#define PTE_TYPE_TABLE          (3 << 0)
#define PTE_TYPE_PAGE           (3 << 0)
#define PTE_TYPE_BLOCK          (1 << 0)
#define PTE_TYPE_VALID          (1 << 0)

/* Block attributes */
#define PTE_BLOCK_MEMTYPE(x)    ((x) << 2)
#define PTE_BLOCK_NS            (1 << 5)
#define PTE_BLOCK_NON_SHARE     (0 << 8)
#define PTE_BLOCK_OUTER_SHARE   (2 << 8)
#define PTE_BLOCK_INNER_SHARE   (3 << 8)
#define PTE_BLOCK_AF            (1 << 10)
#define PTE_BLOCK_NG            (1 << 11)
#define PTE_BLOCK_PXN           (1ULL << 53)
#define PTE_BLOCK_UXN           (1ULL << 54)
#define PTE_BLOCK_RO            (1ULL << 7)

/* Memory types */
#define MT_DEVICE_NGNRNE        0
#define MT_DEVICE_NGNRE         1
#define MT_DEVICE_GRE           2
#define MT_NORMAL_NC            3
#define MT_NORMAL               4

#define MEMORY_ATTRIBUTES       ((0x00ULL << (MT_DEVICE_NGNRNE * 8)) |  \
                                (0x04ULL << (MT_DEVICE_NGNRE * 8)) |    \
                                (0x0CULL << (MT_DEVICE_GRE * 8)) |      \
                                (0x44ULL << (MT_NORMAL_NC * 8)) |       \
                                (0xFFULL << (MT_NORMAL * 8)))

/* TCR flags */
#define TCR_T0SZ(x)             ((64 - (x)) << 0)
#define TCR_IRGN_NC             (0 << 8)
#define TCR_IRGN_WBWA           (1 << 8)
#define TCR_IRGN_WT             (2 << 8)
#define TCR_IRGN_WBNWA          (3 << 8)
#define TCR_IRGN_MASK           (3 << 8)
#define TCR_ORGN_NC             (0 << 10)
#define TCR_ORGN_WBWA           (1 << 10)
#define TCR_ORGN_WT             (2 << 10)
#define TCR_ORGN_WBNWA          (3 << 10)
#define TCR_ORGN_MASK           (3 << 10)
#define TCR_SHARED_NON          (0 << 12)
#define TCR_SHARED_OUTER        (2 << 12)
#define TCR_SHARED_INNER        (3 << 12)
#define TCR_TG0_4K              (0 << 14)
#define TCR_TG0_64K             (1 << 14)
#define TCR_TG0_16K             (2 << 14)
#define TCR_EPD1_DISABLE        (1 << 23)

#define TCR_EL1_RSVD            (1U << 31)
#define TCR_EL2_RSVD            (1U << 31 | 1 << 23)
#define TCR_EL3_RSVD            (1U << 31 | 1 << 23)

/* SCTLR_EL1 bits */
#define SCTLR_EL1_M             (1ULL << 0)   /* MMU enable */
#define SCTLR_EL1_A             (1ULL << 1)   /* Alignment check enable */
#define SCTLR_EL1_C             (1ULL << 2)   /* Data cache enable */
#define SCTLR_EL1_SA            (1ULL << 3)   /* Stack alignment check enable */
#define SCTLR_EL1_I             (1ULL << 12)  /* Instruction cache enable */

/* UEFI memory management integration */
extern FREELDR_MEMORY_DESCRIPTOR* UefiMemGetMemoryMap(PULONG MaxMemoryMapSize);
static BOOLEAN identity_mapping_enabled = FALSE;

/* Page tables - aligned to 4K boundaries */
static UINT64 arm64_l0_page_table[512] __attribute__((aligned(4096)));
static UINT64 arm64_l1_page_tables[4][512] __attribute__((aligned(4096)));
static BOOLEAN mmu_enabled = FALSE;

/* Function prototypes */
static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits);
static int get_effective_el(VOID);
static VOID setup_pgtables(VOID);
static VOID set_ttbr_tcr_mair(int el, UINT64 table, UINT64 tcr, UINT64 attr);

/* Get current effective exception level */
static int get_effective_el(VOID)
{
    int el = (ARM64_READ_SYSREG(CurrentEL) >> 2) & 3;
    
    if (el == 2) {
        UINT64 hcr_el2;
        __asm__ volatile("mrs %0, hcr_el2" : "=r" (hcr_el2));
        
        /* If using EL2&0 translation regime, TCR_EL2 looks like EL1 */
        if (hcr_el2 & (1ULL << 34))  /* HCR_EL2.E2H */
            return 1;
    }
    
    return el;
}

/* Calculate TCR and address space parameters using ReactOS memory map */
static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits)
{
    int el = get_effective_el();
    UINT64 max_addr = 0x100000000ULL; /* Default to 4GB */
    UINT64 ips, va_bits, tcr;
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;
    
    /* Get UEFI memory map to determine address space requirements */
    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 end_addr = (UINT64)(MemoryMap[i].BasePage + MemoryMap[i].PageCount) * PAGE_SIZE;
            if (end_addr > max_addr)
                max_addr = end_addr;
        }
    }
    
    /* Calculate maximum physical address space based on actual memory */
    if (max_addr > (1ULL << 44)) {
        ips = 5; va_bits = 48;
    } else if (max_addr > (1ULL << 42)) {
        ips = 4; va_bits = 44;
    } else if (max_addr > (1ULL << 40)) {
        ips = 3; va_bits = 42;
    } else if (max_addr > (1ULL << 36)) {
        ips = 2; va_bits = 40;
    } else if (max_addr > (1ULL << 32)) {
        ips = 1; va_bits = 36;
    } else {
        ips = 0; va_bits = 32;
    }
    
    /* Build TCR based on exception level */
    if (el == 1) {
        tcr = TCR_EL1_RSVD | (ips << 32) | TCR_EPD1_DISABLE;
    } else if (el == 2) {
        tcr = TCR_EL2_RSVD | (ips << 16);
    } else {
        tcr = TCR_EL3_RSVD | (ips << 16);
    }
    
    /* Add common TCR settings */
    tcr |= TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | 
           TCR_IRGN_WBWA | TCR_TG0_4K;
    
    if (pips) *pips = ips;
    if (pva_bits) *pva_bits = va_bits;
    
    return tcr;
}

/* Set MMU registers */
static VOID set_ttbr_tcr_mair(int el, UINT64 table, UINT64 tcr, UINT64 attr)
{
    __asm__ volatile("dsb sy");
    
    if (el == 1) {
        __asm__ volatile("msr ttbr0_el1, %0" : : "r" (table) : "memory");
        __asm__ volatile("msr tcr_el1, %0" : : "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el1, %0" : : "r" (attr) : "memory");
    } else if (el == 2) {
        __asm__ volatile("msr ttbr0_el2, %0" : : "r" (table) : "memory");
        __asm__ volatile("msr tcr_el2, %0" : : "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el2, %0" : : "r" (attr) : "memory");
    } else if (el == 3) {
        __asm__ volatile("msr ttbr0_el3, %0" : : "r" (table) : "memory");
        __asm__ volatile("msr tcr_el3, %0" : : "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el3, %0" : : "r" (attr) : "memory");
    }
    
    __asm__ volatile("isb");
}

/* Set up page tables using ReactOS memory map */
static VOID setup_pgtables(VOID)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;
    UINT64 l1_table_idx;
    UINT64 *l1_table;
    
    /* Clear all page tables */
    RtlZeroMemory(arm64_l0_page_table, sizeof(arm64_l0_page_table));
    RtlZeroMemory(arm64_l1_page_tables, sizeof(arm64_l1_page_tables));
    
    /* Set up L0 page table entries pointing to L1 tables */
    for (i = 0; i < 4; i++) {
        arm64_l0_page_table[i] = (UINT64)&arm64_l1_page_tables[i][0] |
                                PTE_TYPE_VALID | PTE_TYPE_TABLE;
    }
    
    /* Get UEFI memory map and create identity mappings */
    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 phys_start = (UINT64)MemoryMap[i].BasePage * PAGE_SIZE;
            UINT64 size = (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;
            UINT64 attrs;
            
            /* Determine page attributes based on memory type */
            switch (MemoryMap[i].MemoryType) {
                case LoaderFirmwarePermanent:
                case LoaderFirmwareTemporary:
                    /* Firmware memory - device attributes */
                    attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
                           PTE_BLOCK_NON_SHARE | PTE_BLOCK_AF |
                           PTE_BLOCK_PXN | PTE_BLOCK_UXN;
                    break;
                    
                case LoaderFree:
                case LoaderLoadedProgram:
                case LoaderOsloaderHeap:
                case LoaderOsloaderStack:
                default:
                    /* Normal memory - cacheable */
                    attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
                           PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF;
                    break;
            }
            
            /* Map in 1GB chunks using L1 blocks */
            while (size > 0)
            {
                UINT64 chunk_size = (size > 0x40000000ULL) ? 0x40000000ULL : size;

                /* Compute L0 and L1 indices for this physical address */
                UINT64 l0_index = (phys_start >> 39) & 0x1FF;     /* 512GB granularity */
                l1_table_idx = (phys_start >> 30) & 0x1FF;        /* 1GB granularity */
                if (l0_index >= 4)
                {
                    /* Outside of our static L0 coverage; skip */
                    phys_start += chunk_size;
                    size -= chunk_size;
                    continue;
                }

                l1_table = arm64_l1_page_tables[l0_index];

                /* Create 1GB identity mapping block entry */
                if (l1_table[l1_table_idx] == 0)
                {
                    l1_table[l1_table_idx] = (phys_start & ~0x3FFFFFFFULL) |
                                             PTE_TYPE_VALID | PTE_TYPE_BLOCK | attrs;
                }

                phys_start += chunk_size;
                size -= chunk_size;
            }
        }
    }
    
    TRACE("ARM64: Identity mapping page tables set up using UEFI memory map\n");
}

/* Initialize ARM64 MMU using ReactOS memory management */
VOID Arm64InitializeMMU(VOID)
{
    UINT64 tcr, sctlr;
    UINT64 ips, va_bits;
    int el;
    
    TRACE("ARM64: Initializing MMU\n");
    
    el = get_effective_el();
    TRACE("ARM64: Running at EL%d\n", el);
    
    /* Set up page tables */
    setup_pgtables();
    
    /* Get TCR configuration */
    tcr = get_tcr(&ips, &va_bits);
    
    TRACE("ARM64: Using %llu-bit VA, %llu-bit PA\n", va_bits, 
          (ips == 5) ? 48 : (ips == 4) ? 44 : (ips == 3) ? 42 : 
          (ips == 2) ? 40 : (ips == 1) ? 36 : 32);
    
    /* Configure MMU registers */
    set_ttbr_tcr_mair(el, (UINT64)arm64_l0_page_table, tcr, MEMORY_ATTRIBUTES);
    
    /* Enable MMU and caches based on current EL */
    if (el == 1)
    {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
        sctlr |= SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I | SCTLR_EL1_SA;
        __asm__ volatile("msr sctlr_el1, %0" :: "r" (sctlr) : "memory");
    }
    else if (el == 2)
    {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r" (sctlr));
        sctlr |= (1ULL << 0) /* M */ | (1ULL << 2) /* C */ | (1ULL << 12) /* I */;
        __asm__ volatile("msr sctlr_el2, %0" :: "r" (sctlr) : "memory");
    }
    else if (el == 3)
    {
        __asm__ volatile("mrs %0, sctlr_el3" : "=r" (sctlr));
        sctlr |= (1ULL << 0) /* M */ | (1ULL << 2) /* C */ | (1ULL << 12) /* I */;
        __asm__ volatile("msr sctlr_el3, %0" :: "r" (sctlr) : "memory");
    }
    ARM64_ISB();
    
    mmu_enabled = TRUE;
    identity_mapping_enabled = TRUE;
    
    TRACE("ARM64: MMU enabled with ReactOS identity mapping\n");
}

/* Map a virtual address range to physical address */
BOOLEAN Arm64MapVirtualMemory(ULONGLONG VirtualAddress,
                              ULONGLONG PhysicalAddress,
                              ULONGLONG Size,
                              ULONG Attributes)
{
    UINT64 va = VirtualAddress;
    UINT64 pa = PhysicalAddress;
    UINT64 end = VirtualAddress + Size;
    UINT64 attrs;

    TRACE("ARM64: Map VA=0x%016llx -> PA=0x%016llx, Size=0x%016llx, Attr=0x%lx\n",
          VirtualAddress, PhysicalAddress, Size, Attributes);

    /* Only support 1GB block mappings with alignment for now */
    if (((va | pa | Size) & 0x3FFFFFFFULL) != 0)
    {
        TRACE("ARM64: Map only supports 1GB-aligned blocks\n");
        return FALSE;
    }

    /* Translate Attributes (memory type index) into block attributes */
    attrs = PTE_BLOCK_MEMTYPE(Attributes) | PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF |
            PTE_BLOCK_PXN | PTE_BLOCK_UXN;

    while (va < end)
    {
        UINT64 l0 = (va >> 39) & 0x1FF;
        UINT64 l1 = (va >> 30) & 0x1FF;
        UINT64 *l1_table;

        if (l0 >= 4)
        {
            TRACE("ARM64: L0 index out of range for VA 0x%llx\n", va);
            return FALSE;
        }

        l1_table = arm64_l1_page_tables[l0];
        l1_table[l1] = (pa & ~0x3FFFFFFFULL) | PTE_TYPE_VALID | PTE_TYPE_BLOCK | attrs;

        va += 0x40000000ULL;
        pa += 0x40000000ULL;
    }

    Arm64FlushTlbRange(VirtualAddress, Size);
    return TRUE;
}

/* Unmap a virtual address range */
BOOLEAN Arm64UnmapVirtualMemory(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    UINT64 va = VirtualAddress;
    UINT64 end = VirtualAddress + Size;

    TRACE("ARM64: Unmap VA=0x%016llx, Size=0x%016llx\n", VirtualAddress, Size);

    if (((va | Size) & 0x3FFFFFFFULL) != 0)
    {
        TRACE("ARM64: Unmap only supports 1GB-aligned blocks\n");
        return FALSE;
    }

    while (va < end)
    {
        UINT64 l0 = (va >> 39) & 0x1FF;
        UINT64 l1 = (va >> 30) & 0x1FF;
        if (l0 < 4)
        {
            UINT64 *l1_table = arm64_l1_page_tables[l0];
            l1_table[l1] = 0;
        }
        va += 0x40000000ULL;
    }

    Arm64FlushTlbRange(VirtualAddress, Size);
    return TRUE;
}

/* Get physical address from virtual address */
ULONGLONG Arm64GetPhysicalAddress(ULONGLONG VirtualAddress)
{
    UINT64 va = VirtualAddress;

    /* Identity mapping common in FreeLoader */
    if (!mmu_enabled || identity_mapping_enabled)
        return VirtualAddress;

    /* Walk our L0/L1 tables for block mapping */
    {
        UINT64 l0 = (va >> 39) & 0x1FF;
        UINT64 l1 = (va >> 30) & 0x1FF;
        UINT64 off = va & 0x3FFFFFFFULL; /* 1GB block offset */
        if (l0 < 4)
        {
            UINT64 pte = arm64_l1_page_tables[l0][l1];
            if ((pte & PTE_TYPE_VALID) && (pte & PTE_TYPE_BLOCK))
            {
                return (pte & ~0x3FFFFFFFULL) | off;
            }
        }
    }

    /* As a fallback, assume identity */
    return VirtualAddress;
}

/* Check if MMU is enabled */
BOOLEAN Arm64IsMMUEnabled(VOID)
{
    return mmu_enabled;
}

/* Disable MMU (for kernel handoff) */
VOID Arm64DisableMMU(VOID)
{
    ULONGLONG sctlr;
    
    if (!mmu_enabled)
        return;
    
    TRACE("ARM64: Disabling MMU\n");
    
    /* Clean and invalidate all caches */
    Arm64FlushDataCacheAll();
    Arm64InvalidateInstructionCacheAll();
    ARM64_DSB_SY();
    ARM64_ISB();
    
    /* Disable MMU and caches (current EL) */
    {
        int el = get_effective_el();
        if (el == 1)
        {
            __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
            sctlr &= ~(SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I);
            __asm__ volatile("msr sctlr_el1, %0" :: "r" (sctlr) : "memory");
        }
        else if (el == 2)
        {
            __asm__ volatile("mrs %0, sctlr_el2" : "=r" (sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el2, %0" :: "r" (sctlr) : "memory");
        }
        else if (el == 3)
        {
            __asm__ volatile("mrs %0, sctlr_el3" : "=r" (sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el3, %0" :: "r" (sctlr) : "memory");
        }
    }
    ARM64_ISB();
    
    mmu_enabled = FALSE;
    
    TRACE("ARM64: MMU disabled\n");
}

/* Get memory attributes for an address using UEFI memory map */
ULONG Arm64GetMemoryAttributes(ULONGLONG Address)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;
    
    /* Get UEFI memory map and find the region containing this address */
    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 start = (UINT64)MemoryMap[i].BasePage * PAGE_SIZE;
            UINT64 end = start + (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;
            
            if (Address >= start && Address < end) {
                /* Return memory type based on UEFI/ReactOS memory descriptor */
                switch (MemoryMap[i].MemoryType) {
                    case LoaderFirmwarePermanent:
                    case LoaderFirmwareTemporary:
                        return MT_DEVICE_NGNRNE;
                    case LoaderFree:
                    case LoaderLoadedProgram:
                    case LoaderOsloaderHeap:
                    case LoaderOsloaderStack:
                    default:
                        return MT_NORMAL;
                }
            }
        }
    }
    
    return MT_NORMAL;  /* Default to normal memory */
}

/* Flush TLB for specific address range */
VOID Arm64FlushTlbRange(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    ULONGLONG end = VirtualAddress + Size;
    ULONGLONG addr;
    
    /* Flush TLB entries for each page in the range */
    for (addr = VirtualAddress & ~0xFFFULL; 
         addr < end; 
         addr += 0x1000ULL)
    {
        __asm__ volatile ("tlbi vaae1, %0" :: "r" (addr >> 12));
    }
    
    ARM64_DSB_SY();
    ARM64_ISB();
}
