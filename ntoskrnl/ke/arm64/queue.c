/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         ARM64 Thread Scheduling Helpers
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * ARM64-specific thread queue and scheduling optimizations can be added here.
 * The generic implementations from ke/thrdschd.c are used for standard APIs.
 */

/* Placeholder for ARM64-specific thread scheduling helpers */
VOID
NTAPI
KiArm64ThreadBarrier(VOID)
{
    /* Memory barrier for ARM64 thread operations */
    __dmb(ish);
}