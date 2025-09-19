/*
 * PROJECT:     ReactOS DDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Memory Management DDK Definitions
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#pragma once

/* Memory Layout Constants for ARM64 */
#define MM_LOWEST_USER_ADDRESS             (PVOID)0x00000000000000000ULL
#define MM_HIGHEST_USER_ADDRESS             (PVOID)0x00007FFFFFFFFFFFULL
#define MM_SYSTEM_RANGE_START               (PVOID)0xFFFF800000000000ULL

#define MM_KSEG0_BASE                       MM_SYSTEM_RANGE_START
#define MM_KSEG2_BASE                       MM_SYSTEM_RANGE_START

/* ARM64 Page Sizes */
#ifndef PAGE_SIZE
#define PAGE_SIZE                           0x1000
#endif

#ifndef PAGE_SHIFT
#define PAGE_SHIFT                          12L
#endif

/* MmUserProbeAddress declaration for ARM64 */
extern NTKERNELAPI ULONG64 MmUserProbeAddress;
#define MM_USER_PROBE_ADDRESS               MmUserProbeAddress

/* Memory Allocation Alignment */
#define MM_ALLOCATION_GRANULARITY           0x10000

/* System PTEs */
#define MM_SYSTEM_PTE_BASE                  ((PVOID)0xFFFFF70000000000ULL)
#define MM_SYSTEM_PTE_END                   ((PVOID)0xFFFFF77FFFFFFFFFULL)

/* Non-paged pool */
#define MI_NON_PAGED_POOL_SIZE              (128 * 1024 * 1024)

/* Session Space */
#define MI_SESSION_SPACE_BASE               ((PVOID)0xFFFFF90000000000ULL)
#define MI_SESSION_SPACE_END                ((PVOID)0xFFFFF97FFFFFFFFFULL)

/* Hyperspace */
#define HYPER_SPACE                         ((PVOID)0xFFFFF88000000000ULL)
#define HYPER_SPACE_END                     ((PVOID)0xFFFFF88000FFFFFFULL)

/* Working Set List */
#define WORKING_SET_LIST                    ((PVOID)HYPER_SPACE)

/* PFN Database */
#define MI_PFN_DATABASE                     ((PVOID)0xFFFFFB0000000000ULL)

/* Note: Memory types and MDL flags are already defined in wdm.h */

/* Additional ARM64 specific definitions */
#define MM_DONT_ZERO_ALLOCATION             0x00000001
#define MM_ALLOCATE_FROM_LOCAL_NODE_ONLY    0x00000002
#define MM_ALLOCATE_FULLY_REQUIRED          0x00000004
#define MM_ALLOCATE_NO_WAIT                 0x00000008
#define MM_ALLOCATE_PREFER_CONTIGUOUS       0x00000010
#define MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS 0x00000020

/* EOF */