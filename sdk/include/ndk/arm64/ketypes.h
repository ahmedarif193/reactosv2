

#ifndef _ARM64_KETYPES_H
#define _ARM64_KETYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt request levels */
#define PASSIVE_LEVEL           0
#define LOW_LEVEL               0
#define APC_LEVEL               1
#define DISPATCH_LEVEL          2
#define CMCI_LEVEL              5
#define CLOCK_LEVEL             13
#define IPI_LEVEL               14
#define DRS_LEVEL               14
#define POWER_LEVEL             14
#define PROFILE_LEVEL           15
#define HIGH_LEVEL              15

//
// Synchronization-level IRQL
//
#ifndef CONFIG_SMP
#define SYNCH_LEVEL             DISPATCH_LEVEL
#else
#define SYNCH_LEVEL             (IPI_LEVEL - 2)
#endif

//
// IPI Types
//
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        6
#define IPI_SYNCH_REQUEST       16

//
// PRCB Flags
//
#define PRCB_MAJOR_VERSION      1
#define PRCB_MINOR_VERSION      1
#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

//
// No LDTs on ARM64
//
#define LDT_ENTRY              ULONG


//
// HAL Variables
//
#define INITIAL_STALL_COUNT     100
#define MM_HAL_VA_START         0xFFFFFFFFFFC00000ULL
#define MM_HAL_VA_END           0xFFFFFFFFFFFFFFFFULL

//
// ARM64 specific constants
//
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define KI_USER_SHARED_DATA     0xFFFFF78000000000ULL

//
// Double fault stack size
//
#define DOUBLE_FAULT_STACK_SIZE 0x8000

//
// Structure for CPUID info
//
typedef union _CPU_INFO
{
    ULONG dummy;
} CPU_INFO, *PCPU_INFO;

typedef struct _KTRAP_FRAME
{
    UCHAR ExceptionActive;
    UCHAR ContextFromKFramesUnwound;
    UCHAR DebugRegistersValid;
    union
    {
        struct
        {
            CHAR PreviousMode;
            UCHAR PreviousIrql;
        };
    };
    ULONG Reserved;
    union
    {
        struct
        {
            ULONG64 FaultAddress;
            ULONG64 TrapFrame;
        };
    };
    //struct PKARM64_VFP_STATE VfpState;
    ULONG VfpState;
    ULONG Bcr[8];
    ULONG64 Bvr[8];
    ULONG Wcr[2];
    ULONG64 Wvr[2];
    ULONG Spsr;
    ULONG Pstate;  // Also known as CPSR in user-mode context
    ULONG Esr;
    ULONG64 Sp;
    union
    {
        ULONG64 X[29];
        struct
        {
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
        };
    };
    ULONG64 Lr;
    ULONG64 Fp;
    ULONG64 Pc;
} KTRAP_FRAME, *PKTRAP_FRAME;

typedef struct _KEXCEPTION_FRAME
{
    ULONG dummy;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

/* For ARM64, the callout frame is the same as the exception frame */
typedef KEXCEPTION_FRAME KCALLOUT_FRAME, *PKCALLOUT_FRAME;

#ifndef NTOS_MODE_USER

typedef struct _TRAPFRAME_LOG_ENTRY
{
    ULONG64 Thread;
    UCHAR CpuNumber;
    UCHAR TrapType;
    USHORT Padding;
    ULONG Cpsrl;
    ULONG64 X0;
    ULONG64 X1;
    ULONG64 X2;
    ULONG64 X3;
    ULONG64 X4;
    ULONG64 X5;
    ULONG64 X6;
    ULONG64 X7;
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 Sp;
    ULONG64 Pc;
    ULONG64 Far;
    ULONG Esr;
    ULONG Reserved1;
} TRAPFRAME_LOG_ENTRY, *PTRAPFRAME_LOG_ENTRY;

//
// Forward declarations
//
typedef struct _KTHREAD KTHREAD, *PKTHREAD;
typedef struct _KPRCB KPRCB, *PKPRCB;

//
// Special Registers Structure (outside of CONTEXT)
// Based on WoA symbols
//
typedef struct _KSPECIAL_REGISTERS
{
    ULONG64 Elr_El1;
    UINT32  Spsr_El1;
    ULONG64 Tpidr_El0;
    ULONG64 Tpidrro_El0;
    ULONG64 Tpidr_El1;
    ULONG64 KernelBvr[8];
    ULONG   KernelBcr[8];
    ULONG64 KernelWvr[2];
    ULONG   KernelWcr[2];
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

//
// ARM64 Architecture State
// Based on WoA symbols
//
typedef struct _KARM64_ARCH_STATE
{
    ULONG64 Midr_El1;
    ULONG64 Sctlr_El1;
    ULONG64 Actlr_El1;
    ULONG64 Cpacr_El1;
    ULONG64 Tcr_El1;
    ULONG64 Ttbr0_El1;
    ULONG64 Ttbr1_El1;
    ULONG64 Esr_El1;
    ULONG64 Far_El1;
    ULONG64 Pmcr_El0;
    ULONG64 Pmcntenset_El0;
    ULONG64 Pmccntr_El0;
    ULONG64 Pmxevcntr_El0[31];
    ULONG64 Pmxevtyper_El0[31];
    ULONG64 Pmovsclr_El0;
    ULONG64 Pmselr_El0;
    ULONG64 Pmuserenr_El0;
    ULONG64 Mair_El1;
    ULONG64 Vbar_El1;
} KARM64_ARCH_STATE, *PKARM64_ARCH_STATE;

typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters; // 0
    KARM64_ARCH_STATE ArchState;         // 160
    CONTEXT ContextFrame;                // 800
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;

#define NUMBER_POOL_LOOKASIDE_LISTS 32

typedef struct _KPRCB
{
    /* Minimal bring-up subset used by scheduler helpers */
    UCHAR Number;                      /* Processor index */
    UCHAR Reserved0[1];
    USHORT MajorVersion;               /* Kernel major version */
    USHORT MinorVersion;               /* Kernel minor version */
    PKTHREAD CurrentThread;            /* Currently running thread */
    PKTHREAD NextThread;               /* Next thread to run */
    PKTHREAD IdleThread;               /* Idle thread for this processor */
    ULONG64 PrcbLock;                  /* Processor control block lock */
    ULONG ReadySummary;                /* Bitmap of ready priorities */
    ULONG QueueIndex;                  /* Queue index for load balancing */
    LIST_ENTRY DispatcherReadyListHead[32]; /* Ready queues by priority */
    SINGLE_LIST_ENTRY DeferredReadyListHead; /* Deferred ready list */
    LIST_ENTRY WaitListHead;           /* Wait list head */
    KSPIN_LOCK_QUEUE LockQueue[LockQueueMaximumLock];
    PP_LOOKASIDE_LIST PPLookasideList[16];
    KDPC_DATA DpcData[2];               /* DPC data structures */
    PVOID DpcStack;                    /* DPC stack pointer */
    LONG MaximumDpcQueueDepth;         /* Maximum DPC queue depth */
    ULONG DpcRequestRate;              /* DPC request rate */
    ULONG MinimumDpcRate;              /* Minimum DPC rate */
    ULONG AdjustDpcThreshold;          /* DPC adjustment threshold */
    UCHAR DpcRoutineActive;
    volatile UCHAR DpcThreadActive;
    ULONG TimerRequest;
    KDPC CallDpc;                      /* Call DPC structure */
    ULONG LookasideIrpFloat;           /* IRP lookaside list float count */
    ULONG KernelTime;                  /* Kernel time accumulator */
    ULONG UserTime;                    /* User time accumulator */
    LONG IoReadOperationCount;         /* IO read operations */
    LONG IoWriteOperationCount;        /* IO write operations */
    LONG IoOtherOperationCount;        /* IO other operations */
    LARGE_INTEGER IoReadTransferCount;  /* IO read transfer count */
    LARGE_INTEGER IoWriteTransferCount; /* IO write transfer count */
    LARGE_INTEGER IoOtherTransferCount; /* IO other transfer count */
    UCHAR DebuggerSavedIRQL;            /* Saved IRQL for debugger */
    UCHAR Pad1[7];                      /* Padding for alignment */
    ULONG KeFirstLevelTbFills;          /* First level TLB fills */
    ULONG KeSecondLevelTbFills;         /* Second level TLB fills */
    ULONG KeSystemCalls;                /* System call count */
    ULONG KeContextSwitches;            /* Context switch count */
    ULONG64 SetMember;                  /* Processor set member */
    UCHAR IdleSchedule;                 /* Idle schedule flag */
    UCHAR SkipTick;                     /* Skip tick flag */
    UCHAR Pad2[6];                      /* Padding */
    ULONG InterruptCount;               /* Interrupt count */
    ULONG InterruptTime;                /* Interrupt time */
    ULONG DpcTime;                      /* DPC time */
    ULONG DebugDpcTime;                 /* Debug DPC time */
    ULONG DpcLastCount;                 /* Last DPC count */
    ULONG64 TimerHand;                  /* Timer hand */
    UCHAR QuantumEnd;                   /* Quantum end flag */
    UCHAR DpcInterruptRequested;        /* DPC interrupt requested */
    UCHAR DpcThreadRequested;           /* DPC thread requested */
    UCHAR ThreadDpcEnable;              /* Thread DPC enable */
    LONG DpcSetEventRequest;            /* DPC set event request */
    LONG Sleeping;                      /* Sleeping flag */
    KEVENT DpcEvent;                    /* DPC event */
    CHAR CpuType;                       /* CPU type identifier */
    CHAR CpuID;                         /* CPU ID */
    UCHAR VendorString[13];             /* CPU vendor string */
    UCHAR Pad3[1];                      /* Padding */
    ULONG KeAlignmentFixupCount;        /* Alignment fixup count */
    ULONG KeExceptionDispatchCount;     /* Exception dispatch count */
    ULONG KeFloatingEmulationCount;     /* Floating emulation count */
    PKPRCB MultiThreadSetMaster;        /* Multi-thread set master */
    ULONG64 MultiThreadProcessorSet;    /* Multi-thread processor set */
    ULONG MHz;                           /* CPU speed in MHz */
    ULONG FeatureBits;                   /* CPU feature bits */
    ULONG BuildType;                     /* Build type flags */
    ULONG CacheLineSize;                 /* Cache line size */
    LARGE_INTEGER UpdateSignature;       /* Update signature */
    USHORT CpuStep;                      /* CPU stepping */
    UCHAR Pad4[2];                       /* Padding for alignment */
    LONG MmPageFaultCount;               /* Page fault count */
    LONG MmDemandZeroCount;              /* Demand zero count */
    LONG MmTransitionCount;              /* Transition count */
    PROCESSOR_POWER_STATE PowerState;   /* Power state */
    PP_LOOKASIDE_LIST PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    PP_LOOKASIDE_LIST PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    KPROCESSOR_STATE ProcessorState;    /* Processor state for debugging */
    struct _KNODE* ParentNode;          /* Parent NUMA node */
    volatile struct _KPRCB *SignalDone; /* IPI signal done pointer */
    volatile ULONG TargetSet;           /* IPI target set */
    volatile PVOID CurrentPacket[3];    /* IPI current packet array */
    volatile PVOID WorkerRoutine;       /* IPI worker routine */
    volatile ULONG IpiFrozen;           /* IPI frozen state */
    KIRQL ThreadLockIrql;               /* Thread lock saved IRQL */
} KPRCB, *PKPRCB;

//
// Processor Control Region
// Based on WoA
//
typedef struct _KIPCR
{
    union
    {
        struct
        {
            ULONG TibPad0[2];
            PVOID Spare1;
            PVOID Self;
            PVOID  PcrReserved0;
            struct _KSPIN_LOCK_QUEUE* LockArray;
            PVOID Used_Self;
        };
    };
    KIRQL CurrentIrql;
    UCHAR SecondLevelCacheAssociativity;
    UCHAR Pad1[2];
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG StallScaleFactor;
    ULONG SecondLevelCacheSize;
    struct
    {
        UCHAR ApcInterrupt;
        UCHAR DispatchInterrupt;
    };
    USHORT InterruptPad;
    UCHAR BtiMitigation;
    struct
    {
        UCHAR SsbMitigationFirmware:1;
        UCHAR SsbMitigationDynamic:1;
        UCHAR SsbMitigationKernel:1;
        UCHAR SsbMitigationUser:1;
        UCHAR SsbMitigationReserved:4;
    };
    UCHAR Pad2[2];
    ULONG64 PanicStorage[6];
    PVOID KdVersionBlock;
    PVOID HalReserved[134];
    PVOID KvaUserModeTtbr1;

    /* Private members, not in ntddk.h */
    PVOID Idt[256];
    PVOID* IdtExt;
    PVOID PcrAlign[15];
    KPRCB Prcb;
} KIPCR, *PKIPCR;

//
// PCR/PRCB accessors (bring-up shim)
//
/* PCR access for ARM64 */
#ifdef _NTOSKRNL_
/* Internal to kernel - direct access */
extern struct _KIPCR KiInitialPcr;
FORCEINLINE
struct _KIPCR * KeGetPcr(VOID)
{
    return &KiInitialPcr;
}
FORCEINLINE
struct _KPRCB * KeGetCurrentPrcb(VOID)
{
    return &KiInitialPcr.Prcb;
}
#else
/* External drivers - use proper accessor functions */
NTSYSAPI
struct _KIPCR *
NTAPI
KeGetPcr(VOID);

NTSYSAPI
struct _KPRCB *
NTAPI
KeGetCurrentPrcb(VOID);
#endif

//
// IRQL access - for external drivers use the HAL function
//
#ifdef _NTOSKRNL_
#define KeGetCurrentIrql()             KeGetPcr()->CurrentIrql
#else
/* External drivers must use the exported function */
NTSYSAPI
KIRQL
NTAPI
KeGetCurrentIrql(VOID);
#endif
#define _KeGetCurrentThread()          KeGetCurrentPrcb()->CurrentThread
#define _KeGetPreviousMode()           KeGetCurrentPrcb()->CurrentThread->PreviousMode
#define _KeIsExecutingDpc()            (KeGetCurrentPrcb()->DpcRoutineActive != 0)
#ifndef KeGetCurrentThread
#define KeGetCurrentThread()           _KeGetCurrentThread()
#endif
#ifndef KeGetPreviousMode
#define KeGetPreviousMode()            _KeGetPreviousMode()
#endif

#endif // !NTOS_MODE_USER

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // !_ARM64_KETYPES_H
