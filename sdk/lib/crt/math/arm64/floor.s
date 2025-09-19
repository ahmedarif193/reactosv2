/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Implementation of floor for ARM64
 * FILE:            lib/crt/math/arm64/floor.s
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

/* CODE **********************************************************************/

    .text

    .global floor
floor:
    /* Convert double to integer rounding toward negative infinity */
    frintm d0, d0
    ret

/* EOF */