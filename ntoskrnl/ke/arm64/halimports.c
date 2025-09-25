/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 HAL Import Wrappers
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * The cmlib library expects __imp_KeGetCurrentIrql to be available as an import.
 * On ARM64, KeGetCurrentIrql is normally a macro that accesses PCR directly.
 * We provide a wrapper function and the import pointer that cmlib expects.
 *
 * This is a build-time workaround specific to ARM64 MinGW cross-compilation.
 */

/* Wrapper function that implements KeGetCurrentIrql for the import */
static KIRQL NTAPI KernelGetCurrentIrql(VOID)
{
    /* Use the macro implementation directly */
#ifdef KeGetCurrentIrql
    /* The macro expands to KeGetPcr()->CurrentIrql */
    return KeGetCurrentIrql();
#else
    /* Fallback if macro is not defined */
    return KeGetPcr()->CurrentIrql;
#endif
}

/* Provide the import pointer that cmlib expects */
#ifdef __GNUC__
/* Create a function pointer with the import name */
KIRQL (NTAPI * const __imp_KeGetCurrentIrql)(VOID) = KernelGetCurrentIrql;
#endif