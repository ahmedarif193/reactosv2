/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         ARM64 Thread Scheduler Helpers
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * ARM64-specific thread scheduler optimizations can be added here.
 * The generic implementations from ke/thrdschd.c are used for standard APIs.
 */

/* Placeholder for ARM64-specific scheduler helpers */
VOID
NTAPI
KiArm64SchedulerBarrier(VOID)
{
    /* Memory barrier for ARM64 scheduler operations */
    __dmb(ish);
}