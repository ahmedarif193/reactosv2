/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 exception handling with frame-pointer backtrace
 */

#include <uefildr.h>
#include <debug.h>

/* Default to WARNING channel for trap diagnostics */
DBG_DEFAULT_CHANNEL(WARNING);

typedef struct _IDT_ENTRY64 {
    USHORT OffsetLow;
    USHORT Selector;
    UCHAR  Ist;
    UCHAR  TypeAttr;
    USHORT OffsetMid;
    ULONG  OffsetHigh;
    ULONG  Zero;
} IDT_ENTRY64;

typedef struct _IDTR64 {
    USHORT Limit;
    ULONG_PTR Base;
} IDTR64;

extern VOID Amd64SetIdt(IDTR64* Idtr);

/* Exception stubs from assembly */
extern VOID Amd64Exc0(VOID);  extern VOID Amd64Exc1(VOID);
extern VOID Amd64Exc2(VOID);  extern VOID Amd64Exc3(VOID);
extern VOID Amd64Exc4(VOID);  extern VOID Amd64Exc5(VOID);
extern VOID Amd64Exc6(VOID);  extern VOID Amd64Exc7(VOID);
extern VOID Amd64Exc8(VOID);  extern VOID Amd64Exc9(VOID);
extern VOID Amd64Exc10(VOID); extern VOID Amd64Exc11(VOID);
extern VOID Amd64Exc12(VOID); extern VOID Amd64Exc13(VOID);
extern VOID Amd64Exc14(VOID); extern VOID Amd64Exc15(VOID);
extern VOID Amd64Exc16(VOID); extern VOID Amd64Exc17(VOID);
extern VOID Amd64Exc18(VOID); extern VOID Amd64Exc19(VOID);
extern VOID Amd64Exc20(VOID); extern VOID Amd64Exc21(VOID);
extern VOID Amd64Exc22(VOID); extern VOID Amd64Exc23(VOID);
extern VOID Amd64Exc24(VOID); extern VOID Amd64Exc25(VOID);
extern VOID Amd64Exc26(VOID); extern VOID Amd64Exc27(VOID);
extern VOID Amd64Exc28(VOID); extern VOID Amd64Exc29(VOID);
extern VOID Amd64Exc30(VOID); extern VOID Amd64Exc31(VOID);

static IDT_ENTRY64 Idt[256];
static IDTR64     Idtr;

static USHORT GetCurrentCs(VOID)
{
    USHORT cs;
#if defined(__GNUC__)
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
#else
    cs = 0x08; /* fallback */
#endif
    return cs;
}

static VOID SetIdtGateSel(UCHAR vec, VOID* isr, USHORT sel)
{
    ULONG_PTR addr = (ULONG_PTR)isr;
    IDT_ENTRY64* e = &Idt[vec];
    e->OffsetLow  = (USHORT)(addr & 0xFFFF);
    e->Selector   = sel;          /* current code selector */
    e->Ist        = 0;
    e->TypeAttr   = 0x8E;         /* present, DPL=0, interrupt gate */
    e->OffsetMid  = (USHORT)((addr >> 16) & 0xFFFF);
    e->OffsetHigh = (ULONG)((addr >> 32) & 0xFFFFFFFF);
    e->Zero       = 0;
}

/* Minimal trap frame view matching our stub save order */
typedef struct _AMD64_TRAP_CONTEXT {
    /* pushed by stub */
    ULONGLONG r15, r14, r13, r12, r11, r10, r9, r8;
    ULONGLONG rdi, rsi, rbp, rbx, rdx, rcx, rax;
    ULONGLONG Vector;
    ULONGLONG ErrorCode;
    /* then CPU-pushed frame: RIP, CS, RFLAGS, RSP, SS (depending on CPL) */
} AMD64_TRAP_CONTEXT, *PAMD64_TRAP_CONTEXT;

VOID Amd64HandleException(PAMD64_TRAP_CONTEXT Ctx)
{
    /* Compute where the CPU frame lives: it's above our saved regs, vector, error */
    ULONG_PTR* cpu = (ULONG_PTR*)(Ctx + 1);
    ULONG_PTR rip = cpu[0];

    /* The hardware frame is: [RIP, CS, RFLAGS] and, on CPL change, [+RSP, +SS].
       We cannot read the original RSP without a CPL change; instead show the
       RSP-at-exception as the top of the hardware frame (cpu + 3) when no CPL
       change, or the saved RSP (cpu[3]) when CPL change occurred. Detect CPL
       change by comparing the low 2 bits of saved CS to 0 (kernel). */
    USHORT cs = (USHORT)cpu[1];
    ULONG_PTR rsp;
    if ((cs & 0x3) != 0)
    {
        /* Came from non-zero CPL -> RSP and SS were pushed */
        rsp = cpu[3];
    }
    else
    {
        /* No CPL change -> approximate RSP as the address above the frame */
        rsp = (ULONG_PTR)(&cpu[3]);
    }

    ERR("\n===============================================================\n");
    ERR("AMD64 Exception vector %llu, error 0x%llx\n", Ctx->Vector, Ctx->ErrorCode);
    ERR("RIP=%p RSP=%p RBP=%p\n", (PVOID)rip, (PVOID)rsp, (PVOID)Ctx->rbp);

#ifdef UEFIBOOT
    {
        extern VOID UefiAmd64PrintBacktrace(ULONG_PTR Rbp, ULONG_PTR StackTop, ULONG_PTR StackBottom);
        ULONG_PTR StackTop = (rsp + 0xFFFF) & ~0xFFFFULL;    /* assume 64KB-aligned region */
        ULONG_PTR StackBottom = StackTop - 0x10000ULL;
        UefiAmd64PrintBacktrace((ULONG_PTR)Ctx->rbp, StackTop, StackBottom);
    }
#endif

    ERR("===============================================================\n");
}

VOID Amd64InitializeExceptions(VOID)
{
    /* Clear IDT and set entries for exceptions 0..31 */
    RtlZeroMemory(Idt, sizeof(Idt));
    USHORT sel = GetCurrentCs();
    SetIdtGateSel(0,  Amd64Exc0,  sel);   SetIdtGateSel(1,  Amd64Exc1,  sel);
    SetIdtGateSel(2,  Amd64Exc2,  sel);   SetIdtGateSel(3,  Amd64Exc3,  sel);
    SetIdtGateSel(4,  Amd64Exc4,  sel);   SetIdtGateSel(5,  Amd64Exc5,  sel);
    SetIdtGateSel(6,  Amd64Exc6,  sel);   SetIdtGateSel(7,  Amd64Exc7,  sel);
    SetIdtGateSel(8,  Amd64Exc8,  sel);   SetIdtGateSel(9,  Amd64Exc9,  sel);
    SetIdtGateSel(10, Amd64Exc10, sel);   SetIdtGateSel(11, Amd64Exc11, sel);
    SetIdtGateSel(12, Amd64Exc12, sel);   SetIdtGateSel(13, Amd64Exc13, sel);
    SetIdtGateSel(14, Amd64Exc14, sel);   SetIdtGateSel(15, Amd64Exc15, sel);
    SetIdtGateSel(16, Amd64Exc16, sel);   SetIdtGateSel(17, Amd64Exc17, sel);
    SetIdtGateSel(18, Amd64Exc18, sel);   SetIdtGateSel(19, Amd64Exc19, sel);
    SetIdtGateSel(20, Amd64Exc20, sel);   SetIdtGateSel(21, Amd64Exc21, sel);
    SetIdtGateSel(22, Amd64Exc22, sel);   SetIdtGateSel(23, Amd64Exc23, sel);
    SetIdtGateSel(24, Amd64Exc24, sel);   SetIdtGateSel(25, Amd64Exc25, sel);
    SetIdtGateSel(26, Amd64Exc26, sel);   SetIdtGateSel(27, Amd64Exc27, sel);
    SetIdtGateSel(28, Amd64Exc28, sel);   SetIdtGateSel(29, Amd64Exc29, sel);
    SetIdtGateSel(30, Amd64Exc30, sel);   SetIdtGateSel(31, Amd64Exc31, sel);

    Idtr.Base  = (ULONG_PTR)Idt;
    Idtr.Limit = (USHORT)(sizeof(Idt) - 1);
    Amd64SetIdt(&Idtr);

    TRACE("AMD64: Exception handlers installed\n");
}
