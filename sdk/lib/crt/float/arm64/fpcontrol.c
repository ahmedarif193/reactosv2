/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 Floating Point Control Implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <float.h>

/* ARM64 FPCR (Floating Point Control Register) bits */
#define FPCR_IOC    (1 << 0)  /* Invalid Operation cumulative exception */
#define FPCR_DZC    (1 << 1)  /* Divide by Zero cumulative exception */
#define FPCR_OFC    (1 << 2)  /* Overflow cumulative exception */
#define FPCR_UFC    (1 << 3)  /* Underflow cumulative exception */
#define FPCR_IXC    (1 << 4)  /* Inexact cumulative exception */
#define FPCR_IDC    (1 << 7)  /* Input Denormal cumulative exception */
#define FPCR_IOE    (1 << 8)  /* Invalid Operation exception trap enable */
#define FPCR_DZE    (1 << 9)  /* Divide by Zero exception trap enable */
#define FPCR_OFE    (1 << 10) /* Overflow exception trap enable */
#define FPCR_UFE    (1 << 11) /* Underflow exception trap enable */
#define FPCR_IXE    (1 << 12) /* Inexact exception trap enable */
#define FPCR_IDE    (1 << 15) /* Input Denormal exception trap enable */
#define FPCR_RN     (3 << 22) /* Rounding mode */
#define FPCR_FZ     (1 << 24) /* Flush to zero mode */
#define FPCR_DN     (1 << 25) /* Default NaN mode */

/* Include standard float.h for the definitions */

/* Helper function to read FPCR */
static inline unsigned long long __get_fpcr(void)
{
    unsigned long long fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r" (fpcr));
    return fpcr;
}

/* Helper function to write FPCR */
static inline void __set_fpcr(unsigned long long fpcr)
{
    __asm__ __volatile__("msr fpcr, %0" : : "r" (fpcr));
}

/* Helper function to read FPSR (Floating Point Status Register) */
static inline unsigned long long __get_fpsr(void)
{
    unsigned long long fpsr;
    __asm__ __volatile__("mrs %0, fpsr" : "=r" (fpsr));
    return fpsr;
}

/* Helper function to write FPSR */
static inline void __set_fpsr(unsigned long long fpsr)
{
    __asm__ __volatile__("msr fpsr, %0" : : "r" (fpsr));
}

/* _control87 - get and set floating-point control word */
unsigned int _control87(unsigned int new_value, unsigned int mask)
{
    unsigned long long fpcr = __get_fpcr();
    unsigned int old_value = 0;

    /* Convert ARM64 FPCR to x86-style control word */
    /* Exception masks (inverted on ARM64) */
    if (!(fpcr & FPCR_IOE)) old_value |= _EM_INVALID;
    if (!(fpcr & FPCR_DZE)) old_value |= _EM_ZERODIVIDE;
    if (!(fpcr & FPCR_OFE)) old_value |= _EM_OVERFLOW;
    if (!(fpcr & FPCR_UFE)) old_value |= _EM_UNDERFLOW;
    if (!(fpcr & FPCR_IXE)) old_value |= _EM_INEXACT;
    if (!(fpcr & FPCR_IDE)) old_value |= _EM_DENORMAL;

    /* Rounding mode */
    switch ((fpcr >> 22) & 3) {
        case 0: old_value |= _RC_NEAR; break;
        case 1: old_value |= _RC_UP; break;
        case 2: old_value |= _RC_DOWN; break;
        case 3: old_value |= _RC_CHOP; break;
    }

    /* Apply new settings if mask is set */
    if (mask != 0) {
        unsigned long long new_fpcr = fpcr;

        /* Exception masks */
        if (mask & _MCW_EM) {
            new_fpcr &= ~(FPCR_IOE | FPCR_DZE | FPCR_OFE | FPCR_UFE | FPCR_IXE | FPCR_IDE);
            if (!(new_value & _EM_INVALID))    new_fpcr |= FPCR_IOE;
            if (!(new_value & _EM_ZERODIVIDE)) new_fpcr |= FPCR_DZE;
            if (!(new_value & _EM_OVERFLOW))   new_fpcr |= FPCR_OFE;
            if (!(new_value & _EM_UNDERFLOW))  new_fpcr |= FPCR_UFE;
            if (!(new_value & _EM_INEXACT))    new_fpcr |= FPCR_IXE;
            if (!(new_value & _EM_DENORMAL))   new_fpcr |= FPCR_IDE;
        }

        /* Rounding mode */
        if (mask & _MCW_RC) {
            new_fpcr &= ~FPCR_RN;
            switch (new_value & _MCW_RC) {
                case _RC_NEAR: new_fpcr |= (0 << 22); break;
                case _RC_UP:   new_fpcr |= (1 << 22); break;
                case _RC_DOWN: new_fpcr |= (2 << 22); break;
                case _RC_CHOP: new_fpcr |= (3 << 22); break;
            }
        }

        __set_fpcr(new_fpcr);
    }

    return old_value;
}

/* _controlfp - floating-point control (wrapper for _control87) */
unsigned int _controlfp(unsigned int new_value, unsigned int mask)
{
    return _control87(new_value, mask);
}

/* _clearfp - clear floating-point status word */
unsigned int _clearfp(void)
{
    unsigned long long fpsr = __get_fpsr();
    unsigned int status = 0;

    /* Convert ARM64 FPSR to x86-style status word */
    if (fpsr & FPCR_IOC) status |= _SW_INVALID;
    if (fpsr & FPCR_DZC) status |= _SW_ZERODIVIDE;
    if (fpsr & FPCR_OFC) status |= _SW_OVERFLOW;
    if (fpsr & FPCR_UFC) status |= _SW_UNDERFLOW;
    if (fpsr & FPCR_IXC) status |= _SW_INEXACT;
    if (fpsr & FPCR_IDC) status |= _SW_DENORMAL;

    /* Clear all exception flags */
    __set_fpsr(0);

    return status;
}

/* _statusfp - get floating-point status word */
unsigned int _statusfp(void)
{
    unsigned long long fpsr = __get_fpsr();
    unsigned int status = 0;

    /* Convert ARM64 FPSR to x86-style status word */
    if (fpsr & FPCR_IOC) status |= _SW_INVALID;
    if (fpsr & FPCR_DZC) status |= _SW_ZERODIVIDE;
    if (fpsr & FPCR_OFC) status |= _SW_OVERFLOW;
    if (fpsr & FPCR_UFC) status |= _SW_UNDERFLOW;
    if (fpsr & FPCR_IXC) status |= _SW_INEXACT;
    if (fpsr & FPCR_IDC) status |= _SW_DENORMAL;

    return status;
}

/* _fpreset - reset floating-point package */
/* Note: _fpreset is provided by ucrtbase_stubs.c for ucrtbase.dll
 * and by _fpreset.c for static CRT, so we don't define it here */

/* Additional compatibility functions */
/* _controlfp_s - removed duplicate, use the one from _controlfp_s.c */