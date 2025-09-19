/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Implementation of pow for ARM64
 * FILE:            lib/crt/math/arm64/pow.s
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

/* CODE **********************************************************************/

    .text

    .global pow
pow:
    /* Basic stub implementation for pow */
    /* Parameters: d0 = base, d1 = exponent */
    /* TODO: Implement proper pow calculation */
    /* For now, return 1.0 to avoid link errors */
    fmov d0, #1.0
    ret

/* EOF */