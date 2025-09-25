/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 exception handling implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

#ifdef __aarch64__

#include <stdint.h>
#include <excpt.h>

/* Windows exception handling structures for ARM64 */
#ifndef _EXCEPTION_RECORD_DEFINED
typedef struct _EXCEPTION_RECORD {
    unsigned long ExceptionCode;
    unsigned long ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    void *ExceptionAddress;
    unsigned long NumberParameters;
    unsigned long long ExceptionInformation[15];
} EXCEPTION_RECORD;
#define _EXCEPTION_RECORD_DEFINED
#endif

#ifndef _CONTEXT_DEFINED
typedef struct _CONTEXT CONTEXT;
#define _CONTEXT_DEFINED
#endif

#ifndef _DISPATCHER_CONTEXT_DEFINED
typedef struct _DISPATCHER_CONTEXT {
    unsigned long long ControlPc;
    unsigned long long ImageBase;
    void *FunctionEntry;
    unsigned long long EstablisherFrame;
    unsigned long long TargetPc;
    CONTEXT *ContextRecord;
    void *LanguageHandler;
    void *HandlerData;
    void *HistoryTable;
    unsigned long ScopeIndex;
    unsigned long Fill0;
} DISPATCHER_CONTEXT;
#define _DISPATCHER_CONTEXT_DEFINED
#endif

/* _EXCEPTION_DISPOSITION already defined in excpt.h */

/* ARM64 scope table structures */
typedef struct _SCOPE_TABLE_ARM64 {
    unsigned long Count;
    struct {
        unsigned long BeginAddress;
        unsigned long EndAddress;
        unsigned long HandlerAddress;
        unsigned long JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE_ARM64, *PSCOPE_TABLE_ARM64;

/* Exception pointers structure */
#ifndef _EXCEPTION_POINTERS_DEFINED
typedef struct _EXCEPTION_POINTERS {
    EXCEPTION_RECORD *ExceptionRecord;
    CONTEXT *ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;
#define _EXCEPTION_POINTERS_DEFINED
#endif

/* Function pointer types */
typedef void (*PTERMINATION_HANDLER)(unsigned char AbnormalTermination, void *EstablisherFrame);
typedef long (*PEXCEPTION_FILTER)(PEXCEPTION_POINTERS ExceptionPointers, void *EstablisherFrame);

/* Exception handling constants */
#define EXCEPTION_EXECUTE_HANDLER      1
#define EXCEPTION_CONTINUE_SEARCH      0
#define EXCEPTION_CONTINUE_EXECUTION  -1

#define EXCEPTION_UNWINDING            0x02
#define EXCEPTION_EXIT_UNWIND          0x04
#define EXCEPTION_STACK_INVALID        0x08
#define EXCEPTION_NESTED_CALL          0x10
#define EXCEPTION_TARGET_UNWIND        0x20
#define EXCEPTION_COLLIDED_UNWIND      0x40
#define EXCEPTION_UNWIND               0x66

/* External declarations for ARM64 unwind functions */
extern void __cdecl RtlUnwindEx(
    void *TargetFrame,
    void *TargetIp,
    EXCEPTION_RECORD *ExceptionRecord,
    void *ReturnValue,
    CONTEXT *ContextRecord,
    void *HistoryTable);

extern unsigned long long __cdecl UlongToPtr(unsigned long Value);

/* C specific exception handler for ARM64 */
EXCEPTION_DISPOSITION __C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    CONTEXT *ContextRecord,
    DISPATCHER_CONTEXT *DispatcherContext)
{
    PSCOPE_TABLE_ARM64 ScopeTable;
    unsigned long i, BeginAddress, EndAddress, Handler;
    unsigned long long ImageBase, JumpTarget, IpOffset, TargetIpOffset;
    EXCEPTION_POINTERS ExceptionPointers;
    PTERMINATION_HANDLER TerminationHandler;
    PEXCEPTION_FILTER ExceptionFilter;
    long FilterResult;

    /* Set up the EXCEPTION_POINTERS */
    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;

    /* Get the image base */
    ImageBase = (unsigned long long)DispatcherContext->ImageBase;

    /* Get the image base relative instruction pointers */
    IpOffset = DispatcherContext->ControlPc - ImageBase;
    TargetIpOffset = DispatcherContext->TargetPc - ImageBase;

    /* Get the scope table and current index */
    ScopeTable = (PSCOPE_TABLE_ARM64)DispatcherContext->HandlerData;

    /* Validate scope table */
    if (!ScopeTable) {
        return ExceptionContinueSearch;
    }

    /* Loop while we have scope table entries */
    while (DispatcherContext->ScopeIndex < ScopeTable->Count)
    {
        /* Use i as index and update the dispatcher context */
        i = DispatcherContext->ScopeIndex++;

        /* Get the start and end of the scope */
        BeginAddress = ScopeTable->ScopeRecord[i].BeginAddress;
        EndAddress = ScopeTable->ScopeRecord[i].EndAddress;

        /* Skip this scope if we are not within the bounds */
        if ((IpOffset < BeginAddress) || (IpOffset >= EndAddress))
        {
            continue;
        }

        /* Check if this is an unwind */
        if (ExceptionRecord->ExceptionFlags & EXCEPTION_UNWIND)
        {
            /* Check if this is a target unwind */
            if (ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND)
            {
                /* Check if the target is within the scope itself */
                if ((TargetIpOffset >= BeginAddress) &&
                    (TargetIpOffset <  EndAddress))
                {
                    return ExceptionContinueSearch;
                }
            }

            /* Check if this is a termination handler / finally function */
            if (ScopeTable->ScopeRecord[i].JumpTarget == 0)
            {
                /* Call the handler */
                Handler = ScopeTable->ScopeRecord[i].HandlerAddress;
                if (Handler != 0) {
                    TerminationHandler = (PTERMINATION_HANDLER)(ImageBase + Handler);
                    TerminationHandler(1, EstablisherFrame);  /* TRUE for abnormal termination during unwind */
                }
            }
            else if (ScopeTable->ScopeRecord[i].JumpTarget == TargetIpOffset)
            {
                return ExceptionContinueSearch;
            }
        }
        else
        {
            /* We are only interested in exception handlers */
            if (ScopeTable->ScopeRecord[i].JumpTarget == 0)
            {
                continue;
            }

            /* This is an exception filter, get the handler address */
            Handler = ScopeTable->ScopeRecord[i].HandlerAddress;

            /* Check for hardcoded EXCEPTION_EXECUTE_HANDLER */
            if (Handler == EXCEPTION_EXECUTE_HANDLER)
            {
                /* This is our result */
                FilterResult = EXCEPTION_EXECUTE_HANDLER;
            }
            else if (Handler != 0)
            {
                /* Otherwise we need to call the handler */
                ExceptionFilter = (PEXCEPTION_FILTER)(ImageBase + Handler);
                FilterResult = ExceptionFilter(&ExceptionPointers, EstablisherFrame);
            }
            else
            {
                /* No handler, continue search */
                continue;
            }

            if (FilterResult < 0 /* EXCEPTION_CONTINUE_EXECUTION */)
            {
                return ExceptionContinueExecution;
            }

            if (FilterResult > 0 /* EXCEPTION_EXECUTE_HANDLER */)
            {
                JumpTarget = (ImageBase + ScopeTable->ScopeRecord[i].JumpTarget);

                /* Unwind to the target address (This does not return) */
                RtlUnwindEx(EstablisherFrame,
                            (void*)JumpTarget,
                            ExceptionRecord,
                            (void*)UlongToPtr(ExceptionRecord->ExceptionCode),
                            DispatcherContext->ContextRecord,
                            DispatcherContext->HistoryTable);

                /* Should not get here */
                __builtin_trap();  /* ARM64 equivalent of __debugbreak() */
            }
        }
    }

    /* Reached the end of the scope table */
    return ExceptionContinueSearch;
}

/* Local unwind function for ARM64 */
void __cdecl _local_unwind(void* frame, void* target)
{
    /* On ARM64, use RtlUnwindEx instead of RtlUnwind */
    RtlUnwindEx(frame, target, NULL, 0, NULL, NULL);
}

/* ARM64 C++ personality routine for handling C++ exceptions */
EXCEPTION_DISPOSITION __gxx_personality_v0(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    CONTEXT *ContextRecord,
    DISPATCHER_CONTEXT *DispatcherContext)
{
    /* For ARM64 C++ exceptions, delegate to __C_specific_handler
     * This is a simplified approach - a full implementation would
     * handle C++ exception matching, virtual base classes, etc.
     */
    return __C_specific_handler(ExceptionRecord, EstablisherFrame,
                               ContextRecord, DispatcherContext);
}

/* ARM64 GCC personality routine (alternative name) */
EXCEPTION_DISPOSITION __gxx_personality_arm64(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    CONTEXT *ContextRecord,
    DISPATCHER_CONTEXT *DispatcherContext)
{
    return __gxx_personality_v0(ExceptionRecord, EstablisherFrame,
                               ContextRecord, DispatcherContext);
}

/* Helper function for UlongToPtr conversion */
unsigned long long __cdecl UlongToPtr(unsigned long Value)
{
    return (unsigned long long)Value;
}

#endif /* __aarch64__ */