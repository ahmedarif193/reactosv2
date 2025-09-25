/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/arm64/pool.c
 * PURPOSE:         ARM64 Pool Allocator
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* ARM64-specific includes */
#include <internal/arm64/intrin_i.h>

/* GLOBALS ********************************************************************/

/* ARM64 Cache Line Size - typically 64 bytes */
#define ARM64_CACHE_LINE_SIZE       64
#define ARM64_CACHE_LINE_MASK       (ARM64_CACHE_LINE_SIZE - 1)
#define ARM64_CACHE_ALIGN(size)     (((size) + ARM64_CACHE_LINE_MASK) & ~ARM64_CACHE_LINE_MASK)

/* ARM64 Pool Block Sizes - optimized for common allocations */
#define ARM64_POOL_BLOCK_SIZE       16  /* Minimum allocation unit */
#define ARM64_POOL_BLOCK_SHIFT      4   /* log2(16) */

/* Pool size classes - powers of 2 up to PAGE_SIZE */
#define ARM64_POOL_SIZE_CLASSES     9
static const SIZE_T Arm64PoolSizeClasses[ARM64_POOL_SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

/* ARM64 Pool Header - aligned to cache line boundary */
typedef struct DECLSPEC_ALIGN(ARM64_CACHE_LINE_SIZE) _ARM64_POOL_HEADER {
    union {
        struct {
            USHORT PreviousSize : 9;      /* Size of previous block in units */
            USHORT PoolIndex : 7;         /* Pool descriptor index */
            USHORT BlockSize : 9;         /* Size of this block in units */
            USHORT PoolType : 7;          /* Pool type and flags */
        };
        ULONG Ulong1;
    };

    ULONG PoolTag;                        /* Pool tag for tracking */

    union {
        PEPROCESS ProcessBilled;          /* Process billed for this allocation */
        struct {
            USHORT AllocatorBackTraceIndex; /* For debugging */
            USHORT PoolTagHash;           /* Hash of pool tag */
        };
    };

    /* ARM64-specific fields for cache optimization */
    ULONGLONG TimeStamp;                  /* Allocation timestamp */
    PVOID NextFree;                       /* Next free block in size class */

    /* Padding to cache line boundary */
    UCHAR Padding[ARM64_CACHE_LINE_SIZE -
                  (sizeof(ULONG) + sizeof(ULONG) + sizeof(PEPROCESS) +
                   sizeof(ULONGLONG) + sizeof(PVOID))];
} ARM64_POOL_HEADER, *PARM64_POOL_HEADER;

C_ASSERT(sizeof(ARM64_POOL_HEADER) == ARM64_CACHE_LINE_SIZE);

/* ARM64 Pool Descriptor */
typedef struct _ARM64_POOL_DESCRIPTOR {
    POOL_TYPE PoolType;                   /* Pool type (paged/non-paged) */
    SIZE_T TotalBytes;                    /* Total bytes allocated */
    SIZE_T FreeBytes;                     /* Free bytes available */
    ULONG TotalAllocations;               /* Total allocation count */
    ULONG FailedAllocations;              /* Failed allocation count */

    /* Free lists for each size class */
    LIST_ENTRY FreeListHeads[ARM64_POOL_SIZE_CLASSES];
    KSPIN_LOCK FreeListLocks[ARM64_POOL_SIZE_CLASSES];

    /* Large allocation tracking */
    LIST_ENTRY LargeAllocations;
    KSPIN_LOCK LargeAllocationLock;

    /* Pool expansion */
    PVOID PoolStart;                      /* Start of pool region */
    PVOID PoolEnd;                        /* End of pool region */
    SIZE_T PoolSize;                      /* Total pool size */

    /* ARM64-specific optimization fields */
    volatile LONG FastAllocHits;          /* Fast path allocation hits */
    volatile LONG SlowAllocHits;          /* Slow path allocation hits */
    ULONG PreferredNode;                  /* NUMA node preference */

} ARM64_POOL_DESCRIPTOR, *PARM64_POOL_DESCRIPTOR;

/* Global pool descriptors */
static ARM64_POOL_DESCRIPTOR NonPagedPoolDescriptor;
static ARM64_POOL_DESCRIPTOR PagedPoolDescriptor;

/* Pool debugging and statistics */
static BOOLEAN PoolDebuggingEnabled = FALSE;
static ULONG PoolCorruptionDetected = 0;
static KSPIN_LOCK PoolStatisticsLock;

/* Pool tag tracking table */
#define POOL_TAG_TABLE_SIZE 2048
typedef struct _ARM64_POOL_TAG_ENTRY {
    ULONG Tag;
    LONG NonPagedAllocs;
    LONG NonPagedFrees;
    SIZE_T NonPagedBytes;
    LONG PagedAllocs;
    LONG PagedFrees;
    SIZE_T PagedBytes;
} ARM64_POOL_TAG_ENTRY, *PARM64_POOL_TAG_ENTRY;

static ARM64_POOL_TAG_ENTRY PoolTagTable[POOL_TAG_TABLE_SIZE];
static KSPIN_LOCK PoolTagTableLock;

/* PRIVATE FUNCTIONS **********************************************************/

/**
 * @brief ARM64-specific memory barrier for pool operations
 */
FORCEINLINE
VOID
Arm64PoolMemoryBarrier(VOID)
{
    /* ARM64 data memory barrier - ensures ordering of memory operations */
    _ReadWriteBarrier();
    KeMemoryBarrier();
}

/**
 * @brief ARM64-specific cache line flush
 */
FORCEINLINE
VOID
Arm64FlushCacheLine(PVOID Address)
{
    /* On ARM64, ensure cache coherency */
    _ReadWriteBarrier();
    KeMemoryBarrier();
    /* Note: Actual cache operations would be handled by the HAL */
}

/**
 * @brief Get size class index for a given size
 */
FORCEINLINE
ULONG
Arm64GetSizeClassIndex(SIZE_T Size)
{
    ULONG Index;

    /* Find the appropriate size class */
    for (Index = 0; Index < ARM64_POOL_SIZE_CLASSES; Index++) {
        if (Size <= Arm64PoolSizeClasses[Index]) {
            return Index;
        }
    }

    /* Size too large for fixed size classes */
    return ARM64_POOL_SIZE_CLASSES;
}

/**
 * @brief Calculate pool tag hash for fast lookup
 */
FORCEINLINE
ULONG
Arm64CalculateTagHash(ULONG Tag)
{
    /* Simple hash function for pool tag */
    ULONG Hash = Tag;
    Hash ^= (Hash >> 16);
    Hash ^= (Hash >> 8);
    return Hash % POOL_TAG_TABLE_SIZE;
}

/**
 * @brief Update pool tag statistics
 */
VOID
Arm64UpdatePoolTagStats(
    _In_ ULONG Tag,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T Size,
    _In_ BOOLEAN IsAllocation
)
{
    ULONG Hash;
    PARM64_POOL_TAG_ENTRY Entry;
    KIRQL OldIrql;

    if (!Tag) return;

    Hash = Arm64CalculateTagHash(Tag);
    Entry = &PoolTagTable[Hash];

    KeAcquireSpinLock(&PoolTagTableLock, &OldIrql);

    /* Find or create entry */
    if (Entry->Tag == 0) {
        Entry->Tag = Tag;
    } else if (Entry->Tag != Tag) {
        /* Hash collision - for simplicity, just increment first available */
        ULONG i;
        for (i = 1; i < POOL_TAG_TABLE_SIZE; i++) {
            Entry = &PoolTagTable[(Hash + i) % POOL_TAG_TABLE_SIZE];
            if (Entry->Tag == 0 || Entry->Tag == Tag) {
                if (Entry->Tag == 0) Entry->Tag = Tag;
                break;
            }
        }
    }

    /* Update statistics */
    if ((PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool) {
        if (IsAllocation) {
            Entry->NonPagedAllocs++;
            Entry->NonPagedBytes += Size;
        } else {
            Entry->NonPagedFrees++;
            Entry->NonPagedBytes -= Size;
        }
    } else {
        if (IsAllocation) {
            Entry->PagedAllocs++;
            Entry->PagedBytes += Size;
        } else {
            Entry->PagedFrees++;
            Entry->PagedBytes -= Size;
        }
    }

    KeReleaseSpinLock(&PoolTagTableLock, OldIrql);
}

/**
 * @brief Validate pool header for corruption detection
 */
BOOLEAN
Arm64ValidatePoolHeader(
    _In_ PARM64_POOL_HEADER Header,
    _In_ SIZE_T ExpectedSize
)
{
    if (!PoolDebuggingEnabled) {
        return TRUE;
    }

    /* Basic validation checks */
    if ((ULONG_PTR)Header & ARM64_CACHE_LINE_MASK) {
        DPRINT1("ARM64 Pool: Header not cache-line aligned: %p\n", Header);
        InterlockedIncrement((PLONG)&PoolCorruptionDetected);
        return FALSE;
    }

    if (Header->BlockSize == 0 || Header->BlockSize > (PAGE_SIZE / ARM64_POOL_BLOCK_SIZE)) {
        DPRINT1("ARM64 Pool: Invalid block size: %u\n", Header->BlockSize);
        InterlockedIncrement((PLONG)&PoolCorruptionDetected);
        return FALSE;
    }

    if (ExpectedSize && (Header->BlockSize * ARM64_POOL_BLOCK_SIZE) < ExpectedSize) {
        DPRINT1("ARM64 Pool: Block size too small for allocation\n");
        InterlockedIncrement((PLONG)&PoolCorruptionDetected);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Allocate from pool expansion area
 */
PVOID
Arm64ExpandPool(
    _In_ PARM64_POOL_DESCRIPTOR Descriptor,
    _In_ SIZE_T Size,
    _In_ ULONG Tag
)
{
    PVOID BaseVa;
    SIZE_T PagesNeeded;
    PARM64_POOL_HEADER Header;

    /* Calculate pages needed (round up) */
    PagesNeeded = BYTES_TO_PAGES(Size + sizeof(ARM64_POOL_HEADER));

    /* Allocate pages from MM */
    BaseVa = MiAllocatePoolPages(Descriptor->PoolType, PagesNeeded * PAGE_SIZE);
    if (!BaseVa) {
        return NULL;
    }

    /* Initialize pool header */
    Header = (PARM64_POOL_HEADER)BaseVa;
    RtlZeroMemory(Header, sizeof(ARM64_POOL_HEADER));

    Header->BlockSize = (USHORT)((Size + ARM64_POOL_BLOCK_SIZE - 1) / ARM64_POOL_BLOCK_SIZE);
    Header->PoolType = (USHORT)(Descriptor->PoolType & 0x7F);
    Header->PoolTag = Tag;
    Header->TimeStamp = KeQueryPerformanceCounter(NULL).QuadPart;

    /* Update descriptor statistics */
    InterlockedExchangeAdd((PLONG)&Descriptor->TotalBytes, Size);
    InterlockedIncrement((PLONG)&Descriptor->TotalAllocations);

    /* Flush cache line to ensure header is visible */
    Arm64FlushCacheLine(Header);

    return (PVOID)((PCHAR)Header + sizeof(ARM64_POOL_HEADER));
}

/**
 * @brief Fast path allocation from free list
 */
PVOID
Arm64FastPoolAlloc(
    _In_ PARM64_POOL_DESCRIPTOR Descriptor,
    _In_ ULONG SizeClass,
    _In_ SIZE_T Size,
    _In_ ULONG Tag
)
{
    PLIST_ENTRY FreeList;
    PLIST_ENTRY Entry;
    PARM64_POOL_HEADER Header;
    KIRQL OldIrql;
    PVOID Result = NULL;

    FreeList = &Descriptor->FreeListHeads[SizeClass];

    /* Acquire spinlock for this size class */
    KeAcquireSpinLock(&Descriptor->FreeListLocks[SizeClass], &OldIrql);

    if (!IsListEmpty(FreeList)) {
        /* Get first free block */
        Entry = RemoveHeadList(FreeList);
        Header = CONTAINING_RECORD(Entry, ARM64_POOL_HEADER, NextFree);

        /* Validate header */
        if (Arm64ValidatePoolHeader(Header, Size)) {
            /* Update header */
            Header->PoolTag = Tag;
            Header->TimeStamp = KeQueryPerformanceCounter(NULL).QuadPart;

            /* Clear free list pointer */
            Header->NextFree = NULL;

            Result = (PVOID)((PCHAR)Header + sizeof(ARM64_POOL_HEADER));

            /* Update statistics */
            InterlockedIncrement((PLONG)&Descriptor->FastAllocHits);
        }
    }

    KeReleaseSpinLock(&Descriptor->FreeListLocks[SizeClass], OldIrql);

    return Result;
}

/**
 * @brief Free block to appropriate free list
 */
VOID
Arm64FastPoolFree(
    _In_ PARM64_POOL_DESCRIPTOR Descriptor,
    _In_ PARM64_POOL_HEADER Header
)
{
    ULONG SizeClass;
    SIZE_T BlockSize;
    KIRQL OldIrql;

    BlockSize = Header->BlockSize * ARM64_POOL_BLOCK_SIZE;
    SizeClass = Arm64GetSizeClassIndex(BlockSize);

    if (SizeClass >= ARM64_POOL_SIZE_CLASSES) {
        /* Large allocation - handle separately */
        MiFreePoolPages((PVOID)Header);
        return;
    }

    /* Clear sensitive data */
    Header->PoolTag = 0;
    Header->ProcessBilled = NULL;
    Header->TimeStamp = 0;

    /* Add to free list */
    KeAcquireSpinLock(&Descriptor->FreeListLocks[SizeClass], &OldIrql);
    InsertHeadList(&Descriptor->FreeListHeads[SizeClass],
                   (PLIST_ENTRY)&Header->NextFree);
    KeReleaseSpinLock(&Descriptor->FreeListLocks[SizeClass], OldIrql);

    /* Update statistics */
    InterlockedExchangeAdd((PLONG)&Descriptor->FreeBytes, BlockSize);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief Initialize ARM64 pool allocator
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
MiInitializeArm64Pool(VOID)
{
    ULONG i;
    PARM64_POOL_DESCRIPTOR Descriptor;

    DPRINT("Initializing ARM64 Pool Allocator\n");

    /* Initialize global locks */
    KeInitializeSpinLock(&PoolStatisticsLock);
    KeInitializeSpinLock(&PoolTagTableLock);

    /* Clear pool tag table */
    RtlZeroMemory(PoolTagTable, sizeof(PoolTagTable));

    /* Initialize NonPaged Pool Descriptor */
    Descriptor = &NonPagedPoolDescriptor;
    RtlZeroMemory(Descriptor, sizeof(ARM64_POOL_DESCRIPTOR));

    Descriptor->PoolType = NonPagedPool;
    Descriptor->PoolStart = MmNonPagedPoolStart;
    Descriptor->PoolEnd = MmNonPagedPoolEnd;
    Descriptor->PoolSize = (ULONG_PTR)MmNonPagedPoolEnd - (ULONG_PTR)MmNonPagedPoolStart;

    /* Initialize free lists and locks */
    for (i = 0; i < ARM64_POOL_SIZE_CLASSES; i++) {
        InitializeListHead(&Descriptor->FreeListHeads[i]);
        KeInitializeSpinLock(&Descriptor->FreeListLocks[i]);
    }

    InitializeListHead(&Descriptor->LargeAllocations);
    KeInitializeSpinLock(&Descriptor->LargeAllocationLock);

    /* Initialize Paged Pool Descriptor */
    Descriptor = &PagedPoolDescriptor;
    RtlZeroMemory(Descriptor, sizeof(ARM64_POOL_DESCRIPTOR));

    Descriptor->PoolType = PagedPool;
    Descriptor->PoolStart = MmPagedPoolStart;
    Descriptor->PoolEnd = MmPagedPoolEnd;
    Descriptor->PoolSize = (ULONG_PTR)MmPagedPoolEnd - (ULONG_PTR)MmPagedPoolStart;

    /* Initialize free lists and locks */
    for (i = 0; i < ARM64_POOL_SIZE_CLASSES; i++) {
        InitializeListHead(&Descriptor->FreeListHeads[i]);
        KeInitializeSpinLock(&Descriptor->FreeListLocks[i]);
    }

    InitializeListHead(&Descriptor->LargeAllocations);
    KeInitializeSpinLock(&Descriptor->LargeAllocationLock);

    /* Enable debugging if requested */
    PoolDebuggingEnabled = (MmProtectFreedNonPagedPool != FALSE);

    DPRINT("ARM64 Pool Allocator initialized successfully\n");
    return STATUS_SUCCESS;
}

/**
 * @brief ARM64 Pool allocation function
 */
PVOID
NTAPI
ExAllocatePoolArm64(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag
)
{
    PARM64_POOL_DESCRIPTOR Descriptor;
    ULONG SizeClass;
    PVOID Result = NULL;
    SIZE_T AlignedSize;

    /* Validate parameters */
    if (NumberOfBytes == 0) {
        return NULL;
    }

    /* Select appropriate pool descriptor */
    if ((PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool) {
        Descriptor = &NonPagedPoolDescriptor;
    } else {
        Descriptor = &PagedPoolDescriptor;
    }

    /* ARM64 cache line alignment for better performance */
    AlignedSize = ARM64_CACHE_ALIGN(NumberOfBytes);

    /* Determine allocation strategy */
    SizeClass = Arm64GetSizeClassIndex(AlignedSize);

    if (SizeClass < ARM64_POOL_SIZE_CLASSES) {
        /* Try fast path allocation from free list */
        Result = Arm64FastPoolAlloc(Descriptor, SizeClass, AlignedSize, Tag);
    }

    if (!Result) {
        /* Fall back to pool expansion */
        Result = Arm64ExpandPool(Descriptor, AlignedSize, Tag);

        if (!Result) {
            InterlockedIncrement((PLONG)&Descriptor->FailedAllocations);
        } else {
            InterlockedIncrement((PLONG)&Descriptor->SlowAllocHits);
        }
    }

    /* Update pool tag statistics */
    if (Result) {
        Arm64UpdatePoolTagStats(Tag, PoolType, AlignedSize, TRUE);
    }

    return Result;
}

/**
 * @brief ARM64 Pool deallocation function
 */
VOID
NTAPI
ExFreePoolArm64(
    _In_ PVOID P,
    _In_ ULONG Tag
)
{
    PARM64_POOL_HEADER Header;
    PARM64_POOL_DESCRIPTOR Descriptor;
    SIZE_T BlockSize;

    if (!P) {
        return;
    }

    /* Get pool header */
    Header = (PARM64_POOL_HEADER)((PCHAR)P - sizeof(ARM64_POOL_HEADER));

    /* Validate header */
    if (!Arm64ValidatePoolHeader(Header, 0)) {
        KeBugCheckEx(BAD_POOL_HEADER,
                    (ULONG_PTR)P,
                    (ULONG_PTR)Header,
                    Tag,
                    0x1);
        return;
    }

    BlockSize = Header->BlockSize * ARM64_POOL_BLOCK_SIZE;

    /* Select appropriate pool descriptor */
    if ((Header->PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool) {
        Descriptor = &NonPagedPoolDescriptor;
    } else {
        Descriptor = &PagedPoolDescriptor;
    }

    /* Update pool tag statistics */
    Arm64UpdatePoolTagStats(Header->PoolTag, Header->PoolType, BlockSize, FALSE);

    /* Free the block */
    Arm64FastPoolFree(Descriptor, Header);

    /* Memory barrier to ensure all writes are complete */
    Arm64PoolMemoryBarrier();
}

/**
 * @brief Get pool allocation information
 */
NTSTATUS
NTAPI
ExGetPoolTagInfoArm64(
    _In_ ULONG Tag,
    _Out_ PSYSTEM_POOL_TAG_INFORMATION TagInfo
)
{
    ULONG Hash;
    PARM64_POOL_TAG_ENTRY Entry;
    KIRQL OldIrql;
    ULONG i;

    if (!TagInfo) {
        return STATUS_INVALID_PARAMETER;
    }

    Hash = Arm64CalculateTagHash(Tag);

    KeAcquireSpinLock(&PoolTagTableLock, &OldIrql);

    /* Find tag entry */
    for (i = 0; i < POOL_TAG_TABLE_SIZE; i++) {
        Entry = &PoolTagTable[(Hash + i) % POOL_TAG_TABLE_SIZE];
        if (Entry->Tag == Tag) {
            TagInfo->TagUlong = Entry->Tag;
            TagInfo->NonPagedAllocs = Entry->NonPagedAllocs;
            TagInfo->NonPagedFrees = Entry->NonPagedFrees;
            TagInfo->NonPagedUsed = Entry->NonPagedBytes;
            TagInfo->PagedAllocs = Entry->PagedAllocs;
            TagInfo->PagedFrees = Entry->PagedFrees;
            TagInfo->PagedUsed = Entry->PagedBytes;

            KeReleaseSpinLock(&PoolTagTableLock, OldIrql);
            return STATUS_SUCCESS;
        }
        if (Entry->Tag == 0) {
            break;
        }
    }

    KeReleaseSpinLock(&PoolTagTableLock, OldIrql);
    return STATUS_NOT_FOUND;
}

/**
 * @brief Get ARM64 pool statistics
 */
VOID
NTAPI
ExGetPoolStatisticsArm64(
    _Out_ PSYSTEM_POOL_ENTRY NonPagedPoolStats,
    _Out_ PSYSTEM_POOL_ENTRY PagedPoolStats
)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&PoolStatisticsLock, &OldIrql);

    /* NonPaged Pool Statistics */
    if (NonPagedPoolStats) {
        NonPagedPoolStats->Allocated = NonPagedPoolDescriptor.TotalBytes -
                                       NonPagedPoolDescriptor.FreeBytes;
        NonPagedPoolStats->Size = NonPagedPoolDescriptor.PoolSize;

        /* Extension fields for ARM64 */
        if (sizeof(SYSTEM_POOL_ENTRY) > 8) {
            NonPagedPoolStats->Frees = NonPagedPoolDescriptor.FailedAllocations;
            NonPagedPoolStats->Allocs = NonPagedPoolDescriptor.TotalAllocations;
        }
    }

    /* Paged Pool Statistics */
    if (PagedPoolStats) {
        PagedPoolStats->Allocated = PagedPoolDescriptor.TotalBytes -
                                    PagedPoolDescriptor.FreeBytes;
        PagedPoolStats->Size = PagedPoolDescriptor.PoolSize;

        /* Extension fields for ARM64 */
        if (sizeof(SYSTEM_POOL_ENTRY) > 8) {
            PagedPoolStats->Frees = PagedPoolDescriptor.FailedAllocations;
            PagedPoolStats->Allocs = PagedPoolDescriptor.TotalAllocations;
        }
    }

    KeReleaseSpinLock(&PoolStatisticsLock, OldIrql);
}

/**
 * @brief Integration wrapper for ExAllocatePool
 */
PVOID
NTAPI
ExAllocatePool(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes
)
{
    /* Call ARM64 implementation with default tag */
    return ExAllocatePoolArm64(PoolType, NumberOfBytes, 'loPM');  /* 'MPoL' */
}

/**
 * @brief Integration wrapper for ExAllocatePoolWithTag
 */
PVOID
NTAPI
ExAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag
)
{
    return ExAllocatePoolArm64(PoolType, NumberOfBytes, Tag);
}

/**
 * @brief Integration wrapper for ExFreePool
 */
VOID
NTAPI
ExFreePool(
    _In_ PVOID P
)
{
    /* Extract tag from header if available */
    ULONG Tag = 0;
    if (P) {
        PARM64_POOL_HEADER Header = (PARM64_POOL_HEADER)((PCHAR)P - sizeof(ARM64_POOL_HEADER));
        if (Arm64ValidatePoolHeader(Header, 0)) {
            Tag = Header->PoolTag;
        }
    }

    ExFreePoolArm64(P, Tag);
}

/**
 * @brief Integration wrapper for ExFreePoolWithTag
 */
VOID
NTAPI
ExFreePoolWithTag(
    _In_ PVOID P,
    _In_ ULONG Tag
)
{
    ExFreePoolArm64(P, Tag);
}

/**
 * @brief Get the size of a pool allocation
 */
SIZE_T
NTAPI
ExSizeOfPool(
    _In_ PVOID P
)
{
    PARM64_POOL_HEADER Header;

    if (!P) {
        return 0;
    }

    Header = (PARM64_POOL_HEADER)((PCHAR)P - sizeof(ARM64_POOL_HEADER));

    if (!Arm64ValidatePoolHeader(Header, 0)) {
        return 0;
    }

    return Header->BlockSize * ARM64_POOL_BLOCK_SIZE;
}

/**
 * @brief Get pool type of an allocation
 */
POOL_TYPE
NTAPI
ExQueryPoolBlockType(
    _In_ PVOID P
)
{
    PARM64_POOL_HEADER Header;

    if (!P) {
        return (POOL_TYPE)-1;
    }

    Header = (PARM64_POOL_HEADER)((PCHAR)P - sizeof(ARM64_POOL_HEADER));

    if (!Arm64ValidatePoolHeader(Header, 0)) {
        return (POOL_TYPE)-1;
    }

    return (POOL_TYPE)Header->PoolType;
}

/**
 * @brief ARM64 Pool corruption check and repair
 */
BOOLEAN
NTAPI
MiCheckPoolCorruption(
    _In_ PVOID StartAddress,
    _In_ SIZE_T Size
)
{
    PARM64_POOL_HEADER Header;
    ULONG CorruptionCount = 0;
    PVOID CurrentAddress = StartAddress;
    PVOID EndAddress = (PVOID)((PCHAR)StartAddress + Size);

    while (CurrentAddress < EndAddress) {
        Header = (PARM64_POOL_HEADER)CurrentAddress;

        /* Check for basic corruption patterns */
        if (!Arm64ValidatePoolHeader(Header, 0)) {
            CorruptionCount++;
            DPRINT1("ARM64 Pool corruption detected at %p\n", CurrentAddress);
        }

        /* Move to next potential block */
        if (Header->BlockSize > 0) {
            CurrentAddress = (PVOID)((PCHAR)CurrentAddress +
                                   (Header->BlockSize * ARM64_POOL_BLOCK_SIZE));
        } else {
            /* Corrupted header, try next cache line */
            CurrentAddress = (PVOID)((PCHAR)CurrentAddress + ARM64_CACHE_LINE_SIZE);
        }
    }

    if (CorruptionCount > 0) {
        InterlockedExchangeAdd((PLONG)&PoolCorruptionDetected, CorruptionCount);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief ARM64 Pool memory pressure notification
 */
VOID
NTAPI
MiNotifyMemoryPressure(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Flags
)
{
    PARM64_POOL_DESCRIPTOR Descriptor;
    ULONG SizeClass;
    KIRQL OldIrql;
    PLIST_ENTRY Entry, NextEntry;
    ULONG FreedBlocks = 0;

    /* Select appropriate pool descriptor */
    if ((PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool) {
        Descriptor = &NonPagedPoolDescriptor;
    } else {
        Descriptor = &PagedPoolDescriptor;
    }

    /* Under memory pressure, try to free some cached blocks */
    for (SizeClass = 0; SizeClass < ARM64_POOL_SIZE_CLASSES; SizeClass++) {
        KeAcquireSpinLock(&Descriptor->FreeListLocks[SizeClass], &OldIrql);

        Entry = Descriptor->FreeListHeads[SizeClass].Flink;
        while ((Entry != &Descriptor->FreeListHeads[SizeClass]) && (FreedBlocks < 16)) {
            NextEntry = Entry->Flink;

            /* Free every other block to reduce fragmentation */
            if ((FreedBlocks % 2) == 0) {
                RemoveEntryList(Entry);
                FreedBlocks++;
            }

            Entry = NextEntry;
        }

        KeReleaseSpinLock(&Descriptor->FreeListLocks[SizeClass], OldIrql);
    }

    if (FreedBlocks > 0) {
        DPRINT("ARM64 Pool: Freed %u blocks due to memory pressure\n", FreedBlocks);
    }
}

/**
 * @brief ARM64 Pool diagnostic function for debugging
 */
VOID
NTAPI
MiDumpPoolStatistics(
    _In_ POOL_TYPE PoolType
)
{
    PARM64_POOL_DESCRIPTOR Descriptor;
    ULONG i;

    if ((PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool) {
        Descriptor = &NonPagedPoolDescriptor;
        DPRINT1("=== NonPaged Pool Statistics ===\n");
    } else {
        Descriptor = &PagedPoolDescriptor;
        DPRINT1("=== Paged Pool Statistics ===\n");
    }

    DPRINT1("Total Bytes: %lu\n", Descriptor->TotalBytes);
    DPRINT1("Free Bytes: %lu\n", Descriptor->FreeBytes);
    DPRINT1("Total Allocations: %lu\n", Descriptor->TotalAllocations);
    DPRINT1("Failed Allocations: %lu\n", Descriptor->FailedAllocations);
    DPRINT1("Fast Alloc Hits: %lu\n", Descriptor->FastAllocHits);
    DPRINT1("Slow Alloc Hits: %lu\n", Descriptor->SlowAllocHits);
    DPRINT1("Pool Start: %p\n", Descriptor->PoolStart);
    DPRINT1("Pool End: %p\n", Descriptor->PoolEnd);
    DPRINT1("Pool Size: %lu bytes\n", Descriptor->PoolSize);

    /* Show free list statistics */
    DPRINT1("\nFree List Statistics:\n");
    for (i = 0; i < ARM64_POOL_SIZE_CLASSES; i++) {
        ULONG Count = 0;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Descriptor->FreeListLocks[i], &OldIrql);
        for (Entry = Descriptor->FreeListHeads[i].Flink;
             Entry != &Descriptor->FreeListHeads[i];
             Entry = Entry->Flink) {
            Count++;
        }
        KeReleaseSpinLock(&Descriptor->FreeListLocks[i], OldIrql);

        if (Count > 0) {
            DPRINT1("Size Class %u (%u bytes): %u free blocks\n",
                   i, Arm64PoolSizeClasses[i], Count);
        }
    }

    if (PoolCorruptionDetected > 0) {
        DPRINT1("\nWARNING: %lu pool corruption(s) detected!\n", PoolCorruptionDetected);
    }

    DPRINT1("================================\n");
}

/* EOF */