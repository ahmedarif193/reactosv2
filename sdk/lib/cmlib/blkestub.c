/*
 * COPYRIGHT:       See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS Configuration Manager Library
 * FILE:            sdk/lib/cmlib/blkestub.c
 * PURPOSE:         Kernel Executive Stubs for Bootloader
 * PROGRAMMER:
 */

/* INCLUDES ******************************************************************/

#include "cmlib.h"

/* FUNCTIONS *****************************************************************/

#if defined(_BLDR_) && defined(_ARM64_)

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

#endif /* _BLDR_ && _ARM64_ */