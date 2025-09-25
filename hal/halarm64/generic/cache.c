/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Cache Management Primitives
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

static ULONG HalCacheLineSize = 64;        /* ARM64 cache line size */
static BOOLEAN HalCacheInitialized = FALSE;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Clean data cache range to Point of Coherency
 *
 * This function cleans the specified virtual address range in the data cache
 * to the Point of Coherency, ensuring writes are visible to other observers.
 */
VOID
NTAPI
HalCleanDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length)
{
    ULONG_PTR StartAddress, EndAddress, CurrentAddress;
    ULONG CacheLineSize = HalCacheLineSize;

    if (!VirtualAddress || Length == 0)
        return;

    /* Align start address down to cache line boundary */
    StartAddress = (ULONG_PTR)VirtualAddress & ~(CacheLineSize - 1);

    /* Calculate end address, aligned up to cache line boundary */
    EndAddress = ((ULONG_PTR)VirtualAddress + Length + CacheLineSize - 1) & ~(CacheLineSize - 1);

    /* Clean cache lines by virtual address to Point of Coherency */
    for (CurrentAddress = StartAddress; CurrentAddress < EndAddress; CurrentAddress += CacheLineSize)
    {
        __asm__ __volatile__ (
            "dc cvac, %0\n"     /* Clean by VA to Point of Coherency */
            :: "r"(CurrentAddress)
            : "memory"
        );
    }

    /* Data Synchronization Barrier to ensure completion */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Invalidate data cache range
 *
 * This function invalidates the specified virtual address range in the data cache,
 * discarding any cached data for those addresses.
 */
VOID
NTAPI
HalInvalidateDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length)
{
    ULONG_PTR StartAddress, EndAddress, CurrentAddress;
    ULONG CacheLineSize = HalCacheLineSize;

    if (!VirtualAddress || Length == 0)
        return;

    /* Align start address down to cache line boundary */
    StartAddress = (ULONG_PTR)VirtualAddress & ~(CacheLineSize - 1);

    /* Calculate end address, aligned up to cache line boundary */
    EndAddress = ((ULONG_PTR)VirtualAddress + Length + CacheLineSize - 1) & ~(CacheLineSize - 1);

    /* Invalidate cache lines by virtual address */
    for (CurrentAddress = StartAddress; CurrentAddress < EndAddress; CurrentAddress += CacheLineSize)
    {
        __asm__ __volatile__ (
            "dc ivac, %0\n"     /* Invalidate by VA to Point of Coherency */
            :: "r"(CurrentAddress)
            : "memory"
        );
    }

    /* Data Synchronization Barrier to ensure completion */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Clean and invalidate data cache range
 *
 * This function both cleans and invalidates the specified virtual address range
 * in the data cache, ensuring writes are flushed and cache lines are invalidated.
 */
VOID
NTAPI
HalFlushDcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length)
{
    ULONG_PTR StartAddress, EndAddress, CurrentAddress;
    ULONG CacheLineSize = HalCacheLineSize;

    if (!VirtualAddress || Length == 0)
        return;

    /* Align start address down to cache line boundary */
    StartAddress = (ULONG_PTR)VirtualAddress & ~(CacheLineSize - 1);

    /* Calculate end address, aligned up to cache line boundary */
    EndAddress = ((ULONG_PTR)VirtualAddress + Length + CacheLineSize - 1) & ~(CacheLineSize - 1);

    /* Clean and invalidate cache lines by virtual address */
    for (CurrentAddress = StartAddress; CurrentAddress < EndAddress; CurrentAddress += CacheLineSize)
    {
        __asm__ __volatile__ (
            "dc civac, %0\n"    /* Clean and Invalidate by VA to Point of Coherency */
            :: "r"(CurrentAddress)
            : "memory"
        );
    }

    /* Data Synchronization Barrier to ensure completion */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Invalidate instruction cache range
 *
 * This function invalidates instruction cache for the specified virtual address range.
 */
VOID
NTAPI
HalInvalidateIcacheRange(
    IN PVOID VirtualAddress,
    IN ULONG Length)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(Length);

    /* ARM64 doesn't support per-address I-cache invalidation by VA
     * We need to invalidate all instruction caches to Point of Unification */
    __asm__ __volatile__ (
        "ic iallu\n"        /* Invalidate all instruction caches to PoU */
        "dsb sy\n"          /* Data Synchronization Barrier */
        "isb\n"             /* Instruction Synchronization Barrier */
        ::: "memory"
    );
}

/*
 * @brief Clean entire data cache by set/way
 *
 * This function performs a comprehensive data cache clean operation
 * using the set/way method for all cache levels.
 */
VOID
NTAPI
HalCleanEntireDataCache(VOID)
{
    ULONG64 Clidr, CurrentLevel, CacheType;
    ULONG64 Ccsidr, NumSets, NumWays, SetShift, WayShift;
    ULONG64 Set, Way, SetWayValue;

    /* Read Cache Level ID Register to determine cache levels */
    __asm__ __volatile__ (
        "mrs %0, CLIDR_EL1\n"
        : "=r"(Clidr)
    );

    /* Clean each cache level */
    for (CurrentLevel = 0; CurrentLevel < 7; CurrentLevel++)
    {
        /* Extract cache type for current level */
        CacheType = (Clidr >> (CurrentLevel * 3)) & 0x7;

        /* Skip if no data cache at this level */
        if ((CacheType & 0x2) == 0)
            continue;

        /* Select cache level in CSSELR_EL1 */
        __asm__ __volatile__ (
            "msr CSSELR_EL1, %0\n"
            "isb\n"
            :: "r"(CurrentLevel << 1)   /* Data cache select */
        );

        /* Read Current Cache Size ID Register */
        __asm__ __volatile__ (
            "mrs %0, CCSIDR_EL1\n"
            : "=r"(Ccsidr)
        );

        /* Extract cache geometry */
        NumSets = ((Ccsidr >> 13) & 0x7FFF) + 1;           /* Number of sets */
        NumWays = ((Ccsidr >> 3) & 0x3FF) + 1;             /* Number of ways */
        SetShift = (Ccsidr & 0x7) + 4;                      /* Cache line size as log2 */

        /* Calculate way shift for set/way format */
        WayShift = 32 - __builtin_clz(NumWays - 1);

        /* Clean all sets and ways for this cache level */
        for (Way = 0; Way < NumWays; Way++)
        {
            for (Set = 0; Set < NumSets; Set++)
            {
                /* Format: Level[3:1] | Way[31:32-WayShift] | Set[SetShift:5] */
                SetWayValue = (CurrentLevel << 1) | (Way << WayShift) | (Set << SetShift);

                __asm__ __volatile__ (
                    "dc csw, %0\n"      /* Clean by Set/Way */
                    :: "r"(SetWayValue)
                    : "memory"
                );
            }
        }
    }

    /* Ensure all cache operations complete */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Invalidate entire data cache by set/way
 *
 * This function performs a comprehensive data cache invalidation operation
 * using the set/way method for all cache levels.
 */
VOID
NTAPI
HalInvalidateEntireDataCache(VOID)
{
    ULONG64 Clidr, CurrentLevel, CacheType;
    ULONG64 Ccsidr, NumSets, NumWays, SetShift, WayShift;
    ULONG64 Set, Way, SetWayValue;

    /* Read Cache Level ID Register to determine cache levels */
    __asm__ __volatile__ (
        "mrs %0, CLIDR_EL1\n"
        : "=r"(Clidr)
    );

    /* Invalidate each cache level in reverse order */
    for (CurrentLevel = 6; CurrentLevel != MAXULONG64; CurrentLevel--)
    {
        /* Extract cache type for current level */
        CacheType = (Clidr >> (CurrentLevel * 3)) & 0x7;

        /* Skip if no data cache at this level */
        if ((CacheType & 0x2) == 0)
            continue;

        /* Select cache level in CSSELR_EL1 */
        __asm__ __volatile__ (
            "msr CSSELR_EL1, %0\n"
            "isb\n"
            :: "r"(CurrentLevel << 1)   /* Data cache select */
        );

        /* Read Current Cache Size ID Register */
        __asm__ __volatile__ (
            "mrs %0, CCSIDR_EL1\n"
            : "=r"(Ccsidr)
        );

        /* Extract cache geometry */
        NumSets = ((Ccsidr >> 13) & 0x7FFF) + 1;           /* Number of sets */
        NumWays = ((Ccsidr >> 3) & 0x3FF) + 1;             /* Number of ways */
        SetShift = (Ccsidr & 0x7) + 4;                      /* Cache line size as log2 */

        /* Calculate way shift for set/way format */
        WayShift = 32 - __builtin_clz(NumWays - 1);

        /* Invalidate all sets and ways for this cache level */
        for (Way = 0; Way < NumWays; Way++)
        {
            for (Set = 0; Set < NumSets; Set++)
            {
                /* Format: Level[3:1] | Way[31:32-WayShift] | Set[SetShift:5] */
                SetWayValue = (CurrentLevel << 1) | (Way << WayShift) | (Set << SetShift);

                __asm__ __volatile__ (
                    "dc isw, %0\n"      /* Invalidate by Set/Way */
                    :: "r"(SetWayValue)
                    : "memory"
                );
            }
        }
    }

    /* Ensure all cache operations complete */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Clean and invalidate entire data cache by set/way
 *
 * This function performs a comprehensive data cache clean and invalidation
 * operation using the set/way method for all cache levels.
 */
VOID
NTAPI
HalFlushEntireDataCache(VOID)
{
    ULONG64 Clidr, CurrentLevel, CacheType;
    ULONG64 Ccsidr, NumSets, NumWays, SetShift, WayShift;
    ULONG64 Set, Way, SetWayValue;

    /* Read Cache Level ID Register to determine cache levels */
    __asm__ __volatile__ (
        "mrs %0, CLIDR_EL1\n"
        : "=r"(Clidr)
    );

    /* Clean and invalidate each cache level in reverse order for flush */
    for (CurrentLevel = 6; CurrentLevel != MAXULONG64; CurrentLevel--)
    {
        /* Extract cache type for current level */
        CacheType = (Clidr >> (CurrentLevel * 3)) & 0x7;

        /* Skip if no data cache at this level */
        if ((CacheType & 0x2) == 0)
            continue;

        /* Select cache level in CSSELR_EL1 */
        __asm__ __volatile__ (
            "msr CSSELR_EL1, %0\n"
            "isb\n"
            :: "r"(CurrentLevel << 1)   /* Data cache select */
        );

        /* Read Current Cache Size ID Register */
        __asm__ __volatile__ (
            "mrs %0, CCSIDR_EL1\n"
            : "=r"(Ccsidr)
        );

        /* Extract cache geometry */
        NumSets = ((Ccsidr >> 13) & 0x7FFF) + 1;           /* Number of sets */
        NumWays = ((Ccsidr >> 3) & 0x3FF) + 1;             /* Number of ways */
        SetShift = (Ccsidr & 0x7) + 4;                      /* Cache line size as log2 */

        /* Calculate way shift for set/way format */
        WayShift = 32 - __builtin_clz(NumWays - 1);

        /* Clean and invalidate all sets and ways for this cache level */
        for (Way = 0; Way < NumWays; Way++)
        {
            for (Set = 0; Set < NumSets; Set++)
            {
                /* Format: Level[3:1] | Way[31:32-WayShift] | Set[SetShift:5] */
                SetWayValue = (CurrentLevel << 1) | (Way << WayShift) | (Set << SetShift);

                __asm__ __volatile__ (
                    "dc cisw, %0\n"     /* Clean and Invalidate by Set/Way */
                    :: "r"(SetWayValue)
                    : "memory"
                );
            }
        }
    }

    /* Ensure all cache operations complete */
    __asm__ __volatile__ (
        "dsb sy\n"
        ::: "memory"
    );
}

/*
 * @brief Initialize ARM64 cache line size detection
 */
VOID
NTAPI
HalInitializeCacheLineSize(VOID)
{
    ULONG64 CtrEl0;

    /* Read Cache Type Register */
    __asm__ __volatile__ (
        "mrs %0, CTR_EL0\n"
        : "=r"(CtrEl0)
    );

    /* Extract data cache minimum line size */
    HalCacheLineSize = 4 << ((CtrEl0 >> 16) & 0xF);

    /* Ensure we have a reasonable cache line size */
    if (HalCacheLineSize < 16 || HalCacheLineSize > 256)
    {
        DPRINT1("ARM64: Invalid cache line size detected: %u, using default 64\n", HalCacheLineSize);
        HalCacheLineSize = 64;
    }

    HalCacheInitialized = TRUE;

    DPRINT("ARM64 cache line size: %u bytes\n", HalCacheLineSize);
}

/*
 * @brief Get ARM64 cache line size
 */
ULONG
NTAPI
HalGetCacheLineSize(VOID)
{
    if (!HalCacheInitialized)
    {
        HalInitializeCacheLineSize();
    }

    return HalCacheLineSize;
}

/* EOF */