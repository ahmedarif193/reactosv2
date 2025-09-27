/*
 * PROJECT:     ReactOS PSEH (ARM64)
 * LICENSE:     GNU GPL - See COPYING in the top level directory
 * PURPOSE:     SEH2-style macros for AArch64/Windows (table-based SEH)
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * NOTES:       Uses the GCC seh plugin to emit handler data and
 *              a small trampoline to enter filter/finally funclets.
 */

#pragma once

struct _SEH$$_EXCEPTION_RECORD
{
    unsigned long ExceptionCode;
    unsigned long ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    void* ExceptionAddress;
    unsigned long NumberParameters;
    unsigned long long ExceptionInformation[15];
};

struct _SEH$$_EXCEPTION_POINTERS
{
    struct _SEH$$_EXCEPTION_RECORD *ExceptionRecord;
    struct _CONTEXT *ContextRecord;
};

/* Global trampoline for filter and finally funclets */
__asm__(
    ".p2align 4\n"
    "__seh2_global_filter_func:\n"
    /* Prologue (save FP/LR) */
    "\tstp x29, x30, [sp, #-16]!\n"
    "\tmov x29, sp\n"
    /* Jump to funclet address in x8 */
    "\tbr x8\n"
    "__seh2_global_filter_func_exit:\n"
    "\tldp x29, x30, [sp], #16\n"
    "\tret\n");

#define STRINGIFY(a) #a
#define EMIT_PRAGMA_(params) _Pragma( STRINGIFY(params) )
#define EMIT_PRAGMA(type,line) EMIT_PRAGMA_(REACTOS seh(type,line))

#define _SEH3$_EMIT_DEFS_AND_PRAGMA__(Line, Type)                                   \
    __asm__ __volatile__ goto ("\n"                                                 \
        "\t__seh2$$begin_try__" #Line "=%l0\n"   /* Begin of tried code */         \
        "\t__seh2$$end_try__" #Line "=%l1 + 1\n" /* End of tried code */           \
        "\t__seh2$$filter__" #Line "=%l2\n"     /* Filter function */              \
        "\t__seh2$$begin_except__" #Line "=%l3\n" /* Called on except */          \
            : : : : __seh2$$begin_try__, __seh2$$end_try__, __seh2$$filter__, __seh2$$begin_except__); \
    EMIT_PRAGMA(Type,Line)

#define _SEH3$_EMIT_DEFS_AND_PRAGMA_(Line, Type) _SEH3$_EMIT_DEFS_AND_PRAGMA__(Line, Type)
#define _SEH3$_EMIT_DEFS_AND_PRAGMA(Type) _SEH3$_EMIT_DEFS_AND_PRAGMA_(__LINE__, Type)

#define _SEH2_TRY                                                                   \
{                                                                                   \
    __label__ __seh2$$filter__;                                                     \
    __label__ __seh2$$begin_except__;                                               \
    __label__ __seh2$$begin_try__;                                                  \
    __label__ __seh2$$end_try__;                                                    \
__seh2$$begin_try__:                                                                \
    {                                                                               \
        __label__ __seh2$$leave_scope__;

#define _SEH2_EXCEPT(...)                                                                       \
__seh2$$leave_scope__: __MINGW_ATTRIB_UNUSED;                                                   \
    }                                                                                           \
__seh2$$end_try__:(void)0;                                                                      \
    _SEH3$_EMIT_DEFS_AND_PRAGMA(__seh$$except);                                                 \
    if (0)                                                                                      \
    {                                                                                           \
        __label__ __seh2$$leave_scope__;                                                        \
        long __MINGW_ATTRIB_UNUSED __seh2$$exception_code__;                                    \
        if (0)                                                                                  \
        {                                                                                       \
            __label__ __seh2$$filter_funclet__;                                                 \
            __seh2$$filter__:                                                                   \
            __asm__ __volatile__ goto(                                                          \
                "\tadrp x8, %l0\n"                                                             \
                "\tadd  x8, x8, :lo12:%l0\n"                                                    \
                "\tb __seh2_global_filter_func\n"                                              \
                : : : "x8" : __seh2$$filter_funclet__);                                        \
            /* Actually declare our filter funclet */                                           \
            struct _SEH$$_EXCEPTION_POINTERS* __seh2$$exception_ptr__;                          \
            __seh2$$filter_funclet__:                                                           \
            __asm__ __volatile__("mov %0, x0" : "=r"(__seh2$$exception_ptr__) : :              \
                                  "x0","x1","x2","x3","x4","x5","x6","x7");               \
            __seh2$$exception_code__ = __seh2$$exception_ptr__->ExceptionRecord->ExceptionCode; \
            register long __MINGW_ATTRIB_UNUSED __seh2$$filter_funclet_ret __asm__("w0") =      \
                ((__VA_ARGS__));                                                                \
            __asm__("b __seh2_global_filter_func_exit");                                        \
        }                                                                                       \
        enum { __seh2$$abnormal_termination__ = 0 };                                            \
        __seh2$$begin_except__:

#define _SEH2_FINALLY                                                                       \
__seh2$$leave_scope__: __MINGW_ATTRIB_UNUSED;                                               \
    }                                                                                       \
__seh2$$end_try__:                                                                          \
__seh2$$begin_except__: __MINGW_ATTRIB_UNUSED;                                              \
    _SEH3$_EMIT_DEFS_AND_PRAGMA(__seh$$finally);                                            \
    if (1)                                                                                  \
    {                                                                                       \
        __label__ __seh2$$finally__;                                                        \
        __label__ __seh2$$begin_finally__;                                                  \
        __label__ __seh2$$leave_scope__;                                                    \
        __asm__ __volatile__ goto("" : : : : __seh2$$finally__);                            \
        int __seh2$$abnormal_termination__;                                                 \
        if (0)                                                                              \
        {                                                                                   \
            __seh2$$filter__: __MINGW_ATTRIB_UNUSED;                                        \
            __seh2$$finally__: __MINGW_ATTRIB_UNUSED;                                       \
            __asm__ __volatile__ goto(                                                      \
                "\tadrp x8, %l0\n"                                                             \
                "\tadd  x8, x8, :lo12:%l0\n"                                                    \
                "\tb __seh2_global_filter_func\n"                                              \
                : : : "x8" : __seh2$$begin_finally__);                                       \
        }                                                                                   \
        /* Zero-out x0 to indicate normal termination */                                    \
        __asm__ __volatile__("mov x0, xzr" : : :                                             \
            "x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15"); \
        __seh2$$begin_finally__:                                                            \
        __asm__ __volatile__("mov %w0, w0" : "=r" (__seh2$$abnormal_termination__));

#define _SEH2_END                                                                   \
        __seh2$$leave_scope__: __MINGW_ATTRIB_UNUSED;                               \
        if (__seh2$$abnormal_termination__)                                         \
        {                                                                           \
            __asm__("b __seh2_global_filter_func_exit");                            \
        }                                                                           \
    }                                                                               \
}

#define _SEH2_GetExceptionInformation() ((struct _EXCEPTION_POINTERS*)__seh2$$exception_ptr__)
#define _SEH2_GetExceptionCode() __seh2$$exception_code__
#define _SEH2_AbnormalTermination() __seh2$$abnormal_termination__
#define _SEH2_LEAVE goto __seh2$$leave_scope__
#define _SEH2_YIELD(__stmt) __stmt
#define _SEH2_VOLATILE volatile

#if !defined(__cplusplus) || defined(__PSEH_USE_IN_CXX)
#undef __try
#define __try _SEH2_TRY
#define __except _SEH2_EXCEPT
#define __finally _SEH2_FINALLY
#define __endtry _SEH2_END
#define __leave goto __seh2$$leave_scope__
#endif
#define _exception_info() ((struct _EXCEPTION_POINTERS*)__seh2$$exception_ptr__)
#define _exception_code() __seh2$$exception_code__
#define _abnormal_termination() __seh2$$abnormal_termination__
