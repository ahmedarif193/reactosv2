/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD-3-Clause (https://spdx.org/licenses/BSD-3-Clause)
 * FILE:            ntoskrnl/mm/arm64/procsup.c
 * PURPOSE:         Process handling for ARM64 architecture
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

BOOLEAN
MiArchCreateProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    /* ARM64 implementation placeholder */
    /* ARM64 uses a different page table structure than x86 */
    /* This needs proper implementation based on ARM64 architecture */

    UNIMPLEMENTED;
    return FALSE;
}

VOID
MmDeleteProcessAddressSpace(IN PEPROCESS Process)
{
    /* ARM64 implementation placeholder */
    UNIMPLEMENTED;
}