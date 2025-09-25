/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 MMU and memory management for ReactOS
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <debug.h>
#include <Uefi.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* Simple UART output for critical debugging during MMU switches */
#define PL011_BASE   0x09000000U
#define PL011_FR     (*(volatile ULONG *)(PL011_BASE + 0x18))
#define PL011_DR     (*(volatile ULONG *)(PL011_BASE + 0x00))
#define PL011_TXFF   (1u << 5)

static inline VOID UartPutc(char Ch)
{
    while (PL011_FR & PL011_TXFF)
    {
        __asm__ __volatile__("wfi");
    }
    PL011_DR = (unsigned char)Ch;
}

static inline VOID UartPuts(const char *String)
{
    while (*String)
    {
        if (*String == '\n')
            UartPutc('\r');
        UartPutc(*String++);
    }
}

static inline VOID UartPutHex64(ULONGLONG Value)
{
    static const char HexDigits[] = "0123456789ABCDEF";
    for (LONG Index = 15; Index >= 0; --Index)
    {
        ULONG Shift = (ULONG)Index * 4;
        UartPutc(HexDigits[(Value >> Shift) & 0xFULL]);
    }
}

static inline VOID UartPutHex32(ULONG Value)
{
    static const char HexDigits[] = "0123456789ABCDEF";
    for (LONG Index = 7; Index >= 0; --Index)
    {
        ULONG Shift = (ULONG)Index * 4;
        UartPutc(HexDigits[(Value >> Shift) & 0xFUL]);
    }
}

/* ARRAYSIZE fallback if not defined */
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
/* Presently identity, but use consistently when placing PAs into PTEs */
#ifndef VA_TO_PA
#define VA_TO_PA(x) ((UINT64)(x))
#endif
/* Presently identity too; use when turning PA from a PTE into a C pointer */
#ifndef PA_TO_VA
#define PA_TO_VA(x) ((UINT64)(x))
#endif

#ifndef ARM64_MAP_ATTR_UXN
#define ARM64_MAP_ATTR_UXN 0
#endif
#ifndef ARM64_MAP_ATTR_PXN
#define ARM64_MAP_ATTR_PXN 0
#endif

extern EFI_SYSTEM_TABLE *GlobalSystemTable;

/* ---------- Barrier & TLBI helpers (multicore-safe) ---------- */

#define ARM64_DSB_ISH()   __asm__ volatile("dsb ish" ::: "memory")
#define ARM64_DSB_ISHST() __asm__ volatile("dsb ishst" ::: "memory")

/* Global/all-ASID TLBI */
#define TLBI_VMALLE1IS()  __asm__ volatile("tlbi vmalle1is" ::: "memory")
#define TLBI_ALLE2IS()    __asm__ volatile("tlbi alle2is" ::: "memory")
#define TLBI_ALLE3IS()    __asm__ volatile("tlbi alle3is" ::: "memory")

/* Per-VA, all ASIDs (EL1&0) */
static inline void tlbi_vaae1is_by_va(ULONGLONG va)
{
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(va >> 12) : "memory");
}

/* ---------- Descriptor bits ---------- */

#define PTE_TYPE_MASK           (3ULL << 0)
#define PTE_TYPE_FAULT          (0ULL << 0)
#define PTE_TYPE_TABLE          (3ULL << 0)  /* next-level pointer */
#define PTE_TYPE_PAGE           (3ULL << 0)  /* leaf at L3 */
#define PTE_TYPE_BLOCK          (1ULL << 0)  /* leaf at L1/L2 */
#define PTE_TYPE_VALID          (1ULL << 0)

/* Block/Page attributes */
#define PTE_BLOCK_MEMTYPE(x)    ((x) << 2)
#define PTE_BLOCK_NS            (1ULL << 5)
#define PTE_BLOCK_NON_SHARE     (0ULL << 8)
#define PTE_BLOCK_OUTER_SHARE   (2ULL << 8)
#define PTE_BLOCK_INNER_SHARE   (3ULL << 8)
#define PTE_BLOCK_AF            (1ULL << 10)
#define PTE_BLOCK_NG            (1ULL << 11)
#define PTE_BLOCK_PXN           (1ULL << 53)
#define PTE_BLOCK_UXN           (1ULL << 54)
#define PTE_BLOCK_RO            (1ULL << 7)

#define PTE_BLOCK_MEMTYPE_MASK  (7ULL << 2)

static inline UINT64
sanitize_block_attrs(UINT64 attrs)
{
    UINT64 sanitized = attrs;

    if ((sanitized & PTE_BLOCK_MEMTYPE_MASK) == 0)
        sanitized |= PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB);

    if ((sanitized & PTE_BLOCK_AF) == 0)
        sanitized |= PTE_BLOCK_AF;

    if ((sanitized & (3ULL << 8)) == 0)
        sanitized |= PTE_BLOCK_INNER_SHARE;

    return sanitized;
}

#ifndef ARM64_BLOCK_SIZE_2M
#define ARM64_BLOCK_SIZE_2M            (1ULL << 21)
#define ARM64_BLOCK_MASK_2M            (ARM64_BLOCK_SIZE_2M - 1ULL)
#endif

/* Descriptor classification helpers */
#define DESC_VALID(e)     (((e) & PTE_TYPE_VALID) != 0)
#define DESC_TYPE(e)      ((e) & PTE_TYPE_MASK)
/* Table: valid && type==3 && !AF (AF is leaf-only) */
#define DESC_IS_TABLE(e)  (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_TABLE) && !((e) & PTE_BLOCK_AF))
/* Leaf (block or page) */
#define DESC_IS_BLOCK(e)  (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_BLOCK))
#define DESC_IS_PAGE(e)   (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_PAGE) && ((e) & PTE_BLOCK_AF))
#define DESC_IS_LEAF(e)   (DESC_IS_BLOCK(e) || DESC_IS_PAGE(e))

/* ---------- MAIR / memory types ---------- */

#define MEMORY_ATTRIBUTES       ((0x00ULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8)) |  \
                                (0x04ULL << (ARM64_MEM_ATTR_DEVICE_nGnRE * 8)) |    \
                                (0x0CULL << (ARM64_MEM_ATTR_DEVICE_GRE * 8)) |      \
                                (0x44ULL << (ARM64_MEM_ATTR_NORMAL_NC * 8)) |       \
                                (0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8)))

/* ---------- TCR helpers ---------- */
#define TCR_T0SZ(x)             ((64ULL - (x)) << 0)
#define TCR_IRGN_WBWA           (1ULL << 8)
#define TCR_ORGN_WBWA           (1ULL << 10)
#define TCR_SHARED_INNER        (3ULL << 12)
#define TCR_TG0_4K              (0ULL << 14)
#define TCR_T1SZ(x)             ((64ULL - (x)) << 16)
#define TCR_IRGN1_WBWA          (1ULL << 24)
#define TCR_ORGN1_WBWA          (1ULL << 26)
#define TCR_SHARED1_INNER       (3ULL << 28)
#define TCR_TG1_4K              (1ULL << 30)
#define TCR_A1                  (1ULL << 22)
#define TCR_EPD0                (1ULL << 7)
#define TCR_EPD1                (1ULL << 23)

#define TCR_EL1_RSVD            (1U << 31)
#define TCR_EL2_RSVD            (1U << 31 | 1 << 23)
#define TCR_EL3_RSVD            (1U << 31 | 1 << 23)

/* SCTLR_EL1 bits */
#define SCTLR_EL1_M             (1ULL << 0)
#define SCTLR_EL1_A             (1ULL << 1)
#define SCTLR_EL1_C             (1ULL << 2)
#define SCTLR_EL1_SA            (1ULL << 3)
#define SCTLR_EL1_I             (1ULL << 12)

/* UEFI memory management integration */
extern FREELDR_MEMORY_DESCRIPTOR* UefiMemGetMemoryMap(PULONG MaxMemoryMapSize);
static BOOLEAN identity_mapping_enabled = FALSE;
static BOOLEAN page_tables_initialized = FALSE;

#define ARM64_KSEG0_L0_INDEX      (((ULONGLONG)ARM64_KSEG0_BASE >> 39) & 0x1FFULL)
#define ARM64_KERNEL_L1_TABLES    4U
#define ARM64_USER_L1_TABLES      4U

/* Page tables - aligned to 4K boundaries */
static UINT64 arm64_l0_page_table[512] __attribute__((aligned(4096)));
static UINT64 arm64_l1_page_tables[ARM64_USER_L1_TABLES][512] __attribute__((aligned(4096)));
static UINT64 arm64_kernel_l0_table[512] __attribute__((aligned(4096)));
static UINT64 arm64_kernel_l1_tables[ARM64_KERNEL_L1_TABLES][512] __attribute__((aligned(4096)));

/* Additional L2 and L3 tables */
#define ARM64_L2_TABLES_PER_L1     4U
#define ARM64_L3_TABLES_PER_L2     64U
static UINT64 arm64_kernel_l2_tables[ARM64_KERNEL_L1_TABLES][ARM64_L2_TABLES_PER_L1][512] __attribute__((aligned(4096)));
static UINT64 arm64_kernel_l3_tables[ARM64_KERNEL_L1_TABLES][ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][512] __attribute__((aligned(4096)));
static UINT64 arm64_user_l2_tables[ARM64_USER_L1_TABLES][ARM64_L2_TABLES_PER_L1][512] __attribute__((aligned(4096)));
static UINT64 arm64_user_l3_tables[ARM64_USER_L1_TABLES][ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][512] __attribute__((aligned(4096)));
static UINT64 arm64_l2_next_index[ARM64_KERNEL_L1_TABLES] = {0};
static UINT64 arm64_l3_next_index[ARM64_KERNEL_L1_TABLES][ARM64_L2_TABLES_PER_L1] = {{0}};
static UINT64 arm64_user_l2_next_index[ARM64_USER_L1_TABLES] = {0};
static UINT64 arm64_user_l3_next_index[ARM64_USER_L1_TABLES][ARM64_L2_TABLES_PER_L1] = {{0}};

static BOOLEAN mmu_enabled = FALSE;

/* Cache computed address-space parameters so we can avoid UEFI calls post-EBS */
static BOOLEAN tcr_limits_cached = FALSE;
static UINT64 cached_max_physical_address = 0x100000000ULL;

/* EL1/EL2-with-E2H register aliases */
#define SCTLR_EL12_SYSREG  "S3_5_C1_C0_0"
#define TTBR0_EL12_SYSREG  "S3_5_C2_C0_0"
#define TTBR1_EL12_SYSREG  "S3_5_C2_C0_1"
#define TCR_EL12_SYSREG    "S3_5_C2_C0_2"
#define MAIR_EL12_SYSREG   "S3_5_C10_C2_0"

/* Prototypes */
static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits);
static int get_effective_el(VOID);
static BOOLEAN use_el12_registers(VOID);
static VOID setup_pgtables(VOID);
static VOID ensure_page_tables_initialized(VOID);
static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr);
static VOID debug_dump_static_mapping(UINT64 va);

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs);
static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index);
static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index);
static UINT64 get_l2_slot_index(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table);

/* ---------- Hardware capability query ---------- */
/* ID_AA64MMFR0_EL1: PARange[3:0], TGran4[31:28] (0 = supported) */
static BOOLEAN g_granule4k_supported = TRUE;
static UINT64  g_parange_field      = 0;

static VOID query_mmfr0_caps(VOID)
{
    UINT64 mmfr0;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    g_parange_field = (mmfr0 & 0xFULL);               /* PARange */
    UINT64 tgran4   = (mmfr0 >> 28) & 0xFULL;         /* TGran4 */
    g_granule4k_supported = (tgran4 == 0);            /* 0 == supported */
}

/* ---------- EL helpers ---------- */

static int get_effective_el(VOID)
{
    return (int)((ARM64_READ_SYSREG(CurrentEL) >> 2) & 3);
}

static BOOLEAN use_el12_registers(VOID)
{
    int el = get_effective_el();
    if (el != 2) return FALSE;
    UINT64 hcr_el2;
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr_el2));
    return (hcr_el2 & (1ULL << 34)) != 0; /* HCR_EL2.E2H */
}

/* ---------- Safe PTE write & BBM helpers ---------- */

static inline void pte_write(UINT64 *entry, UINT64 val)
{
    *entry = val;
    Arm64CleanDataCacheToPoC((ULONGLONG)(uintptr_t)entry);
    ARM64_DSB_ISHST(); /* Ensure PTE store is visible before we invalidate TLBs */
}

/* Heavy (boot-safe) BBM: clear -> DSB ISHST -> TLBI all -> DSB ISH -> ISB -> set */
static inline void pte_replace_break_before_make(UINT64 *entry, UINT64 newval)
{
    if (*entry == newval) {
        return;
    }
    if (DESC_VALID(*entry)) {
        pte_write(entry, 0);
        TLBI_VMALLE1IS();
        ARM64_DSB_ISH();
        ARM64_ISB();
    }
    pte_write(entry, newval);
}

/* ---------- TCR composition (now clamped by hw caps) ---------- */

static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits)
{
    query_mmfr0_caps();
    if (!g_granule4k_supported) {
        ERR("ARM64: 4KiB granule not supported on this CPU; MMU setup aborted.\n");
        if (pips) *pips = 0;
        if (pva_bits) *pva_bits = 0;
        return 0;
    }

    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    UINT64 max_addr = cached_max_physical_address;
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;
    BOOLEAN boot_services_available = (GlobalSystemTable && GlobalSystemTable->BootServices);

    /* Refresh cached physical limit when possible */
    if (boot_services_available) {
        MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
        if (MemoryMap) {
            max_addr = 0;
            for (i = 0; i < MemoryMapSize; i++) {
                UINT64 end_addr = (UINT64)(MemoryMap[i].BasePage + MemoryMap[i].PageCount) * PAGE_SIZE;
                if (end_addr > max_addr) max_addr = end_addr;
            }
            cached_max_physical_address = max_addr;
            tcr_limits_cached = TRUE;
        } else if (!tcr_limits_cached) {
            max_addr = 0x100000000ULL;
        }
    } else if (!tcr_limits_cached) {
        max_addr = 0x100000000ULL;
    }

    if (!boot_services_available && tcr_limits_cached) {
        max_addr = cached_max_physical_address;
    }

    /* Desired IPS from memory size */
    UINT64 ips_desired;
    UINT64 va_bits = (max_addr > (1ULL << 44)) ? 48 :
                     (max_addr > (1ULL << 42)) ? 44 :
                     (max_addr > (1ULL << 40)) ? 42 :
                     (max_addr > (1ULL << 36)) ? 40 :
                     (max_addr > (1ULL << 32)) ? 36 : 32;

    ips_desired = (va_bits == 48) ? 5 :
                  (va_bits == 44) ? 4 :
                  (va_bits == 42) ? 3 :
                  (va_bits == 40) ? 2 :
                  (va_bits == 36) ? 1 : 0;

    /* Clamp IPS to hardware PARange */
    UINT64 ips_hw = g_parange_field; /* 0..6 per ARM ARM; we only use up to 5 */
    if (ips_desired > ips_hw) ips_desired = ips_hw;

    UINT64 tcr;
    if (el == 1 || el12) {
        tcr = TCR_EL1_RSVD | (ips_desired << 32);
    } else if (el == 2) {
        tcr = TCR_EL2_RSVD | (ips_desired << 16);
    } else {
        tcr = TCR_EL3_RSVD | (ips_desired << 16);
    }

    /* TTBR0 (user/identity) */
    tcr |= TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;
    /* TTBR1 (kernel window fixed 48b) */
    tcr |= TCR_T1SZ(48) | TCR_SHARED1_INNER | TCR_ORGN1_WBWA | TCR_IRGN1_WBWA | TCR_TG1_4K | TCR_A1;

    if (pips) *pips = ips_desired;
    if (pva_bits) *pva_bits = va_bits;
    return tcr;
}

/* ---------- Register programming ---------- */

static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr)
{
    BOOLEAN el12 = use_el12_registers();
    UINT64 phys_table0 = VA_TO_PA(table0);
    UINT64 phys_table1 = VA_TO_PA(table1);

    ARM64_DSB_ISH();

    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " TTBR0_EL12_SYSREG ", %0" :: "r"(phys_table0) : "memory");
            __asm__ volatile("msr " TTBR1_EL12_SYSREG ", %0" :: "r"(phys_table1) : "memory");
            __asm__ volatile("msr " TCR_EL12_SYSREG   ", %0" :: "r"(tcr)    : "memory");
            __asm__ volatile("msr " MAIR_EL12_SYSREG  ", %0" :: "r"(attr)   : "memory");
        } else {
            __asm__ volatile("msr ttbr0_el1, %0" :: "r"(phys_table0) : "memory");
            __asm__ volatile("msr ttbr1_el1, %0" :: "r"(phys_table1) : "memory");
            __asm__ volatile("msr tcr_el1, %0"  :: "r"(tcr)     : "memory");
            __asm__ volatile("msr mair_el1, %0" :: "r"(attr)    : "memory");
        }
    } else if (el == 2) {
        __asm__ volatile("msr ttbr0_el2, %0" :: "r"(phys_table0) : "memory");
        __asm__ volatile("msr tcr_el2, %0"   :: "r"(tcr)    : "memory");
        __asm__ volatile("msr mair_el2, %0"  :: "r"(attr)   : "memory");
    } else if (el == 3) {
        __asm__ volatile("msr ttbr0_el3, %0" :: "r"(phys_table0) : "memory");
        __asm__ volatile("msr tcr_el3, %0"   :: "r"(tcr)    : "memory");
        __asm__ volatile("msr mair_el3, %0"  :: "r"(attr)   : "memory");
    }

    ARM64_ISB();
}

/* ---------- Page-table allocation helpers ---------- */

static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index)
{
    UINT64 entry = l1_table[l1_index];

    if (DESC_VALID(entry)) {
        if (DESC_IS_TABLE(entry))
            return (UINT64 *)PA_TO_VA(entry & ~0xFFFULL);

        if (DESC_IS_LEAF(entry))
        {
            UINT64 block_base = entry & ~ARM64_BLOCK_MASK_1G;
            UINT64 block_attrs = sanitize_block_attrs(entry & ~((UINT64)ARM64_BLOCK_MASK_1G | PTE_TYPE_MASK));
            UINT64 *split_table;

            if (is_kernel)
            {
                if (l0_slot >= ARM64_KERNEL_L1_TABLES) return NULL;
                if (arm64_l2_next_index[l0_slot] >= ARM64_L2_TABLES_PER_L1) return NULL;
                UINT64 index = arm64_l2_next_index[l0_slot]++;
                split_table = arm64_kernel_l2_tables[l0_slot][index];
            }
            else
            {
                if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
                if (arm64_user_l2_next_index[l0_slot] >= ARM64_L2_TABLES_PER_L1) return NULL;
                UINT64 index = arm64_user_l2_next_index[l0_slot]++;
                split_table = arm64_user_l2_tables[l0_slot][index];
            }

            RtlZeroMemory(split_table, PAGE_SIZE);
            for (ULONG i = 0; i < 512; ++i)
            {
                UINT64 pa = block_base + ((UINT64)i << 21);
                split_table[i] = pa | PTE_TYPE_VALID | PTE_TYPE_BLOCK | block_attrs;
            }

            pte_replace_break_before_make(&l1_table[l1_index],
                                          VA_TO_PA(split_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE);
            return split_table;
        }

        TRACE("ARM64: ensure_l2_table unexpected descriptor L1[%llu]=0x%llx\n",
              (unsigned long long)l1_index,
              (unsigned long long)entry);
        return NULL;
    }

    if (is_kernel) {
        if (l0_slot >= ARM64_KERNEL_L1_TABLES) return NULL;
        if (arm64_l2_next_index[l0_slot] >= ARM64_L2_TABLES_PER_L1) return NULL;

        UINT64 index = arm64_l2_next_index[l0_slot]++;
        UINT64 *new_table = arm64_kernel_l2_tables[l0_slot][index];
        RtlZeroMemory(new_table, PAGE_SIZE);
        pte_write(&l1_table[l1_index], (VA_TO_PA(new_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
        return new_table;
    }

    if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
    if (arm64_user_l2_next_index[l0_slot] >= ARM64_L2_TABLES_PER_L1) return NULL;

    UINT64 index = arm64_user_l2_next_index[l0_slot]++;
    UINT64 *new_table = arm64_user_l2_tables[l0_slot][index];
    RtlZeroMemory(new_table, PAGE_SIZE);
    pte_write(&l1_table[l1_index], (VA_TO_PA(new_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
    return new_table;
}

static UINT64 get_l2_slot_index(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table)
{
    if (is_kernel)
        return (UINT64)(l2_table - &arm64_kernel_l2_tables[l0_slot][0][0]) / 512ULL;

    return (UINT64)(l2_table - &arm64_user_l2_tables[l0_slot][0][0]) / 512ULL;
}

static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index)
{
    UINT64 entry = l2_table[l2_index];
    UINT64 l2_slot = get_l2_slot_index(is_kernel, l0_slot, l2_table);

    if (DESC_VALID(entry)) {
        if (DESC_IS_TABLE(entry))
            return (UINT64 *)PA_TO_VA(entry & ~0xFFFULL);

        if (DESC_IS_LEAF(entry))
        {
            UINT64 block_base = entry & ~ARM64_BLOCK_MASK_2M;
            UINT64 block_attrs = sanitize_block_attrs(entry & ~((UINT64)ARM64_BLOCK_MASK_2M | PTE_TYPE_MASK));
            UINT64 *split_table;

            if (is_kernel)
            {
                if (l0_slot >= ARM64_KERNEL_L1_TABLES) return NULL;
                if (l2_slot >= ARM64_L2_TABLES_PER_L1) return NULL;
                if (arm64_l3_next_index[l0_slot][l2_slot] >= ARM64_L3_TABLES_PER_L2) {
                    TRACE("ARM64: ensure_l3_table out of kernel L3 tables (l0=%llu l2=%llu)\n",
                          (unsigned long long)l0_slot, (unsigned long long)l2_slot);
                    return NULL;
                }
                UINT64 index = arm64_l3_next_index[l0_slot][l2_slot]++;
                split_table = arm64_kernel_l3_tables[l0_slot][l2_slot][index];
            }
            else
            {
                if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
                if (l2_slot >= ARM64_L2_TABLES_PER_L1) return NULL;
                if (arm64_user_l3_next_index[l0_slot][l2_slot] >= ARM64_L3_TABLES_PER_L2) {
                    TRACE("ARM64: ensure_l3_table out of user L3 tables (l0=%llu l2=%llu)\n",
                          (unsigned long long)l0_slot, (unsigned long long)l2_slot);
                    return NULL;
                }
                UINT64 index = arm64_user_l3_next_index[l0_slot][l2_slot]++;
                split_table = arm64_user_l3_tables[l0_slot][l2_slot][index];
            }

            RtlZeroMemory(split_table, PAGE_SIZE);
            for (ULONG i = 0; i < 512; ++i)
            {
                UINT64 pa = block_base + ((UINT64)i << 12);
                split_table[i] = pa | PTE_TYPE_VALID | PTE_TYPE_PAGE | block_attrs;
            }

            pte_replace_break_before_make(&l2_table[l2_index],
                                          VA_TO_PA(split_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE);
            return split_table;
        }

        TRACE("ARM64: ensure_l3_table unexpected descriptor L2[%llu]=0x%llx\n",
              (unsigned long long)l2_index,
              (unsigned long long)entry);
        return NULL;
    }

    if (is_kernel)
    {
        if (l0_slot >= ARM64_KERNEL_L1_TABLES) return NULL;
        if (l2_slot >= ARM64_L2_TABLES_PER_L1) return NULL;
        if (arm64_l3_next_index[l0_slot][l2_slot] >= ARM64_L3_TABLES_PER_L2) {
            TRACE("ARM64: ensure_l3_table out of kernel L3 tables (l0=%llu l2=%llu)\n",
                  (unsigned long long)l0_slot, (unsigned long long)l2_slot);
            return NULL;
        }

        UINT64 index = arm64_l3_next_index[l0_slot][l2_slot]++;
        UINT64 *new_table = arm64_kernel_l3_tables[l0_slot][l2_slot][index];
        RtlZeroMemory(new_table, PAGE_SIZE);
        pte_write(&l2_table[l2_index], (VA_TO_PA(new_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
        return new_table;
    }

    if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
    if (l2_slot >= ARM64_L2_TABLES_PER_L1) return NULL;
    if (arm64_user_l3_next_index[l0_slot][l2_slot] >= ARM64_L3_TABLES_PER_L2) {
        TRACE("ARM64: ensure_l3_table out of user L3 tables (l0=%llu l2=%llu)\n",
              (unsigned long long)l0_slot, (unsigned long long)l2_slot);
        return NULL;
    }

    UINT64 index = arm64_user_l3_next_index[l0_slot][l2_slot]++;
    UINT64 *new_table = arm64_user_l3_tables[l0_slot][l2_slot][index];
    RtlZeroMemory(new_table, PAGE_SIZE);
    pte_write(&l2_table[l2_index], (VA_TO_PA(new_table) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
    return new_table;
}

/* ---------- Page-table construction ---------- */

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs)
{
    UINT64 end = va + size;

    /* Align to 4K */
    va &= ~(PAGE_SIZE - 1);
    pa &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    while (va < end)
    {
        UINT64 l0_idx = (va >> 39) & 0x1FF;
        UINT64 l1_idx = (va >> 30) & 0x1FF;
        UINT64 l2_idx = (va >> 21) & 0x1FF;
        UINT64 l3_idx = (va >> 12) & 0x1FF;
        UINT64 *l0_table, *l1_table;
        UINT64 remaining = end - va;
        BOOLEAN is_kernel = (va >= ARM64_KSEG0_BASE);

        if (is_kernel) {
            l0_table = arm64_kernel_l0_table;
            if (l0_idx < ARM64_KSEG0_L0_INDEX ||
                l0_idx >= (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
            {
                return FALSE;
            }
        } else {
            l0_table = arm64_l0_page_table;
            if (l0_idx >= ARM64_USER_L1_TABLES)
            {
                return FALSE;
            }
        }

        if (is_kernel) {
            UINT64 slot = l0_idx - ARM64_KSEG0_L0_INDEX;
            if (!DESC_VALID(l0_table[l0_idx])) {
                pte_write(&l0_table[l0_idx],
                          (VA_TO_PA(&arm64_kernel_l1_tables[slot][0]) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
            }
            l1_table = arm64_kernel_l1_tables[slot];
        } else {
            if (!DESC_VALID(l0_table[l0_idx])) {
                pte_write(&l0_table[l0_idx],
                          (VA_TO_PA(&arm64_l1_page_tables[l0_idx][0]) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
            }
            l1_table = arm64_l1_page_tables[l0_idx];
        }

        /* 1GiB block if possible */
        if ((va % 0x40000000ULL) == 0 && (pa % 0x40000000ULL) == 0 && remaining >= 0x40000000ULL)
        {
            if (!DESC_IS_TABLE(l1_table[l1_idx])) {
                pte_replace_break_before_make(&l1_table[l1_idx], pa | PTE_TYPE_VALID | PTE_TYPE_BLOCK | attrs);
                va += 0x40000000ULL;
                pa += 0x40000000ULL;
                continue;
            }
        }

        /* 2MiB block if possible */
        if ((va & 0x1FFFFFULL) == 0 && (pa & 0x1FFFFFULL) == 0 && remaining >= 0x200000ULL)
        {
            UINT64 l0_slot = is_kernel ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx;
            UINT64 *l2_table_ptr = ensure_l2_table(is_kernel, l0_slot, l1_table, l1_idx);
            if (!l2_table_ptr)
                return FALSE;

            if (!DESC_IS_TABLE(l2_table_ptr[l2_idx])) {
                pte_replace_break_before_make(&l2_table_ptr[l2_idx], pa | PTE_TYPE_VALID | PTE_TYPE_BLOCK | attrs);
                va += 0x200000ULL;
                pa += 0x200000ULL;
                continue;
            }
        }

        /* 4KiB page */
        {
            UINT64 l0_slot = is_kernel ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx;
            UINT64 *l2_table_ptr = ensure_l2_table(is_kernel, l0_slot, l1_table, l1_idx);
            if (!l2_table_ptr)
                return FALSE;

            UINT64 *l3_table_ptr = ensure_l3_table(is_kernel, l0_slot, l2_table_ptr, l2_idx);
            if (!l3_table_ptr)
                return FALSE;

            pte_replace_break_before_make(&l3_table_ptr[l3_idx],
                      (pa & ~0xFFFULL) | PTE_TYPE_VALID | PTE_TYPE_PAGE | attrs);
        }

        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    /* Global TLB invalidate for simplicity (safe during boot) */
    TLBI_VMALLE1IS();
    ARM64_DSB_ISH();
    ARM64_ISB();
    /* Conservative I-cache maintenance after creating new mappings (bootloader-safe) */
    __asm__ volatile("ic iallu" ::: "memory");
    ARM64_DSB_ISH();
    ARM64_ISB();
    return TRUE;
}

static VOID setup_pgtables(VOID)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;

    TRACE("ARM64:  setup_pgtables BEGIN\n");

    RtlZeroMemory(arm64_l0_page_table, sizeof(arm64_l0_page_table));
    RtlZeroMemory(arm64_l1_page_tables, sizeof(arm64_l1_page_tables));
    RtlZeroMemory(arm64_kernel_l0_table, sizeof(arm64_kernel_l0_table));
    RtlZeroMemory(arm64_kernel_l1_tables, sizeof(arm64_kernel_l1_tables));
    RtlZeroMemory(arm64_kernel_l2_tables, sizeof(arm64_kernel_l2_tables));
    RtlZeroMemory(arm64_kernel_l3_tables, sizeof(arm64_kernel_l3_tables));
    RtlZeroMemory(arm64_user_l2_tables, sizeof(arm64_user_l2_tables));
    RtlZeroMemory(arm64_user_l3_tables, sizeof(arm64_user_l3_tables));
    RtlZeroMemory(arm64_l2_next_index, sizeof(arm64_l2_next_index));
    RtlZeroMemory(arm64_l3_next_index, sizeof(arm64_l3_next_index));
    RtlZeroMemory(arm64_user_l2_next_index, sizeof(arm64_user_l2_next_index));
    RtlZeroMemory(arm64_user_l3_next_index, sizeof(arm64_user_l3_next_index));

    /* TTBR0 L0 */
    for (i = 0; i < ARM64_USER_L1_TABLES; i++) {
        pte_write(&arm64_l0_page_table[i],
                  (VA_TO_PA(&arm64_l1_page_tables[i][0]) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
    }

    /* TTBR1 L0 for KSEG0 */
    for (i = 0; i < ARM64_KERNEL_L1_TABLES; i++) {
        UINT64 l0_index = ARM64_KSEG0_L0_INDEX + i;
        if (l0_index < ARRAYSIZE(arm64_kernel_l0_table)) {
            pte_write(&arm64_kernel_l0_table[l0_index],
                      (VA_TO_PA(&arm64_kernel_l1_tables[i][0]) | PTE_TYPE_VALID | PTE_TYPE_TABLE));
            TRACE("ARM64: Set L0[%lld] = L1 table %d at PA 0x%llx\n",
                  l0_index, i, (unsigned long long)VA_TO_PA(&arm64_kernel_l1_tables[i][0]));
        }
    }

    /* Identity + selected kernel mappings from UEFI map */
    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 phys_start = (UINT64)MemoryMap[i].BasePage * PAGE_SIZE;
            UINT64 size       = (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;
            UINT64 attrs;

            switch (MemoryMap[i].MemoryType) {
                case LoaderFirmwarePermanent:
                case LoaderFirmwareTemporary:
                    attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_DEVICE_nGnRnE) |
                            PTE_BLOCK_OUTER_SHARE | PTE_BLOCK_AF |
                            PTE_BLOCK_PXN | PTE_BLOCK_UXN;
                    break;
                case LoaderFree:
                case LoaderLoadedProgram:
                case LoaderOsloaderHeap:
                case LoaderOsloaderStack:
                default:
                    attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                            PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF;
                    break;
            }

            /* Identity map in TTBR0 */
            if (!map_region_hierarchical(phys_start, phys_start, size, attrs)) {
                ERR("ARM64: identity hierarchical map failed at PA=0x%llx size=0x%llx\n", phys_start, size);
                return;
            }

            /* Map loader-critical ranges into TTBR1 @ KSEG0 */
            {
                UINT64 kernel_va   = ARM64_KSEG0_BASE | ((UINT64)MemoryMap[i].BasePage * PAGE_SIZE);
                UINT64 kernel_size = (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;

                if (MemoryMap[i].MemoryType == LoaderLoadedProgram ||
                    MemoryMap[i].MemoryType == LoaderOsloaderHeap ||
                    MemoryMap[i].MemoryType == LoaderOsloaderStack ||
                    MemoryMap[i].MemoryType == LoaderMemoryData ||
                    MemoryMap[i].MemoryType == LoaderRegistryData ||
                    MemoryMap[i].MemoryType == LoaderSystemBlock)
                {
                    (void)map_region_hierarchical(kernel_va,
                                                  (UINT64)MemoryMap[i].BasePage * PAGE_SIZE,
                                                  kernel_size,
                                                  attrs);
                    /* Errors here are non-fatal in bootloader context; already logged by mapper */
                }
            }
        }
    }

    TRACE("ARM64:  setup_pgtables END\n");
}

static VOID ensure_page_tables_initialized(VOID)
{
    if (page_tables_initialized)
        return;

    setup_pgtables();
    page_tables_initialized = TRUE;
}

/* ---------- Public MMU control ---------- */

VOID Arm64InitializeMMU(VOID)
{
    UINT64 tcr, sctlr;
    UINT64 ips, va_bits;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    TRACE("ARM64: Initializing MMU (EL%d)\n", el);

    ensure_page_tables_initialized();

    tcr = get_tcr(&ips, &va_bits);
    if (!tcr) {
        ERR("ARM64: TCR composition failed (granule unsupported?)\n");
        return;
    }

    TRACE("ARM64: Using %llu-bit VA, IPS=%llu\n", va_bits, ips);

    set_ttbr_tcr_mair(el,
                      (UINT64)arm64_l0_page_table,
                      (UINT64)arm64_kernel_l0_table,
                      tcr,
                      MEMORY_ATTRIBUTES);

    /* Enable MMU + caches */
    if (el == 1 || el12) {
        if (el12) __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
        else      __asm__ volatile("mrs %0, sctlr_el1"          : "=r"(sctlr));
        sctlr |= SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I | SCTLR_EL1_SA;
        if (el12) __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr) : "memory");
        else      __asm__ volatile("msr sctlr_el1, %0"           :: "r"(sctlr) : "memory");
    } else if (el == 2) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
    } else if (el == 3) {
        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
    }
    ARM64_ISB();

    mmu_enabled = TRUE;
    identity_mapping_enabled = TRUE;

    TRACE("ARM64: MMU enabled\n");
}

VOID Arm64SetupKernelHandoffMMU(VOID)
{
    UINT64 tcr, ips, va_bits;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    TRACE("ARM64: Setting up kernel handoff MMU (EL%d)\n", el);

    ensure_page_tables_initialized();

    tcr = get_tcr(&ips, &va_bits);
    if (!tcr) {
        ERR("ARM64: handoff TCR failed\n");
        return;
    }

    UINT64 phys_ttbr0 = VA_TO_PA((UINT64)arm64_l0_page_table);
    UINT64 phys_ttbr1 = VA_TO_PA((UINT64)arm64_kernel_l0_table);
    BOOLEAN reprogram_ttbrs = TRUE;

    /* CRITICAL: Map kernel and loader regions BEFORE switching TTBR1! */
    {
        TRACE("ARM64: Pre-mapping kernel/loader regions in new page tables\n");

        /* Map the kernel and drivers region (256MB from 0x40000000-0x50000000) */
        /* This covers where ReactOS kernel and drivers are loaded */
        UINT64 kernel_region_pa = 0x40000000ULL;
        UINT64 kernel_region_va = ARM64_KSEG0_BASE + kernel_region_pa;
        UINT64 kernel_region_size = 0x10000000ULL; /* 256MB */

        UartPuts("ARM64: Mapping kernel region PA 0x");
        UartPutHex64(kernel_region_pa);
        UartPuts(" -> VA 0x");
        UartPutHex64(kernel_region_va);
        UartPuts(" size 0x");
        UartPutHex64(kernel_region_size);
        UartPuts("\n");

        /* Map with executable permissions */
        UINT64 attrs = PTE_BLOCK_MEMTYPE(ARM64_MT_NORMAL) |
                       PTE_BLOCK_INNER_SHARE |
                       PTE_BLOCK_AF;
        /* Note: PXN/UXN are NOT set, allowing execution */

        if (!map_region_hierarchical(kernel_region_va, kernel_region_pa,
                                    kernel_region_size, attrs))
        {
            ERR("ARM64: CRITICAL - Failed to map kernel region!\n");
            UartPuts("ARM64: ERROR - Failed to map kernel region!\n");
            return;
        }

        TRACE("ARM64: Kernel region mapped successfully\n");
        UartPuts("ARM64: Kernel region mapped\n");

        /* Also map the loader itself both identity and in kernel space */
        UINT64 loader_region_pa = 0x13e000000ULL;
        UINT64 loader_region_size = 0x02000000ULL; /* 32MB should cover loader + data */

        /* First, identity map for current execution */
        UartPuts("ARM64: Identity mapping loader region PA 0x");
        UartPutHex64(loader_region_pa);
        UartPuts("\n");

        if (!map_region_hierarchical(loader_region_pa, loader_region_pa,
                                    loader_region_size, attrs))
        {
            TRACE("ARM64: Warning - loader identity mapping failed (may be out of range)\n");
        }

        /* Also map loader into kernel space for transition */
        UINT64 loader_kernel_va = ARM64_KSEG0_BASE + loader_region_pa;
        UartPuts("ARM64: Mapping loader to kernel VA 0x");
        UartPutHex64(loader_kernel_va);
        UartPuts("\n");

        if (!map_region_hierarchical(loader_kernel_va, loader_region_pa,
                                    loader_region_size, attrs))
        {
            TRACE("ARM64: Warning - loader kernel mapping failed\n");
        }
        else
        {
            TRACE("ARM64: Loader mapped to kernel space\n");
        }
    }

    /* Debug: Verify kernel mapping */
    {
        TRACE("ARM64: Verifying kernel mapping in new page tables\n");

        UINT64 kernel_va = 0xFFFF800042160800ULL;
        UINT64 l0_idx = (kernel_va >> 39) & 0x1FF;
        UINT64 l0_entry = arm64_kernel_l0_table[l0_idx];
        TRACE("ARM64: Kernel VA 0x%llx L0[%lld]=0x%llx (valid=%d)\n",
              kernel_va, l0_idx, l0_entry, (l0_entry & PTE_TYPE_VALID) ? 1 : 0);

        if (l0_entry & PTE_TYPE_VALID)
        {
            /* Check L1 */
            UINT64 l1_idx = (kernel_va >> 30) & 0x1FF;
            UINT64 *l1_table = (UINT64 *)PA_TO_VA(l0_entry & ~0xFFFULL);
            UINT64 l1_entry = l1_table[l1_idx];
            TRACE("ARM64: L1[%lld]=0x%llx (valid=%d)\n",
                  l1_idx, l1_entry, (l1_entry & PTE_TYPE_VALID) ? 1 : 0);
        }
    }

    {
        UINT64 pc_va = (UINT64)(uintptr_t)&Arm64SetupKernelHandoffMMU;
        TRACE("ARM64: Debug lookup for current PC VA=0x%llx\n", (unsigned long long)pc_va);
        debug_dump_static_mapping(pc_va);
        if (pc_va < ARM64_KSEG0_BASE)
        {
            UINT64 kva = pc_va + ARM64_KSEG0_BASE;
            TRACE("ARM64: Debug lookup for mirrored KSEG0 VA=0x%llx\n",
                  (unsigned long long)kva);
            debug_dump_static_mapping(kva);
        }
        UINT64 sp;
        __asm__ volatile("mov %0, sp" : "=r"(sp));
        TRACE("ARM64: Debug lookup for current SP VA=0x%llx\n", (unsigned long long)sp);
        debug_dump_static_mapping(sp);
        if (sp < ARM64_KSEG0_BASE)
        {
            UINT64 sp_kva = sp + ARM64_KSEG0_BASE;
            TRACE("ARM64: Debug lookup for mirrored SP VA=0x%llx\n",
                  (unsigned long long)sp_kva);
            debug_dump_static_mapping(sp_kva);
        }
    }

    /* Ensure the loader stack has a higher-half mapping before TTBR1 is used */
    {
        UINT64 sp_value;
        __asm__ volatile("mov %0, sp" : "=r"(sp_value));
        UINT64 sp_page = sp_value & ~(PAGE_SIZE - 1ULL);
        UINT64 sp_kva = ARM64_KSEG0_BASE | sp_page;
        UINT64 sp_phys = VA_TO_PA(sp_page);
        ULONG stack_attrs = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_UXN | ARM64_MAP_ATTR_PXN;

        TRACE("ARM64: Pre-mapping loader stack page SP=0x%llx -> KVA=0x%llx PA=0x%llx\n",
              (unsigned long long)sp_value,
              (unsigned long long)sp_kva,
              (unsigned long long)sp_phys);

        if (!Arm64MapVirtualMemory(sp_kva, sp_phys, PAGE_SIZE, stack_attrs))
        {
            TRACE("ARM64: WARNING: Failed to map stack page into TTBR1\n");
        }
        else
        {
            debug_dump_static_mapping(sp_kva | (sp_value & (PAGE_SIZE - 1ULL)));
        }
    }

    if (el == 1 || el12) {
        /* Read current TCR to preserve its configuration */
        UINT64 current_tcr;
        if (el12)
            __asm__ volatile("mrs %0, " TCR_EL12_SYSREG : "=r"(current_tcr));
        else
            __asm__ volatile("mrs %0, tcr_el1" : "=r"(current_tcr));

        TRACE("ARM64: Current TCR = 0x%llx\n", current_tcr);

        /* Use existing TCR but fix T1SZ to 16 for 48-bit kernel addresses */
        tcr = current_tcr;
        /* Clear EPD bits to enable both TTBR0 & TTBR1 */
        tcr &= ~(TCR_EPD0 | TCR_EPD1);
        /* Clear and set T1SZ to 16 (48-bit addresses) */
        tcr &= ~(0x3FULL << 16);  /* Clear T1SZ bits [21:16] */
        tcr |= (16ULL << 16);     /* Set T1SZ = 16 */

        TRACE("ARM64: Modified TCR (EPD cleared, T1SZ=16) = 0x%llx\n", tcr);
        UINT64 ttbr1_current;
        BOOLEAN need_full_reprogram = reprogram_ttbrs;
        __asm__ volatile("mrs %0, ttbr1_el1" : "=r" (ttbr1_current));

        if (!reprogram_ttbrs || ttbr1_current == phys_ttbr1)
        {
            TRACE("ARM64: Skipping EL1 TTBR reprogramming (reuse existing configuration)\n");
            need_full_reprogram = FALSE;
        }

        if (need_full_reprogram)
        {
            if (ttbr1_current == 0 && !el12)
            {
                TRACE("ARM64: TTBR1 currently zero, performing full reprogram to 0x%llx\n",
                      (unsigned long long)phys_ttbr1);
            }
            UINT64 sctlr_saved;

            if (el12)
                __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr_saved));
            else
                __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_saved));

            /* Get current PC and ensure it's mapped before disabling MMU */
            {
                UINT64 current_pc;
                __asm__ volatile("adr %0, ." : "=r"(current_pc));
                UartPuts("ARM64: Pre-disable PC: 0x");
                UartPutHex64(current_pc);
                UartPuts("\n");

                /* Map FreeLoader region into TTBR1 before switch */
                UINT64 loader_base = current_pc & ~(UINT64)0xFFFFFULL; /* Align to 1MB */
                UINT64 loader_kva = ARM64_KSEG0_BASE | loader_base;
                UINT64 loader_size = 0x200000; /* Map 2MB for safety */

                UartPuts("ARM64: Pre-mapping loader region PA=0x");
                UartPutHex64(loader_base);
                UartPuts(" -> KVA=0x");
                UartPutHex64(loader_kva);
                UartPuts(" size=0x");
                UartPutHex64(loader_size);
                UartPuts("\n");

                if (!Arm64MapVirtualMemory(loader_kva, loader_base, loader_size,
                                          ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_EXECUTE))
                {
                    UartPuts("ARM64: ERROR: Failed to pre-map loader region - CRITICAL!\n");
                }
                else
                {
                    UartPuts("ARM64: Loader region pre-mapped successfully\n");
                }
            }

            /* Try a different approach - keep MMU enabled, just update TTBRs */
            UartPuts("ARM64: Updating TTBRs with MMU enabled\n");

            /* Don't disable MMU - this avoids the risky transition */
            /* UINT64 sctlr_tmp = sctlr_saved & ~(SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I);
            if (el12)
                __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr_tmp) : "memory");
            else
                __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_tmp) : "memory");
            ARM64_ISB();
            TRACE("ARM64: EL1 MMU temporarily disabled for TTBR switch\n"); */

            ARM64_DSB_ISH();
            TLBI_VMALLE1IS();
            ARM64_DSB_ISH();
            ARM64_ISB();
            TRACE("ARM64: EL1 TLB invalidated before TTBR update\n");

            /* CRITICAL: We can only update TTBR1, not TTBR0, while the MMU is enabled */
            UartPuts("ARM64: Writing TTBR1=0x");
            UartPutHex64(phys_ttbr1);
            UartPuts("\n");

            if (el12) {
                /* Don't change TTBR0 - we're running from it! */
                /* __asm__ volatile("msr " TTBR0_EL12_SYSREG ", %0" :: "r" (phys_ttbr0) : "memory"); */
                __asm__ volatile("msr " TTBR1_EL12_SYSREG ", %0" :: "r" (phys_ttbr1) : "memory");
                /* We can update TCR since we're using the same configuration */
                __asm__ volatile("msr " TCR_EL12_SYSREG   ", %0" :: "r" (tcr) : "memory");
                /* MAIR should be safe to update */
                __asm__ volatile("msr " MAIR_EL12_SYSREG  ", %0" :: "r" (MEMORY_ATTRIBUTES) : "memory");
            } else {
                /* Don't change TTBR0 - we're running from it! */
                /* __asm__ volatile("msr ttbr0_el1, %0" :: "r" (phys_ttbr0) : "memory"); */
                __asm__ volatile("msr ttbr1_el1, %0" :: "r" (phys_ttbr1) : "memory");
                ARM64_ISB();

                /* Verify TTBR1 was written */
                UINT64 ttbr1_verify;
                __asm__ volatile("mrs %0, ttbr1_el1" : "=r" (ttbr1_verify));
                UartPuts("ARM64: TTBR1 written, readback=0x");
                UartPutHex64(ttbr1_verify);
                UartPuts("\n");

                /* Debug: Walk the page table using physical addresses to verify kernel mapping */
                {
                    UartPuts("ARM64: Verifying kernel page table after TTBR1 update\n");
                    UINT64 kernel_va = 0xFFFF800042160800ULL;
                    UINT64 l0_idx = (kernel_va >> 39) & 0x1FF;

                    /* Access L0 using physical address from TTBR1 */
                    UINT64 *l0_table_phys = (UINT64 *)(uintptr_t)(ttbr1_verify & ~0xFFFULL);
                    UINT64 *l0_table_va = (UINT64 *)PA_TO_VA((UINT64)(uintptr_t)l0_table_phys);

                    UartPuts("  TTBR1 physical L0 @ 0x");
                    UartPutHex64((UINT64)(uintptr_t)l0_table_phys);
                    UartPuts(", VA @ 0x");
                    UartPutHex64((UINT64)(uintptr_t)l0_table_va);
                    UartPuts("\n");

                    UINT64 l0_entry = l0_table_va[l0_idx];
                    UartPuts("  L0[");
                    UartPutHex32(l0_idx);
                    UartPuts("] = 0x");
                    UartPutHex64(l0_entry);

                    if (l0_entry & 0x1) {
                        UartPuts(" (valid table)\n");

                        /* Check L1 */
                        UINT64 l1_idx = (kernel_va >> 30) & 0x1FF;
                        UINT64 *l1_table_phys = (UINT64 *)(uintptr_t)(l0_entry & 0xFFFFFFFFF000ULL);
                        UINT64 *l1_table_va = (UINT64 *)PA_TO_VA((UINT64)(uintptr_t)l1_table_phys);

                        UartPuts("  L1 physical @ 0x");
                        UartPutHex64((UINT64)(uintptr_t)l1_table_phys);
                        UartPuts(", VA @ 0x");
                        UartPutHex64((UINT64)(uintptr_t)l1_table_va);
                        UartPuts("\n");

                        UINT64 l1_entry = l1_table_va[l1_idx];
                        UartPuts("  L1[");
                        UartPutHex32(l1_idx);
                        UartPuts("] = 0x");
                        UartPutHex64(l1_entry);

                        if (l1_entry & 0x1) {
                            UartPuts(" (valid)\n");
                        } else {
                            UartPuts(" (INVALID - this is the problem!)\n");
                        }
                    } else {
                        UartPuts(" (INVALID - this is the problem!)\n");
                    }
                }

                /* We can update TCR since we're using the same configuration */
                UartPuts("ARM64: Writing TCR=0x");
                UartPutHex64(tcr);
                UartPuts("\n");
                __asm__ volatile("msr tcr_el1, %0"   :: "r" (tcr) : "memory");
                ARM64_ISB();

                /* Verify TCR was written */
                UINT64 tcr_verify;
                __asm__ volatile("mrs %0, tcr_el1" : "=r" (tcr_verify));
                UartPuts("ARM64: TCR written, readback=0x");
                UartPutHex64(tcr_verify);

                /* Decode T1SZ for debugging */
                UINT64 t1sz = (tcr_verify >> 16) & 0x3F;
                UartPuts(" (T1SZ=");
                UartPutHex32((UINT32)t1sz);
                UartPuts(")\n");

                /* MAIR should be safe to update */
                __asm__ volatile("msr mair_el1, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
                UartPuts("ARM64: MAIR written\n");
            }
            TRACE("ARM64: EL1 TTBR1/MAIR written (TCR kept unchanged)\n");

            ARM64_ISB();
            TLBI_VMALLE1IS();
            ARM64_DSB_ISH();
            ARM64_ISB();
            TRACE("ARM64: EL1 TLB invalidated after TTBR update\n");

            /* Since we're keeping MMU enabled, no need to restore SCTLR */
            UartPuts("ARM64: TTBRs updated with MMU still enabled\n");

            /* Verify we can still execute by reading back SCTLR */
            {
                UINT64 sctlr_verify;
                if (el12)
                    __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr_verify));
                else
                    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_verify));

                UartPuts("ARM64: SCTLR verified read back: 0x");
                UartPutHex64(sctlr_verify);
                UartPuts("\n");

                /* Also verify PC is still accessible */
                UINT64 test_pc;
                __asm__ volatile("adr %0, ." : "=r"(test_pc));
                UartPuts("ARM64: PC still accessible at: 0x");
                UartPutHex64(test_pc);
                UartPuts("\n");
            }

            TRACE("ARM64: EL1 MMU restored\n");
            TRACE("ARM64: TTBRs programmed (EL1%s) TTBR0=0x%llx TTBR1=0x%llx\n",
                  el12 ? "+EL12" : "",
                  (unsigned long long)phys_ttbr0,
                  (unsigned long long)phys_ttbr1);
        }


        /* Example: map a 1GiB kernel window (if desired) */
        {
            UINT64 kernel_phys_base = 0x40000000ULL; /* example alignment */
            UINT64 kernel_virt_base = ARM64_KSEG0_BASE | kernel_phys_base;

            UINT64 kernel_l0_idx = (kernel_virt_base >> 39) & 0x1FF;
            UINT64 kernel_l1_idx = (kernel_virt_base >> 30) & 0x1FF;

            if (kernel_l0_idx >= ARM64_KSEG0_L0_INDEX &&
                kernel_l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
            {
                UINT64 slot = kernel_l0_idx - ARM64_KSEG0_L0_INDEX;
                UINT64 newval = (kernel_phys_base |
                                 PTE_TYPE_VALID | PTE_TYPE_BLOCK |
                                 PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                                 PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF);
                pte_replace_break_before_make(&arm64_kernel_l1_tables[slot][kernel_l1_idx], newval);

                /* Per-VA flush (all ASIDs) */
                tlbi_vaae1is_by_va(kernel_virt_base);
                ARM64_DSB_ISH();
                ARM64_ISB();
                /* Conservatively sync I-cache if this mapping may become executable soon */
                __asm__ volatile("ic iallu" ::: "memory");
                ARM64_DSB_ISH();
                ARM64_ISB();
            }
        }
        TRACE("ARM64: EL1 handoff tables primed\n");
    } else if (el == 2) {
        UINT64 sctlr;
        tcr = TCR_EL2_RSVD | (ips << 16) |
              TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;

        /* Disable MMU for safe reprogramming at EL2 */
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        sctlr &= ~(1ULL << 0);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
        ARM64_ISB();

        ARM64_DSB_ISH();
            __asm__ volatile("msr ttbr0_el2, %0" :: "r" (phys_ttbr0) : "memory");
            __asm__ volatile("msr tcr_el2, %0"   :: "r" (tcr) : "memory");
            __asm__ volatile("msr mair_el2, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
            ARM64_ISB();

            TLBI_ALLE2IS();
            ARM64_DSB_ISH();
            ARM64_ISB();
        TRACE("ARM64: TTBRs programmed (EL2) TTBR0=0x%llx\n",
              (unsigned long long)phys_ttbr0);

        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
        ARM64_ISB();
    } else if (el == 3) {
        UINT64 sctlr;
        tcr = TCR_EL3_RSVD | (ips << 16) |
              TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;

        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
        sctlr &= ~(1ULL << 0);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
        ARM64_ISB();

        ARM64_DSB_ISH();
        __asm__ volatile("msr ttbr0_el3, %0" :: "r" (phys_ttbr0) : "memory");
        __asm__ volatile("msr tcr_el3, %0"   :: "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el3, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
        ARM64_ISB();

        TLBI_ALLE3IS();
        ARM64_DSB_ISH();
        ARM64_ISB();
        TRACE("ARM64: TTBRs programmed (EL3) TTBR0=0x%llx\n",
              (unsigned long long)phys_ttbr0);

        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
        ARM64_ISB();
    }

    mmu_enabled = TRUE;
    identity_mapping_enabled = TRUE;

    TRACE("ARM64: Kernel handoff MMU configuration complete\n");
}

/* ---------- Mapping API (1GiB blocks) ---------- */

BOOLEAN Arm64MapVirtualMemory(ULONGLONG VirtualAddress,
                              ULONGLONG PhysicalAddress,
                              ULONGLONG Size,
                              ULONG Attributes)
{
    UINT64 attrs;
    BOOLEAN executable = (Attributes & ARM64_MAP_ATTR_EXECUTE) != 0;
    ULONG mem_type = Attributes & ARM64_MAP_ATTR_TYPE_MASK;

    ensure_page_tables_initialized();

    /* Too verbose - disable for now
    if (VirtualAddress >= ARM64_KSEG0_BASE) {
        TRACE("ARM64: Map VA=0x%016llx -> PA=0x%016llx, Size=0x%016llx, Attr=0x%lx\n",
              VirtualAddress, PhysicalAddress, Size, Attributes);
    } */

    if (((VirtualAddress | PhysicalAddress | Size) & (PAGE_SIZE - 1)) != 0)
    {
        TRACE("ARM64: Map requires 4KiB alignment\n");
        return FALSE;
    }

    attrs = PTE_BLOCK_MEMTYPE(mem_type) | PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF;
    if (!executable)
        attrs |= PTE_BLOCK_PXN | PTE_BLOCK_UXN;

    if (!map_region_hierarchical(VirtualAddress, PhysicalAddress, Size, attrs))
    {
        TRACE("ARM64: map_region_hierarchical failed for VA 0x%llx size 0x%llx\n",
              VirtualAddress, Size);
        return FALSE;
    }

    TLBI_VMALLE1IS();
    ARM64_DSB_ISH();
    ARM64_ISB();
    return TRUE;
}

BOOLEAN Arm64UnmapVirtualMemory(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    UINT64 va = VirtualAddress;
    UINT64 end = VirtualAddress + Size;

    TRACE("ARM64: Unmap VA=0x%016llx, Size=0x%016llx\n", VirtualAddress, Size);

    ensure_page_tables_initialized();

    if (((va | Size) & (PAGE_SIZE - 1)) != 0)
    {
        TRACE("ARM64: Unmap requires 4KiB alignment\n");
        return FALSE;
    }

    while (va < end)
    {
        UINT64 l0_idx = (va >> 39) & 0x1FF;
        UINT64 l1_idx = (va >> 30) & 0x1FF;
        UINT64 l2_idx = (va >> 21) & 0x1FF;
        UINT64 l3_idx = (va >> 12) & 0x1FF;
        BOOLEAN kernel_va = (va >= ARM64_KSEG0_BASE);
        UINT64 *l1_table;

        if (kernel_va)
        {
            if (l0_idx < ARM64_KSEG0_L0_INDEX ||
                l0_idx >= (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
            {
                TRACE("ARM64: Unmap VA 0x%llx outside kernel space\n", va);
                return FALSE;
            }
            l1_table = arm64_kernel_l1_tables[l0_idx - ARM64_KSEG0_L0_INDEX];
        }
        else
        {
            if (l0_idx >= ARM64_USER_L1_TABLES)
            {
                TRACE("ARM64: Unmap VA 0x%llx outside user static range\n", va);
                return FALSE;
            }
            l1_table = arm64_l1_page_tables[l0_idx];
        }

        UINT64 l1_entry = l1_table[l1_idx];
        if (DESC_IS_BLOCK(l1_entry))
        {
            if ((va & ARM64_BLOCK_MASK_1G) != 0 || (end - va) < ARM64_BLOCK_SIZE_1G)
            {
                TRACE("ARM64: Cannot partially unmap 1GiB block at VA 0x%llx\n", va);
                return FALSE;
            }
            pte_replace_break_before_make(&l1_table[l1_idx], 0);
            va += ARM64_BLOCK_SIZE_1G;
            continue;
        }

        if (!DESC_IS_TABLE(l1_entry))
        {
            TRACE("ARM64: No mapping found at L1 for VA 0x%llx\n", va);
            return FALSE;
        }

        UINT64 *l2_table = (UINT64 *)PA_TO_VA(l1_entry & ~0xFFFULL);
        UINT64 l2_entry = l2_table[l2_idx];
        if (DESC_IS_BLOCK(l2_entry))
        {
            if ((va & ARM64_BLOCK_MASK_2M) != 0 || (end - va) < ARM64_BLOCK_SIZE_2M)
            {
                TRACE("ARM64: Cannot partially unmap 2MiB block at VA 0x%llx\n", va);
                return FALSE;
            }
            pte_replace_break_before_make(&l2_table[l2_idx], 0);
            va += ARM64_BLOCK_SIZE_2M;
            continue;
        }

        if (!DESC_IS_TABLE(l2_entry))
        {
            TRACE("ARM64: No mapping found at L2 for VA 0x%llx\n", va);
            return FALSE;
        }

        UINT64 *l3_table = (UINT64 *)PA_TO_VA(l2_entry & ~0xFFFULL);
        UINT64 pte = l3_table[l3_idx];
        if (!DESC_IS_PAGE(pte))
        {
            TRACE("ARM64: No 4KiB mapping at VA 0x%llx\n", va);
            return FALSE;
        }

        pte_replace_break_before_make(&l3_table[l3_idx], 0);
        va += PAGE_SIZE;
    }

    TLBI_VMALLE1IS();
    ARM64_DSB_ISH();
    ARM64_ISB();
    return TRUE;
}

/* ---------- Address translation helpers ---------- */

ULONGLONG Arm64GetPhysicalAddress(ULONGLONG VirtualAddress)
{
    UINT64 va = VirtualAddress;

    if (!mmu_enabled)
        return va;

    /* Fast path: identity for TTBR0 space */
    if (va < ARM64_KSEG0_BASE)
        return va;

    /* Full walk for TTBR1 KSEG0: L0 -> L1 -> (L2 -> L3) */
    UINT64 l0 = (va >> 39) & 0x1FF;
    if (l0 < ARM64_KSEG0_L0_INDEX || l0 >= (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
        return va; /* outside our static coverage */
    UINT64 slot = l0 - ARM64_KSEG0_L0_INDEX;

    UINT64 l1 = (va >> 30) & 0x1FF;
    UINT64 pte1 = arm64_kernel_l1_tables[slot][l1];
    if (DESC_IS_BLOCK(pte1)) {
        return (pte1 & ~0x3FFFFFFFULL) | (va & 0x3FFFFFFFULL); /* 1GiB */
    }
    if (!DESC_IS_TABLE(pte1)) return va;

    UINT64 *l2tbl = (UINT64 *)PA_TO_VA(pte1 & ~0xFFFULL);
    UINT64 l2 = (va >> 21) & 0x1FF;
    UINT64 pte2 = l2tbl[l2];
    if (DESC_IS_BLOCK(pte2)) {
        return (pte2 & ~0x1FFFFFULL) | (va & 0x1FFFFFULL);     /* 2MiB */
    }
    if (!DESC_IS_TABLE(pte2)) return va;

    UINT64 *l3tbl = (UINT64 *)PA_TO_VA(pte2 & ~0xFFFULL);
    UINT64 l3 = (va >> 12) & 0x1FF;
    UINT64 pte3 = l3tbl[l3];
    if (DESC_IS_PAGE(pte3)) {
        return (pte3 & ~0xFFFULL) | (va & 0xFFFULL);           /* 4KiB */
    }
    return va;
}

BOOLEAN Arm64IsMMUEnabled(VOID)
{
    return mmu_enabled;
}

VOID Arm64DisableMMU(VOID)
{
    ULONGLONG sctlr;

    if (!mmu_enabled) return;

    TRACE("ARM64: Disabling MMU\n");

    Arm64FlushDataCacheAll();
    Arm64InvalidateInstructionCacheAll();
    ARM64_DSB_ISH();
    ARM64_ISB();

    {
        int el = get_effective_el();
        BOOLEAN el12 = use_el12_registers();

        if (el == 1 || el12) {
            if (el12) __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
            else      __asm__ volatile("mrs %0, sctlr_el1"          : "=r"(sctlr));
            sctlr &= ~(SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I);
            if (el12) __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr) : "memory");
            else      __asm__ volatile("msr sctlr_el1, %0"           :: "r"(sctlr) : "memory");
        } else if (el == 2) {
            __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
        } else if (el == 3) {
            __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
        }
    }
    ARM64_ISB();

    mmu_enabled = FALSE;

    TRACE("ARM64: MMU disabled\n");
}

/* ---------- Attribute query ---------- */

ULONG Arm64GetMemoryAttributes(ULONGLONG Address)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;

    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 start = (UINT64)MemoryMap[i].BasePage * PAGE_SIZE;
            UINT64 end   = start + (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;
            if (Address >= start && Address < end) {
                switch (MemoryMap[i].MemoryType) {
                    case LoaderFirmwarePermanent:
                    case LoaderFirmwareTemporary:
                        return ARM64_MEM_ATTR_DEVICE_nGnRnE;
                    default:
                        return ARM64_MEM_ATTR_NORMAL_WB;
                }
            }
        }
    }
    return ARM64_MEM_ATTR_NORMAL_WB;
}

/* ---------- TLB range flush ---------- */

VOID Arm64FlushTlbRange(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    ULONGLONG end = VirtualAddress + Size;

    if (Size >= ARM64_BLOCK_SIZE_1G)
    {
        TLBI_VMALLE1IS();
    }
    else
    {
        for (ULONGLONG addr = (VirtualAddress & ~0xFFFULL);
             addr < end;
             addr += 0x1000ULL)
        {
            tlbi_vaae1is_by_va(addr);
        }
    }
    ARM64_DSB_ISH();
    ARM64_ISB();
}
static VOID debug_dump_static_mapping(UINT64 va)
{
    BOOLEAN kernel = (va >= ARM64_KSEG0_BASE);
    UINT64 *l0_table = kernel ? arm64_kernel_l0_table : arm64_l0_page_table;
    UINT64 l0_idx = (va >> 39) & 0x1FF;
    UINT64 l1_idx = (va >> 30) & 0x1FF;
    UINT64 l2_idx = (va >> 21) & 0x1FF;
    UINT64 l3_idx = (va >> 12) & 0x1FF;
    UINT64 entry;

    /* Enhanced UART debug for critical handoff debugging */
    UartPuts("ARM64: Page walk for VA 0x");
    UartPutHex64(va);
    UartPuts(" (");
    UartPuts(kernel ? "kernel" : "user");
    UartPuts("):\n");

    UartPuts("  L0 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l0_table);
    UartPuts(", idx=");
    UartPutHex32(l0_idx);

    entry = l0_table[l0_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L0 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L0[%llx] invalid (kernel=%d)\n",
              (unsigned long long)va, (unsigned long long)l0_idx, kernel);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L0 entry is leaf (not table) - unexpected\n");
        TRACE("ARM64: debug map VA=0x%llx L0[%llx]=0x%llx leaf (kernel=%d)\n",
              (unsigned long long)va, (unsigned long long)l0_idx,
              (unsigned long long)entry, kernel);
        return;
    }

    UINT64 *l1_table = (UINT64 *)PA_TO_VA(entry & ~0xFFFULL);
    UartPuts("  L1 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l1_table);
    UartPuts(", idx=");
    UartPutHex32(l1_idx);

    entry = l1_table[l1_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L1 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L1[%llx] invalid\n",
              (unsigned long long)va, (unsigned long long)l1_idx);
        return;
    }
    if (DESC_IS_BLOCK(entry)) {
        UartPuts("  L1 block mapping -> PA 0x");
        UartPutHex64(entry & 0xFFFFFFFFF000ULL);
        UartPuts("\n");
        TRACE("ARM64: debug map VA=0x%llx -> block L1[%llx]=0x%llx\n",
              (unsigned long long)va, (unsigned long long)l1_idx,
              (unsigned long long)entry);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L1 entry unexpected type\n");
        TRACE("ARM64: debug map VA=0x%llx L1[%llx]=0x%llx unexpected\n",
              (unsigned long long)va, (unsigned long long)l1_idx,
              (unsigned long long)entry);
        return;
    }

    UINT64 *l2_table = (UINT64 *)PA_TO_VA(entry & ~0xFFFULL);
    UartPuts("  L2 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l2_table);
    UartPuts(", idx=");
    UartPutHex32(l2_idx);

    entry = l2_table[l2_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L2 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L2[%llx] invalid\n",
              (unsigned long long)va, (unsigned long long)l2_idx);
        return;
    }
    if (DESC_IS_BLOCK(entry)) {
        UartPuts("  L2 block mapping -> PA 0x");
        UartPutHex64(entry & 0xFFFFFFFFF000ULL);
        UartPuts("\n");
        TRACE("ARM64: debug map VA=0x%llx -> block L2[%llx]=0x%llx\n",
              (unsigned long long)va, (unsigned long long)l2_idx,
              (unsigned long long)entry);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L2 entry unexpected type\n");
        TRACE("ARM64: debug map VA=0x%llx L2[%llx]=0x%llx unexpected\n",
              (unsigned long long)va, (unsigned long long)l2_idx,
              (unsigned long long)entry);
        return;
    }

    UINT64 *l3_table = (UINT64 *)PA_TO_VA(entry & ~0xFFFULL);
    UartPuts("  L3 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l3_table);
    UartPuts(", idx=");
    UartPutHex32(l3_idx);

    entry = l3_table[l3_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);

    if (DESC_VALID(entry)) {
        UartPuts(" -> PA 0x");
        UartPutHex64(entry & 0xFFFFFFFFF000ULL);
    } else {
        UartPuts(" INVALID");
    }
    UartPuts("\n");

    TRACE("ARM64: debug map VA=0x%llx L3[%llx]=0x%llx\n",
          (unsigned long long)va, (unsigned long long)l3_idx,
          (unsigned long long)entry);
}

VOID Arm64DebugDumpMapping(UINT64 VirtualAddress)
{
    debug_dump_static_mapping(VirtualAddress);
}
