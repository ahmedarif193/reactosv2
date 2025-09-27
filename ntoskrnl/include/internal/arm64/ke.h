#ifndef _NTOSKRNL_INCLUDE_INTERNAL_ARM64_KE_H
#define _NTOSKRNL_INCLUDE_INTERNAL_ARM64_KE_H

#pragma once

#include "intrin_i.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ARM64 Kernel Executive Definitions */

/* Make KPCR visible using existing KIPCR definition */
typedef KIPCR KPCR, *PKPCR;

#define KiServiceExit2 KiExceptionExit

#ifndef SYNCH_LEVEL
#define SYNCH_LEVEL DISPATCH_LEVEL
#endif

/* ARM64 specific addresses */
#define ARM64_PCR_ADDRESS           0xFFFFF80000000000ULL
#define ARM64_SHARED_USER_DATA      0x7FFE0000ULL

/* ARM64 PCR (Processor Control Region) */
#define PCR (KeGetPcr())

/* ARM64 breakpoint definitions */
#define KD_BREAKPOINT_TYPE          ULONG
#define KD_BREAKPOINT_SIZE          sizeof(ULONG) 
#define KD_BREAKPOINT_VALUE         0xD4200000  /* BRK #0 instruction */

/* Maximum interrupt vectors for ARM64 GIC */
#define MAXIMUM_VECTOR              1024
#define MAXIMUM_BUILTIN_VECTOR      32

/* ARM64 page size definitions */
#ifndef PAGE_SIZE
#define PAGE_SIZE                   0x1000      /* 4KB pages */
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT                  12
#endif

/* ARM64 Exception Levels */
#define ARM64_EL0                   0
#define ARM64_EL1                   1
#define ARM64_EL2                   2
#define ARM64_EL3                   3

/* ARM64 System Register Access Macros */
#define ARM64_READ_SYSREG(reg) \
    ({ \
        ULONGLONG __val; \
        __asm__ volatile("mrs %0, " #reg : "=r" (__val)); \
        __val; \
    })

#define ARM64_WRITE_SYSREG(reg, val) \
    do { \
        __asm__ volatile("msr " #reg ", %0" :: "r" ((ULONGLONG)(val))); \
    } while(0)

/* ARM64 Memory Barrier Macros */
#define ARM64_DMB_SY()      __asm__ volatile("dmb sy" ::: "memory")
#define ARM64_DSB_SY()      __asm__ volatile("dsb sy" ::: "memory")
#define ARM64_ISB()         __asm__ volatile("isb" ::: "memory")

/* Context and trap frame manipulation macros */
#define KeGetContextPc(Context) \
    ((Context)->Pc)

#define KeSetContextPc(Context, ProgramCounter) \
    ((Context)->Pc = (ProgramCounter))

#define KeGetTrapFramePc(TrapFrame) \
    ((TrapFrame)->Pc)

#define KeGetContextReturnRegister(Context) \
    ((Context)->X0)

#define KeSetContextReturnRegister(Context, ReturnValue) \
    ((Context)->X0 = (ReturnValue))

/* Macro to get trap and exception frame from thread stack */
#define KeGetTrapFrame(Thread) \
    (PKTRAP_FRAME)((ULONG_PTR)((Thread)->InitialStack) - \
                   ALIGN_UP(sizeof(KTRAP_FRAME), STACK_ALIGN))

#define KeGetExceptionFrame(Thread) \
    (PKEXCEPTION_FRAME)((ULONG_PTR)KeGetTrapFrame(Thread) - \
                        ALIGN_UP(sizeof(KEXCEPTION_FRAME), STACK_ALIGN))

/* ARM64 Thread initialization macro */
#define KeInitializeThread(Process, Thread, SystemRoutine, StartRoutine, \
                          StartContext, ContextFrame, Teb, KernelStack) \
    KiInitializeThread(Process, Thread, SystemRoutine, StartRoutine, \
                      StartContext, ContextFrame, Teb, KernelStack)

/* ARM64 Quantum constants */
#define THREAD_QUANTUM             6        /* Default quantum */
#define THREAD_QUANTUM_MIN         2        /* Minimum quantum */
#define THREAD_QUANTUM_MAX         12       /* Maximum quantum */

/* ARM64 specific CPU features */
#define ARM64_FEATURE_AES           0x00000001
#define ARM64_FEATURE_SHA           0x00000002
#define ARM64_FEATURE_FP            0x00000004
#define ARM64_FEATURE_ASIMD         0x00000008
#define ARM64_FEATURE_CRC32         0x00000010
#define ARM64_FEATURE_ATOMIC        0x00000020
#define ARM64_FEATURE_RAS           0x00000040
#define ARM64_FEATURE_SVE           0x00000080
#define ARM64_FEATURE_PAC           0x00000100  /* Pointer Authentication */
#define ARM64_FEATURE_BTI           0x00000200  /* Branch Target Identification */
#define ARM64_FEATURE_MTE           0x00000400  /* Memory Tagging Extensions */

/* Global ARM64 feature flags */
extern ULONG KiArmV8Features;
extern BOOLEAN KiArmV83PacSupported;
extern BOOLEAN KiArmV85BtiSupported;
extern BOOLEAN KiArmV85MteSupported;

/* ARM64 cache operations */
#define ARM64_DC_CIVAC(addr)    __asm__ volatile("dc civac, %0" :: "r" (addr) : "memory")
#define ARM64_DC_CVAU(addr)     __asm__ volatile("dc cvau, %0" :: "r" (addr) : "memory")
#define ARM64_DC_CVAC(addr)     __asm__ volatile("dc cvac, %0" :: "r" (addr) : "memory")
#define ARM64_DC_IVAC(addr)     __asm__ volatile("dc ivac, %0" :: "r" (addr) : "memory")
#define ARM64_IC_IVAU(addr)     __asm__ volatile("ic ivau, %0" :: "r" (addr) : "memory")

/* ARM64 TLB operations */
#define ARM64_TLBI_VMALLE1()    __asm__ volatile("tlbi vmalle1" ::: "memory")
#define ARM64_TLBI_VAE1(addr)   __asm__ volatile("tlbi vae1, %0" :: "r" (addr) : "memory")

/* ARM64 interrupt control */
#define ARM64_DISABLE_INTERRUPTS() \
    __asm__ volatile("msr daifset, #0xF" ::: "memory")

#define ARM64_ENABLE_INTERRUPTS() \
    __asm__ volatile("msr daifclr, #0xF" ::: "memory")

#define ARM64_SAVE_DISABLE_INTERRUPTS() \
    ({ \
        ULONGLONG __daif; \
        __asm__ volatile("mrs %0, daif; msr daifset, #0xF" : "=r" (__daif) :: "memory"); \
        __daif; \
    })

#define ARM64_RESTORE_INTERRUPTS(daif) \
    __asm__ volatile("msr daif, %0" :: "r" (daif) : "memory")

/* ARM64 context preservation for DPC/APC handlers */
#define ARM64_SAVE_NEON_CONTEXT() \
    do { \
        ULONG64 __fpcr, __fpsr; \
        __asm__ volatile( \
            "mrs %0, fpcr\n" \
            "mrs %1, fpsr\n" \
            : "=r" (__fpcr), "=r" (__fpsr) :: "memory"); \
        KeGetCurrentThread()->FpcrSaved = __fpcr; \
        KeGetCurrentThread()->FpsrSaved = __fpsr; \
    } while(0)

#define ARM64_RESTORE_NEON_CONTEXT() \
    do { \
        ULONG64 __fpcr = KeGetCurrentThread()->FpcrSaved; \
        ULONG64 __fpsr = KeGetCurrentThread()->FpsrSaved; \
        __asm__ volatile( \
            "msr fpcr, %0\n" \
            "msr fpsr, %1\n" \
            :: "r" (__fpcr), "r" (__fpsr) : "memory"); \
    } while(0)

/* ARM64 pointer authentication context preservation */
#define ARM64_SAVE_PAC_KEYS() \
    do { \
        if (KiArmV83PacSupported) { \
            /* Save pointer authentication keys if supported */ \
            /* This would save APIA, APIB, APDA, APDB, APGA keys */ \
            /* Implementation depends on kernel policy */ \
        } \
    } while(0)

#define ARM64_RESTORE_PAC_KEYS() \
    do { \
        if (KiArmV83PacSupported) { \
            /* Restore pointer authentication keys if supported */ \
        } \
    } while(0)

/* Stack alignment for ARM64 */
#define STACK_ALIGN                 16

/* ARM64 DPC/APC context preservation functions */
VOID
NTAPI
KiSaveFloatingPointState(
    IN PKTHREAD Thread
);

VOID
NTAPI
KiRestoreFloatingPointState(
    IN PKTHREAD Thread
);

VOID
NTAPI
KiSaveSveLengthContext(
    IN PKTHREAD Thread
);

VOID
NTAPI
KiRestoreSveLengthContext(
    IN PKTHREAD Thread
);

/* ARM64 kernel function declarations */
NTSTATUS
NTAPI
KiInitializeKernel(
    IN PKPROCESS InitProcess,
    IN PKTHREAD InitThread,
    IN PVOID IdleStack,
    IN PKPRCB Prcb,
    IN CCHAR Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

/* ARM64 doesn't have GDT/TSS - these are x86/x64 specific */

VOID
NTAPI 
KiInitializeCpu(
    IN PKIPCR Pcr
);

VOID
NTAPI
KiSetCacheInformation(
    VOID
);

/* KiInitMachineDependent - declared in generic headers */

VOID
NTAPI
KiInitializeInterrupts(
    VOID
);

/* ARM64 Exception and interrupt handlers */
VOID KiTrapHandler(VOID);
VOID KiInterruptHandler(VOID);
VOID KiFiqHandler(VOID);
VOID KiSerrorHandler(VOID);
VOID KiSystemCallHandler64(VOID);
VOID KiSystemCallHandler32(VOID);
VOID KiUnexpectedInterrupt(VOID);

/* ARM64 Context switching functions - ARM64 specific signatures */
VOID
FASTCALL
KiSwapContextARM64(
    IN PKTHREAD OldThread,
    IN PKTHREAD NewThread);

/* KiSwapContext is declared in generic ke.h - no ARM64 specific version needed */

/* ARM64 specific PCR access */
#define KeGetCurrentPrcb() (&(KeGetPcr()->Prcb))

/* ARM64 IRQL function declarations */
VOID
NTAPI
_KfLowerIrql(
    IN KIRQL NewIrql
);

KIRQL
NTAPI
_KfRaiseIrql(
    IN KIRQL NewIrql
);

BOOLEAN
NTAPI
_KfEnable(VOID);

BOOLEAN
NTAPI
_KfDisable(VOID);

/* ARM64 IRQL manipulation (using GIC priority) */
#define KfLowerIrql(NewIrql) \
    _KfLowerIrql(NewIrql)

#define KfRaiseIrql(NewIrql) \
    _KfRaiseIrql(NewIrql)

/* ARM64 specific memory management helpers */
/* MmIsRecursiveIoFault is implemented as a function in mmsup.c */

/* ARM64 Memory Layout - MM_SYSTEM_RANGE_START defined in mm.h */
extern PVOID MmHighestUserAddress;
extern PVOID MmSystemRangeStart;

/* KeTickCount external declaration */
extern volatile KSYSTEM_TIME KeTickCount;

/* ARM64 Performance Measurement (stub for x86 compatibility) */
#define Ki386PerfEnd()

/* ARM64 spinlock operations using LDXR/STXR for better performance */
#define Arm64AcquireSpinLock(SpinLock) do { \
    ULONG tmp; \
    __asm__ volatile( \
        "1: ldxr    %w0, [%1]\n" \
        "   cbnz    %w0, 2f\n" \
        "   mov     %w0, #1\n" \
        "   stxr    %w2, %w0, [%1]\n" \
        "   cbnz    %w2, 1b\n" \
        "   dmb     sy\n" \
        "   b       3f\n" \
        "2: yield\n" \
        "   b       1b\n" \
        "3:\n" \
        : "=&r" (tmp), "+Q" (*(SpinLock)) \
        : "r" (tmp) \
        : "memory"); \
} while(0)

#define Arm64ReleaseSpinLock(SpinLock) do { \
    __asm__ volatile( \
        "dmb    sy\n" \
        "str    wzr, [%0]\n" \
        : \
        : "r" (SpinLock) \
        : "memory"); \
} while(0)

/* ARM64 atomic operations using LDXR/STXR for kernel operations */

/* ARM64-specific atomic compare and swap */
FORCEINLINE
LONG
Arm64InterlockedCompareExchange(
    IN OUT volatile LONG* Destination,
    IN LONG Exchange,
    IN LONG Comparand)
{
    LONG Result, tmp;
    __asm__ volatile(
        "1: ldxr    %w0, [%3]\n"
        "   cmp     %w0, %w4\n"
        "   b.ne    2f\n"
        "   stxr    %w1, %w2, [%3]\n"
        "   cbnz    %w1, 1b\n"
        "   dmb     sy\n"
        "2:\n"
        : "=&r" (Result), "=&r" (tmp)
        : "r" (Exchange), "r" (Destination), "r" (Comparand)
        : "memory", "cc");
    return Result;
}

/* ARM64-specific atomic exchange */
FORCEINLINE
LONG
Arm64InterlockedExchange(
    IN OUT volatile LONG* Target,
    IN LONG Value)
{
    LONG Result, tmp;
    __asm__ volatile(
        "1: ldxr    %w0, [%3]\n"
        "   stxr    %w1, %w2, [%3]\n"
        "   cbnz    %w1, 1b\n"
        "   dmb     sy\n"
        : "=&r" (Result), "=&r" (tmp)
        : "r" (Value), "r" (Target)
        : "memory");
    return Result;
}

/* ARM64-specific atomic increment */
FORCEINLINE
LONG
Arm64InterlockedIncrement(
    IN OUT volatile LONG* Addend)
{
    LONG Result, tmp, tmp2;
    __asm__ volatile(
        "1: ldxr    %w0, [%3]\n"
        "   add     %w1, %w0, #1\n"
        "   stxr    %w2, %w1, [%3]\n"
        "   cbnz    %w2, 1b\n"
        "   dmb     sy\n"
        "   mov     %w0, %w1\n"
        : "=&r" (Result), "=&r" (tmp), "=&r" (tmp2)
        : "r" (Addend)
        : "memory");
    return Result;
}

/* ARM64-specific atomic decrement */
FORCEINLINE
LONG
Arm64InterlockedDecrement(
    IN OUT volatile LONG* Addend)
{
    LONG Result, tmp, tmp2;
    __asm__ volatile(
        "1: ldxr    %w0, [%3]\n"
        "   sub     %w1, %w0, #1\n"
        "   stxr    %w2, %w1, [%3]\n"
        "   cbnz    %w2, 1b\n"
        "   dmb     sy\n"
        "   mov     %w0, %w1\n"
        : "=&r" (Result), "=&r" (tmp), "=&r" (tmp2)
        : "r" (Addend)
        : "memory");
    return Result;
}

/* ARM64-specific atomic OR operation */
FORCEINLINE
CHAR
Arm64InterlockedOr8(
    IN OUT volatile CHAR* Destination,
    IN CHAR Value)
{
    CHAR Result, tmp, tmp2;
    __asm__ volatile(
        "1: ldxrb   %w0, [%3]\n"
        "   orr     %w1, %w0, %w4\n"
        "   stxrb   %w2, %w1, [%3]\n"
        "   cbnz    %w2, 1b\n"
        "   dmb     sy\n"
        : "=&r" (Result), "=&r" (tmp), "=&r" (tmp2)
        : "r" (Destination), "r" ((ULONG)Value)
        : "memory");
    return Result;
}

/* ARM64-specific atomic AND operation */
FORCEINLINE
CHAR
Arm64InterlockedAnd8(
    IN OUT volatile CHAR* Destination,
    IN CHAR Value)
{
    CHAR Result, tmp, tmp2;
    __asm__ volatile(
        "1: ldxrb   %w0, [%3]\n"
        "   and     %w1, %w0, %w4\n"
        "   stxrb   %w2, %w1, [%3]\n"
        "   cbnz    %w2, 1b\n"
        "   dmb     sy\n"
        : "=&r" (Result), "=&r" (tmp), "=&r" (tmp2)
        : "r" (Destination), "r" ((ULONG)Value)
        : "memory");
    return Result;
}

/* InterlockedXX functions are already defined in wdm.h, but we provide ARM64 optimized versions */

/* ARM64 CPU yield for spin loops */
#define YieldProcessor() __yield()

/* ARM64 Generic Timer definitions */
#define ARM64_TIMER_FREQ_DEFAULT    62500000ULL  /* 62.5 MHz typical */

/* ARM64 Thread Scheduler Constants */
#define THREAD_QUANTUM_MAX          12          /* Maximum quantum */
#define ARM64_QUANTUM_TARGET_MS     30          /* Target quantum duration in milliseconds */

/* ARM64 Static definition (compatibility) */
#ifndef STATIC
#define STATIC static
#endif

/* win64 uses DMA macros, this one is not defined (following AMD64 pattern) */
NTHALAPI
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject,
    IN PWAIT_CONTEXT_BLOCK Wcb,
    IN ULONG NumberOfMapRegisters,
    IN PDRIVER_CONTROL ExecutionRoutine);

/* ARM64 specific debugging */
#define ARM64_BRK()                 __asm__ volatile("brk #0")
#define ARM64_WFI()                 __asm__ volatile("wfi")
#define ARM64_WFE()                 __asm__ volatile("wfe")
#define ARM64_SEV()                 __asm__ volatile("sev")
#define ARM64_SEVL()                __asm__ volatile("sevl")

/* Missing function declarations for ARM64 */
#ifndef _NTDDK_
FORCEINLINE
VOID
KeQueryTickCount(OUT PLARGE_INTEGER TickCount)
{
    *TickCount = *(PLARGE_INTEGER)&KeTickCount;
}

FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentPrcb()->Number;
}
#endif

FORCEINLINE
KIRQL
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

FORCEINLINE
KIRQL
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

FORCEINLINE
ULONG
KeGetContextSwitches(
    IN PKPRCB Prcb)
{
    return Prcb->KeContextSwitches;
}

/* KPROCESSOR_STATE structure accessors for ARM64 */
/* ARM64 processor state functions - use generic implementations */

/* Missing constants for ARM64 - KPCR field offsets */
#define KPCR_SELF_PCR_OFFSET        0x018   /* Offset of Self field in KPCR */
#define KPCR_CURRENT_PRCB_OFFSET    0x008   /* Offset of CurrentPrcb (not used on ARM64) */
#define KPCR_CONTAINED_PRCB_OFFSET  0x180   /* Offset of PRCB in KPCR structure */
#define KPCR_INITIAL_STACK_OFFSET   0x028   /* Initial stack offset */
#define KPCR_STACK_LIMIT_OFFSET     0x030   /* Stack limit offset */
#define KPRCB_PCR_PAGE_OFFSET       0x000   /* PCR page offset in PRCB */

/* ARM64 doesn't have V86 mode, so define EFLAGS_V86_MASK as 0 */
#define EFLAGS_V86_MASK 0

/* ARM64 MM helpers - temporary stubs */
#define MiPdeToPte(PDE) ((PMMPTE)(((ULONG_PTR)(PDE) & ~0xFFF) + 0x8000))  /* TODO: Implement proper ARM64 page table mapping */

/* ARM64 specific kernel functions */
FORCEINLINE
VOID
KiRundownThread(
    IN PKTHREAD Thread)
{
    /* TODO: Implement ARM64 thread rundown */
}

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Return the linked trap frame */
    return (PKTRAP_FRAME)TrapFrame->TrapFrame;
}

FORCEINLINE
VOID
DECLSPEC_NORETURN
KiExceptionExit(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    /* TODO: Implement proper ARM64 exception exit */
    /* This function should restore context and return to the trap point */
    /* For now, enter infinite loop as we can't properly return */
    for(;;) {
        __asm__ volatile("wfi");
    }
}

FORCEINLINE
BOOLEAN
KiUserTrap(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Check if the trap came from user mode */
    return (TrapFrame->PreviousMode == UserMode);
}

/* ARM64 cache management */
FORCEINLINE
VOID
KeSweepICache(
    IN PVOID BaseAddress,
    IN SIZE_T FlushSize)
{
    /* ARM64 instruction cache invalidation */
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/* ARM64 trap frame state */
FORCEINLINE
BOOLEAN
KeGetTrapFrameInterruptState(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Check if interrupts were enabled in the trap frame */
    return (TrapFrame->Pstate & 0x80) == 0;
}

/* ARM64 frame register access functions */
FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(
    IN PCONTEXT Context)
{
    /* Return frame pointer (X29/Fp) from context */
    return Context->Fp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(
    IN PCONTEXT Context,
    IN ULONG_PTR Frame)
{
    /* Set frame pointer (X29/Fp) in context */
    Context->Fp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Return stack pointer from trap frame */
    return TrapFrame->Sp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Return frame pointer from trap frame */
    return TrapFrame->Fp;
}

/* ARM64 interrupt management functions */
FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG64 daif;
    __asm__ volatile("mrs %0, daif" : "=r" (daif));
    __asm__ volatile("msr daifset, #2");  /* Disable IRQ */
    return (daif & 0x80) == 0;  /* Return TRUE if interrupts were enabled */
}

FORCEINLINE
VOID
KeRestoreInterrupts(
    IN BOOLEAN Enable)
{
    if (Enable)
    {
        __asm__ volatile("msr daifclr, #2");  /* Enable IRQ */
    }
    else
    {
        __asm__ volatile("msr daifset, #2");  /* Disable IRQ */
    }
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* _NTOSKRNL_INCLUDE_INTERNAL_ARM64_KE_H */
