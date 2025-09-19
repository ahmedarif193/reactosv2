
#ifndef _ARM64_MMTYPES_H
#define _ARM64_MMTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Page-related Macros
//
#ifndef PAGE_SIZE
#define PAGE_SIZE                         0x1000
#endif
#define PAGE_SHIFT                        12L
#define MM_ALLOCATION_GRANULARITY         0x10000
#define MM_ALLOCATION_GRANULARITY_SHIFT   16L
#define MM_PAGE_FRAME_NUMBER_SIZE         20

//
// User space range limit for ARM64
// ARM64 uses 48-bit virtual addresses with user space in the lower half
// User space: 0x0000_0000_0000_0000 to 0x0000_7FFF_FFFF_FFFF (128TB)
// Kernel space: 0xFFFF_8000_0000_0000 to 0xFFFF_FFFF_FFFF_FFFF (128TB)
//
#ifndef MI_HIGHEST_USER_ADDRESS
#define MI_HIGHEST_USER_ADDRESS         (PVOID)0x00007FFFFFFFFFFFULL
#endif

//
// Address of the shared user page (ARM64)
//
#define MM_SHARED_USER_DATA_VA          0x7FFE0000ULL

/* Following structs are based on WoA symbols */
typedef struct _HARDWARE_PTE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    union {
        ULONG64 NotLargePage:1;
        ULONG64 LargePage:1;  /* Inverted logic - LargePage = !NotLargePage */
    };
    ULONG64 CacheType:2;
    ULONG64 OsAvailable2:1;
    ULONG64 NonSecure:1;
    ULONG64 Owner:1;
    union {
        ULONG64 NotDirty:1;
        ULONG64 Dirty:1;     /* Inverted logic - Dirty = !NotDirty */
    };
    ULONG64 Shareability:2;
    ULONG64 Accessed:1;
    union {
        ULONG64 NonGlobal:1;
        ULONG64 Global:1;    /* Inverted logic - Global = !NonGlobal */
    };
    ULONG64 PageFrameNumber:36;
    ULONG64 RsvdZ1:4;
    ULONG64 ContigousBit:1;
    ULONG64 PrivilegedNoExecute:1;
    ULONG64 UserNoExecute:1;
    union {
        ULONG64 Writable:1;
        ULONG64 Write:1;     /* Alias for Writable */
    };
    ULONG64 CopyOnWrite:1;
    ULONG64 OsAvailable:2;
    ULONG64 PxnTable:1;
    ULONG64 UxnTable:1;
    ULONG64 ApTable:2;
    ULONG64 NsTable:1;
} HARDWARE_PTE, *PHARDWARE_PTE;

typedef struct _MMPTE_SOFTWARE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 PageFileLow:4;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 PageFileReserved:1;
    ULONG64 PageFileAllocated:1;
    ULONG64 UsedPageTableEntries:10;
    ULONG64 ColdPage:1;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 RsvdZ1:6;
    ULONG64 PageFileHigh:32;
} MMPTE_SOFTWARE;

typedef struct _MMPTE_TRANSITION
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 Spare:2;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 IoTracker:1;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 PageFrameNumber:40;
    ULONG64 RsvdZ1:12;
} MMPTE_TRANSITION;

typedef struct _MMPTE_PROTOTYPE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 HiberVerifyConverted:1;
    ULONG64 Unused1:1;
    ULONG64 ReadOnly:1;
    ULONG64 Combined:1;
    ULONG64 Prototype:1;
    ULONG64 DemandFillProto:1;
    ULONG64 RsvdZ1:4;
    ULONG64 ProtoAddress:48;
} MMPTE_PROTOTYPE;

typedef struct _MMPTE_SUBSECTION
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 WhichPool:1;
    ULONG64 RsvdZ1:2;
    ULONG64 Prototype:1;
    ULONG64 ColdPage:1;
    ULONG64 SubsectionAddressLow:4;
    ULONG64 SubsectionAddressHigh:20;
    ULONG64 SubsectionAddress:28;  /* Upper bits of address */
} MMPTE_SUBSECTION;

typedef struct _MMPTE_TIMESTAMP
{
    /* 8 Byte struct */
    ULONG64 MustBeZero:1;
    ULONG64 Protection:5;
    ULONG64 PageFileLow:4;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 RsvdZ1:20;
    ULONG64 GlobalTimeStamp:32;
} MMPTE_TIMESTAMP;

typedef struct _MMPTE_LIST
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 OneEntry:1;
    ULONG64 RsvdZ1:3;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 RsvdZ2:16;
    ULONG64 NextEntry:36;
} MMPTE_LIST;

typedef struct _MMPTE
{
    union
    {
        ULONG_PTR Long;
        HARDWARE_PTE Flush;
        HARDWARE_PTE Hard;
        MMPTE_PROTOTYPE Proto;
        MMPTE_SOFTWARE Soft;
        MMPTE_TRANSITION Trans;
        MMPTE_SUBSECTION Subsect;
        MMPTE_LIST List;
    } u;
} MMPTE, *PMMPTE,
  MMPDE, *PMMPDE;

#ifdef __cplusplus
}; // extern "C"
#endif

#endif
