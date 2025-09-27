/*
 * Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 stubs
 * FILE:            lib/rtl/arm64/stubs.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
RtlInitializeContext(
    _Reserved_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB StackBase)
{
    /* Zero the context */
    RtlZeroMemory(ThreadContext, sizeof(CONTEXT));

    /* Setup basic context flags */
    ThreadContext->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

    /* Set the thread start address */
    ThreadContext->Pc = (DWORD64)ThreadStartAddress;

    /* Set the first parameter */
    ThreadContext->X0 = (DWORD64)ThreadStartParam;

    /* Set the stack pointer */
    ThreadContext->Sp = (DWORD64)StackBase->StackLimit;

    /* TODO: Implement full context initialization if needed */
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    DPRINT1("RtlDispatchException: stub for ARM64\n");
    /* TODO: Implement exception dispatch */
    return FALSE;
}


VOID
NTAPI
RtlCaptureContext(
    _Out_ PCONTEXT Context)
{
    ULONG64 sp, fp, lr, pc;
    ULONG64 x0, x1, x2, x3, x4, x5, x6, x7;
    ULONG64 x8, x9, x10, x11, x12, x13, x14, x15;
    ULONG64 x16, x17, x18, x19, x20, x21, x22, x23;
    ULONG64 x24, x25, x26, x27, x28;

    RtlZeroMemory(Context, sizeof(CONTEXT));
    Context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

    /* Capture general purpose registers X0..X28 */
    __asm__ __volatile__("mov %0, x0"  : "=r"(x0));
    __asm__ __volatile__("mov %0, x1"  : "=r"(x1));
    __asm__ __volatile__("mov %0, x2"  : "=r"(x2));
    __asm__ __volatile__("mov %0, x3"  : "=r"(x3));
    __asm__ __volatile__("mov %0, x4"  : "=r"(x4));
    __asm__ __volatile__("mov %0, x5"  : "=r"(x5));
    __asm__ __volatile__("mov %0, x6"  : "=r"(x6));
    __asm__ __volatile__("mov %0, x7"  : "=r"(x7));
    __asm__ __volatile__("mov %0, x8"  : "=r"(x8));
    __asm__ __volatile__("mov %0, x9"  : "=r"(x9));
    __asm__ __volatile__("mov %0, x10" : "=r"(x10));
    __asm__ __volatile__("mov %0, x11" : "=r"(x11));
    __asm__ __volatile__("mov %0, x12" : "=r"(x12));
    __asm__ __volatile__("mov %0, x13" : "=r"(x13));
    __asm__ __volatile__("mov %0, x14" : "=r"(x14));
    __asm__ __volatile__("mov %0, x15" : "=r"(x15));
    __asm__ __volatile__("mov %0, x16" : "=r"(x16));
    __asm__ __volatile__("mov %0, x17" : "=r"(x17));
    __asm__ __volatile__("mov %0, x18" : "=r"(x18));
    __asm__ __volatile__("mov %0, x19" : "=r"(x19));
    __asm__ __volatile__("mov %0, x20" : "=r"(x20));
    __asm__ __volatile__("mov %0, x21" : "=r"(x21));
    __asm__ __volatile__("mov %0, x22" : "=r"(x22));
    __asm__ __volatile__("mov %0, x23" : "=r"(x23));
    __asm__ __volatile__("mov %0, x24" : "=r"(x24));
    __asm__ __volatile__("mov %0, x25" : "=r"(x25));
    __asm__ __volatile__("mov %0, x26" : "=r"(x26));
    __asm__ __volatile__("mov %0, x27" : "=r"(x27));
    __asm__ __volatile__("mov %0, x28" : "=r"(x28));

    Context->X[0]  = x0;  Context->X[1]  = x1;  Context->X[2]  = x2;  Context->X[3]  = x3;
    Context->X[4]  = x4;  Context->X[5]  = x5;  Context->X[6]  = x6;  Context->X[7]  = x7;
    Context->X[8]  = x8;  Context->X[9]  = x9;  Context->X[10] = x10; Context->X[11] = x11;
    Context->X[12] = x12; Context->X[13] = x13; Context->X[14] = x14; Context->X[15] = x15;
    Context->X[16] = x16; Context->X[17] = x17; Context->X[18] = x18; Context->X[19] = x19;
    Context->X[20] = x20; Context->X[21] = x21; Context->X[22] = x22; Context->X[23] = x23;
    Context->X[24] = x24; Context->X[25] = x25; Context->X[26] = x26; Context->X[27] = x27;
    Context->X[28] = x28;

    /* Capture a minimal but useful subset: SP, FP, LR, PC, FPCR/FPSR */
    __asm__ __volatile__("mov %0, sp" : "=r"(sp));
    __asm__ __volatile__("mov %0, x29" : "=r"(fp));
    __asm__ __volatile__("mov %0, x30" : "=r"(lr));
    __asm__ __volatile__("adr %0, .+0" : "=r"(pc));

    Context->Sp = sp;
    Context->Fp = fp;
    Context->Lr = lr;
    Context->Pc = pc;

    /* Floating-point status/control (optional) */
    {
        ULONG64 _fpcr, _fpsr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(_fpcr));
        __asm__ __volatile__("mrs %0, fpsr" : "=r"(_fpsr));
        Context->Fpcr = (ULONG)_fpcr;
        Context->Fpsr = (ULONG)_fpsr;
    }
}
