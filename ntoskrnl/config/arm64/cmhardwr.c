/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/config/arm64/cmhardwr.c
 * PURPOSE:         Configuration Manager - ARM64 Specific Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* GLOBALS *******************************************************************/

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    //
    // Nothing to do on ARM64
    //
    return STATUS_SUCCESS;
}