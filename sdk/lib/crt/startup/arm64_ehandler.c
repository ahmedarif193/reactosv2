/*
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         ARM64 exception handling stubs
 * PROGRAMMER:      Claude AI (for ARM64 port compatibility)
 */

/* Stub implementations for missing ARM64 exception handling functions */

#ifdef __aarch64__

#include <stdint.h>

/* Forward declarations for unwind functions */
typedef struct _Unwind_Context _Unwind_Context;
typedef struct _Unwind_Exception _Unwind_Exception;

/* Reason codes for _Unwind_Action */
typedef int _Unwind_Action;
typedef int _Unwind_Reason_Code;

/* __mingw_init_ehandler is already implemented in crt_handler.c for ARM64 */

/* Basic stub implementations to prevent linker errors */

uintptr_t _Unwind_GetTextRelBase(_Unwind_Context *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetDataRelBase(_Unwind_Context *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetRegionStart(_Unwind_Context *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetLanguageSpecificData(_Unwind_Context *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetIPInfo(_Unwind_Context *context, int *ip_before_insn)
{
    (void)context;
    if (ip_before_insn) *ip_before_insn = 0;
    return 0;
}

void _Unwind_SetGR(_Unwind_Context *context, int index, uintptr_t value)
{
    (void)context;
    (void)index;
    (void)value;
}

void _Unwind_SetIP(_Unwind_Context *context, uintptr_t value)
{
    (void)context;
    (void)value;
}

void _Unwind_Resume(_Unwind_Exception *exception)
{
    (void)exception;
    /* This should not return, but we'll terminate to prevent infinite loops */
    __builtin_trap();
}

_Unwind_Reason_Code _Unwind_RaiseException(_Unwind_Exception *exception)
{
    (void)exception;
    return 5; /* _URC_END_OF_STACK */
}

_Unwind_Reason_Code _Unwind_Resume_or_Rethrow(_Unwind_Exception *exception)
{
    (void)exception;
    return 5; /* _URC_END_OF_STACK */
}

void _Unwind_DeleteException(_Unwind_Exception *exception)
{
    (void)exception;
    /* Nothing to do for stubs */
}

/* MinGW-specific setjmp/longjmp stubs */
int __mingw_setjmp(void *jmp_buf)
{
    /* Use the standard setjmp implementation */
    extern int _setjmp(void *);
    return _setjmp(jmp_buf);
}

void __mingw_longjmp(void *jmp_buf, int value)
{
    /* Use the standard longjmp implementation */
    extern void longjmp(void *, int);
    longjmp(jmp_buf, value);
}

/* Windows exception handling for ARM64 */
typedef struct _EXCEPTION_RECORD {
    unsigned long ExceptionCode;
    unsigned long ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    void *ExceptionAddress;
    unsigned long NumberParameters;
    uintptr_t ExceptionInformation[15];
} EXCEPTION_RECORD;

typedef struct _CONTEXT CONTEXT;

typedef struct _DISPATCHER_CONTEXT {
    uintptr_t ControlPc;
    uintptr_t ImageBase;
    void *FunctionEntry;
    uintptr_t EstablisherFrame;
    uintptr_t TargetPc;
    CONTEXT *ContextRecord;
    void *LanguageHandler;
    void *HandlerData;
    void *HistoryTable;
    unsigned long ScopeIndex;
    unsigned long Fill0;
} DISPATCHER_CONTEXT;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;

/* C specific exception handler for ARM64 */
EXCEPTION_DISPOSITION __C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    CONTEXT *ContextRecord,
    DISPATCHER_CONTEXT *DispatcherContext)
{
    /* Basic stub implementation - just continue search */
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;

    return ExceptionContinueSearch;
}

#endif /* __aarch64__ */