$if (_WDMDDK_)
/** Kernel definitions for ARM64 **/

/* Interrupt request levels */
#define PASSIVE_LEVEL           0
#define LOW_LEVEL               0
#define APC_LEVEL               1
#define DISPATCH_LEVEL          2
#define CLOCK_LEVEL             13
#define IPI_LEVEL               14
#define DRS_LEVEL               14
#define POWER_LEVEL             14
#define PROFILE_LEVEL           15
#define HIGH_LEVEL              15

#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#ifndef PAGE_SIZE
#define PAGE_SIZE               0x1000
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT              12L
#endif

#define PAUSE_PROCESSOR YieldProcessor();

/* FIXME: Based on AMD64 but needed to compile apps */
#define KERNEL_STACK_SIZE                   12288
#define KERNEL_LARGE_STACK_SIZE             61440
#define KERNEL_LARGE_STACK_COMMIT KERNEL_STACK_SIZE
/* FIXME End */

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

NTSYSAPI
PKTHREAD
NTAPI
KeGetCurrentThread(VOID);

NTKERNELAPI
ULONG
NTAPI
KeGetCurrentProcessorIndex(VOID);

_IRQL_requires_max_(HIGH_LEVEL)
_IRQL_saves_
NTHALAPI
KIRQL
NTAPI
KeGetCurrentIrql(VOID);

_IRQL_requires_max_(HIGH_LEVEL)
_IRQL_saves_
NTHALAPI
KIRQL
NTAPI
KfRaiseIrql(
    _In_ KIRQL NewIrql);

_IRQL_requires_max_(HIGH_LEVEL)
NTHALAPI
VOID
NTAPI
KfLowerIrql(
    _In_ KIRQL NewIrql);

/* ARM64 debug break intrinsic */
#define DbgRaiseAssertionFailure __debugbreak

/* Memory barrier for ARM64 */
#define KeMemoryBarrier() __asm__ volatile("dmb sy" ::: "memory")

/* PCR version constants for ARM64 */
#define PCR_MINOR_VERSION 1
#define PCR_MAJOR_VERSION 1

/* Match other arches: map Ke{Lower,Raise}Irql to Kf* */
#define KeLowerIrql(a) KfLowerIrql(a)
#define KeRaiseIrql(a,b) *(b) = KfRaiseIrql(a)

/* VOID
 * KeFlushIoBuffers(
 *   IN PMDL Mdl,
 *   IN BOOLEAN ReadOperation,
 *   IN BOOLEAN DmaOperation)
 */
#define KeFlushIoBuffers(_Mdl, _ReadOperation, _DmaOperation)

#define KeQueryTickCount(CurrentCount) _KeQueryTickCount(CurrentCount)

FORCEINLINE
VOID
_KeQueryTickCount(
    OUT PLARGE_INTEGER CurrentCount)
{
    /* ARM64 implementation - read from shared user data */
    CurrentCount->QuadPart = *(volatile LONGLONG*)0xFFFFF78000000320ULL;
}

typedef union NEON128 {
    struct {
        ULONGLONG Low;
        LONGLONG High;
    } DUMMYSTRUCTNAME;
    double D[2];
    float  S[4];
    USHORT H[8];
    UCHAR  B[16];
} NEON128, *PNEON128;

typedef struct _KFLOATING_SAVE
{
    ULONG Fpcr;             /* Floating-point Control Register */
    ULONG Fpsr;             /* Floating-point Status Register */
    NEON128 V[32];          /* NEON/FPU registers */
} KFLOATING_SAVE, *PKFLOATING_SAVE;

$endif (_WDMDDK_)
$if (_NTDDK_)

#if (NTDDI_VERSION >= NTDDI_WIN7)
_CRT_DEPRECATE_TEXT("KeGetCurrentProcessorNumber is deprecated. Use KeGetCurrentProcessorNumberEx or KeGetCurrentProcessorIndex instead.")
#endif
FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    /* On ARM64, the current processor number is stored in TPIDR_EL1 */
    ULONG processorNumber;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(processorNumber));
    /* Extract processor number from PCR (offset 0x8) */
    return *(ULONG*)((ULONG_PTR)processorNumber + 0x8);
}

#define ARM64_MAX_BREAKPOINTS 8
#define ARM64_MAX_WATCHPOINTS 2


typedef struct _CONTEXT {

    //
    // Control flags.
    //

    ULONG ContextFlags;

    //
    // Integer registers
    //

    ULONG Pstate;
    union {
        struct {
            ULONG64 X0;
            ULONG64 X1;
            ULONG64 X2;
            ULONG64 X3;
            ULONG64 X4;
            ULONG64 X5;
            ULONG64 X6;
            ULONG64 X7;
            ULONG64 X8;
            ULONG64 X9;
            ULONG64 X10;
            ULONG64 X11;
            ULONG64 X12;
            ULONG64 X13;
            ULONG64 X14;
            ULONG64 X15;
            ULONG64 X16;
            ULONG64 X17;
            ULONG64 X18;
            ULONG64 X19;
            ULONG64 X20;
            ULONG64 X21;
            ULONG64 X22;
            ULONG64 X23;
            ULONG64 X24;
            ULONG64 X25;
            ULONG64 X26;
            ULONG64 X27;
            ULONG64 X28;
    		ULONG64 Fp;
            ULONG64 Lr;
        } DUMMYSTRUCTNAME;
        ULONG64 X[31];
    } DUMMYUNIONNAME;

    ULONG64 Sp;
    ULONG64 Pc;

    //
    // Floating Point/NEON Registers
    //

    NEON128 V[32];
    ULONG Fpcr;
    ULONG Fpsr;

    //
    // Debug registers
    //

    ULONG Bcr[ARM64_MAX_BREAKPOINTS];
    ULONG64 Bvr[ARM64_MAX_BREAKPOINTS];
    ULONG Wcr[ARM64_MAX_WATCHPOINTS];
    ULONG64 Wvr[ARM64_MAX_WATCHPOINTS];

} CONTEXT, *PCONTEXT;

/* ARM64 floating point save area */

/* ARM64 floating point functions */
NTSTATUS
NTAPI
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave);

NTSTATUS
NTAPI
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave);

$endif
