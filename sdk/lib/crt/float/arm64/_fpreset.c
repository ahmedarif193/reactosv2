/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 Floating Point Reset Implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* Helper function to write FPCR */
static inline void __set_fpcr(unsigned long long fpcr)
{
    __asm__ __volatile__("msr fpcr, %0" : : "r" (fpcr));
}

/* Helper function to write FPSR */
static inline void __set_fpsr(unsigned long long fpsr)
{
    __asm__ __volatile__("msr fpsr, %0" : : "r" (fpsr));
}

/* _fpreset - reset floating-point package */
void _fpreset(void)
{
    /* Set default FPCR: all exceptions masked, round to nearest */
    __set_fpcr(0);
    /* Clear all status flags */
    __set_fpsr(0);
}