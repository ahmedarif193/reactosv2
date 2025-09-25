/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of _setjmp for ARM64
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

//#include <kxarm64.h>

    TEXTAREA

    LEAF_ENTRY _setjmpex

    brk #0xf000

    LEAF_END _setjmpex

    IMPORT __intrinsic_setjmpex, WEAK _setjmpex
    IMPORT _setjmp, WEAK _setjmpex
    IMPORT setjmp, WEAK _setjmpex

    END

/* EOF */
