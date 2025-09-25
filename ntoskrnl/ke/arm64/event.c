/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Event Dispatcher Object Helpers
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64 specific intrinsics */
#include <internal/arm64/intrin_i.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 memory ordering for event operations */
#define ARM64_EVENT_ACQUIRE_BARRIER()  __dmb(ishld)
#define ARM64_EVENT_RELEASE_BARRIER()  __dmb(ishst)
#define ARM64_EVENT_FULL_BARRIER()     __dmb(ish)

/* FUNCTIONS *****************************************************************/

/*
 * ARM64-specific event optimizations can be added here if needed.
 * The generic implementations from ke/eventobj.c are used for standard APIs.
 */

/* Placeholder for ARM64-specific event helpers */
VOID
NTAPI
KiArm64EventBarrier(VOID)
{
    /* Full memory barrier for ARM64 event operations */
    ARM64_EVENT_FULL_BARRIER();
}