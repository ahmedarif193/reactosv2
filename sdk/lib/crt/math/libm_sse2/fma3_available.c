
/*******************************************************************************
MIT License
-----------

Copyright (c) 2002-2019 Advanced Micro Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this Software and associated documentaon files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*******************************************************************************/

#ifdef TEST_STANDALONE
#include <stdio.h>
#ifdef _MSC_VER
#pragma section (".CRT$XIC",long,read)
#define _CRTALLOC(x) __declspec(allocate(x))
#endif /* _MSC_VER */
#else
#include <intrin.h>
#include <sect_attribs.h>
#undef _CRTALLOC
#define _CRTALLOC(x)
#endif

typedef int (__cdecl *_PIFV)(void); // FIXME: include process.h?

int __fma3_is_available = 0;
int __use_fma3_lib = 0;


int __cdecl _set_FMA3_enable(int flag)
{
    if (__fma3_is_available) __use_fma3_lib = flag;
    return __use_fma3_lib;
}

/* Forward declaration for the init function */
#ifdef __fma3_lib_init
/* When __fma3_lib_init is defined as a macro, use the macro value */
int __cdecl __acrt_initialize_fma3(void);
_CRTALLOC(".CRT$XIC") static _PIFV __attribute__((used)) init_fma3 = __acrt_initialize_fma3;
#else
/* When not defined as a macro, use the actual function */
int __fma3_lib_init(void);
_CRTALLOC(".CRT$XIC") static _PIFV __attribute__((used)) init_fma3 = __fma3_lib_init;
#endif

#ifndef __fma3_lib_init
int __fma3_lib_init(void)
#else
int __acrt_initialize_fma3(void)
#endif
{
#if defined(_M_IX86) || defined(_M_X64)
    int CPUID[4]; // CPUID[2] is ECX;

    __fma3_is_available = 0;
    __cpuid(CPUID, 1);
    if (CPUID[2] & (1 << 12)) {
        __fma3_is_available = 1;
    }

    __use_fma3_lib = __fma3_is_available;
#elif defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__)
    /* ARM64 has NEON FMA instructions but not x86 FMA3, set to 0 */
    __fma3_is_available = 0;
    __use_fma3_lib = 0;
#else
    /* Other architectures don't have FMA3, set to 0 */
    __fma3_is_available = 0;
    __use_fma3_lib = 0;
#endif
    return 0;
}

/* Additional compatibility function for UCRT - only if not already defined via macro */
#ifndef __fma3_lib_init
int __cdecl __acrt_initialize_fma3(void)
{
    return __fma3_lib_init();
}
#endif
