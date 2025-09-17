/*
 * COPYRIGHT:       See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS Boot Library
 * FILE:            boot/environ/lib/arch/stub/kestubs.c
 * PURPOSE:         Kernel Executive Stubs for Bootloader
 * PROGRAMMER:
 */

/* INCLUDES ******************************************************************/

#include "bl.h"

/* FUNCTIONS *****************************************************************/

#ifdef _ARM64_

/*
 * Bootloader stub for KeGetCurrentIrql
 * In bootloader context, we're always at PASSIVE_LEVEL
 */
KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

#endif /* _ARM64_ */