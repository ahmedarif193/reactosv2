/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 exception handling with frame-pointer backtrace
 */

#define UEFIBOOT 1  /* Enable UEFI-specific features */

#include <uefildr.h>
#include <debug.h>

/* Use WARNING channel for trap diagnostics */
DBG_DEFAULT_CHANNEL(WARNING);

/*--- Pack IDT entries strictly ---*/
#pragma pack(push,1)
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
    USHORT    Limit;
    ULONG_PTR Base;
} IDTR64;
#pragma pack(pop)

/* lidt helper from asm */
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

static IDT_ENTRY64 s_Idt[256];
static IDTR64      s_Idtr;

static USHORT GetCurrentCs(VOID)
{
    USHORT cs;
#if defined(__GNUC__)
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
#else
    cs = 0x08; /* fallback if inline asm is disabled */
#endif
    return cs;
}

static VOID SetIdtGateSel(UCHAR vec, VOID* isr, USHORT sel)
{
    ULONG_PTR addr = (ULONG_PTR)isr;
    IDT_ENTRY64* e = &s_Idt[vec];
    e->OffsetLow  = (USHORT)(addr & 0xFFFF);
    e->Selector   = sel;          /* use current code selector */
    e->Ist        = 0;            /* IST = 0 (can be tuned later) */
    e->TypeAttr   = 0x8E;         /* Present | DPL=0 | 64-bit interrupt gate */
    e->OffsetMid  = (USHORT)((addr >> 16) & 0xFFFF);
    e->OffsetHigh = (ULONG)((addr >> 32) & 0xFFFFFFFF);
    e->Zero       = 0;
}

/* Matches the *exact* push order in Amd64CommonStub */
typedef struct _AMD64_TRAP_CONTEXT {
    /* GPRs saved by stub, in order (top of stack <= r15 first) */
    ULONGLONG r15, r14, r13, r12, r11, r10, r9, r8;
    ULONGLONG rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Pushed by stub prologue right before jump into common: */
    ULONGLONG Vector;
    ULONGLONG ErrorCode;

    /* Then CPU-pushed frame: RIP, CS, RFLAGS, RSP, SS (when CPL change) */
} AMD64_TRAP_CONTEXT, *PAMD64_TRAP_CONTEXT;

/* C entry called by the stub with RCX = &AMD64_TRAP_CONTEXT */
VOID Amd64HandleException(PAMD64_TRAP_CONTEXT Ctx)
{
    /* CPU frame starts right after ErrorCode */
    ULONG_PTR* cpu = (ULONG_PTR*)(Ctx + 1);

    ULONG_PTR rip    = cpu[0];
    USHORT    cs     = (USHORT)cpu[1];
    ULONG_PTR rflags = cpu[2];

    /* If the exception came from CPL=3 (unlikely in UEFI), CPU pushes RSP,SS.
       In CPL=0 we can *show* the RSP as the next slot to keep output sane. */
    ULONG_PTR rsp = ((cs & 0x3) != 0) ? cpu[3] : (ULONG_PTR)(&cpu[3]);

    /* Minimal trap print without headers */
    DbgPrint("TRAP: vector=%llu err=0x%llx\n", Ctx->Vector, Ctx->ErrorCode);
    DbgPrint("  RIP=%p ", (PVOID)rip);
    {
        extern VOID UefiPrintAddressWithSymbol(ULONG_PTR Address);
        UefiPrintAddressWithSymbol(rip);
    }
    DbgPrint("  RSP=%p RBP=%p CS=%04x RFLAGS=%016llx\n", (PVOID)rsp, (PVOID)Ctx->rbp, cs, rflags);
    DbgPrint("  RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n",
             Ctx->rax, Ctx->rbx, Ctx->rcx, Ctx->rdx);
    DbgPrint("  RSI=%016llx RDI=%016llx R8 =%016llx R9 =%016llx\n",
             Ctx->rsi, Ctx->rdi, Ctx->r8, Ctx->r9);
    DbgPrint("  R10=%016llx R11=%016llx R12=%016llx R13=%016llx\n",
             Ctx->r10, Ctx->r11, Ctx->r12, Ctx->r13);
    DbgPrint("  R14=%016llx R15=%016llx\n",
             Ctx->r14, Ctx->r15);

#ifdef UEFIBOOT
    /* Optional simple backtrace using frame pointers (if you keep RBP chain) */
    {
        extern VOID UefiAmd64PrintBacktrace(ULONG_PTR Rbp, ULONG_PTR StackTop, ULONG_PTR StackBottom);

        /* Use a wider stack range for better compatibility */
        ULONG_PTR StackTop    = (rsp | 0xFFFFULL) + 1;  /* round up to 64KiB boundary */
        ULONG_PTR StackBottom = rsp & ~0xFFFFFFULL;      /* round down to 16MB boundary */

        /* If RBP looks suspicious, try using RSP as the starting point */
        ULONG_PTR StartRbp = (ULONG_PTR)Ctx->rbp;
        if (StartRbp < StackBottom || StartRbp >= StackTop) {
            /* RBP is outside stack range, try to walk from current stack */
            /* keep quiet */
            StartRbp = rsp & ~0xF;  /* align to 16 bytes */
        }

        UefiAmd64PrintBacktrace(StartRbp, StackTop, StackBottom);
    }
#endif
}

/* Public init: build a minimal IDT and load it */
VOID Amd64InitializeExceptions(VOID)
{
    RtlZeroMemory(s_Idt, sizeof(s_Idt));

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

    s_Idtr.Base  = (ULONG_PTR)s_Idt;
    s_Idtr.Limit = (USHORT)(sizeof(s_Idt) - 1);
    Amd64SetIdt(&s_Idtr);

    TRACE("AMD64: Exception handlers installed\n");
}
