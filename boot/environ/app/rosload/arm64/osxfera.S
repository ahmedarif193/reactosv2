/*
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS UEFI OS Loader
 * FILE:            boot/environ/app/rosload/arm64/osxfera.S
 * PURPOSE:         Architecture-Specific Transfer Routines
 * PROGRAMMER:
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

/* FUNCTIONS *****************************************************************/

.code

/*
 * OslArchTransferToKernel(
 *     _In_ PLOADER_PARAMETER_BLOCK LoaderBlock, // x0
 *     _In_ PVOID KernelEntrypoint              // x1
 *     )
 */
FUNC OslArchTransferToKernel
    /* Save the loader block */
    mov x19, x0

    /* Save kernel entry point */
    mov x20, x1

    /* Disable interrupts */
    msr daifset, #0xF

    /* Ensure all memory operations complete */
    dsb sy
    isb

    /* Jump to kernel entry point with loader block in x0 */
    mov x0, x19
    br x20
ENDFUNC

END