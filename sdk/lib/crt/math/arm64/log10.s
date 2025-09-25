/*
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Implementation of log10 for ARM64
 * FILE:            lib/crt/math/arm64/log10.s
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

/* CODE **********************************************************************/

    .text

    .global log10
log10:
    /* Basic stub implementation for log10 */
    /* TODO: Implement proper log10 calculation */
    /* For now, return 0.0 to avoid link errors */
    fmov d0, xzr
    ret

/* EOF */