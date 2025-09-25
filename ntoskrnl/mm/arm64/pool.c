/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/arm64/pool.c
 * PURPOSE:         ARM64 Pool Allocator Helpers
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

/* FUNCTIONS ******************************************************************/

/*
 * ARM64-specific pool allocation optimizations can be added here.
 * The generic implementations from mm/ARM3/expool.c are used for standard APIs.
 */

/* ARM64-specific cache-aligned allocation helper */
SIZE_T
NTAPI
MiArm64CacheAlignSize(
    IN SIZE_T Size)
{
    /* Align to ARM64 cache line boundary for optimal performance */
    return ARM64_CACHE_ALIGN(Size);
}

/* ARM64-specific memory barrier for pool operations */
VOID
NTAPI
MiArm64PoolBarrier(VOID)
{
    /* Memory barrier for ARM64 pool operations */
    __dmb(ish);
}