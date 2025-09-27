/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Initialization
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64 Architecture definitions */
#include <internal/arm64/mm.h>

/* ARM64 Specific Constants */
#define PCR_MAJOR_VERSION       1
#define PCR_MINOR_VERSION       1

/* ARM64 Register Default Values - following Windows patterns */
#define ARM64_DEFAULT_TIMER_FREQ    62500000ULL
#define ARM64_CSSELR_L1_DCACHE      0ULL

/* ARM64 MPIDR_EL1 parsing */
#define ARM64_MPIDR_AFF0_MASK       0x000000FFULL
#define ARM64_MPIDR_AFF1_MASK       0x0000FF00ULL
#define ARM64_MPIDR_AFF2_MASK       0x00FF0000ULL
#define ARM64_MPIDR_AFF3_MASK       0xFF00000000ULL
#define ARM64_MPIDR_MT              (1ULL << 24)
#define ARM64_MPIDR_UP              (1ULL << 30)

/* ARM64 CPU Feature Detection Masks */
#define ARM64_ISAR0_AES_MASK        0xF0ULL
#define ARM64_ISAR0_SHA_MASK        0xF00ULL
#define ARM64_ISAR0_CRC32_MASK      0xF0000ULL
#define ARM64_ISAR0_ATOMIC_MASK     0xF00000ULL
#define ARM64_PFR0_FP_MASK          0xFULL
#define ARM64_PFR0_ASIMD_MASK       0xF0ULL

/* Exception Level constants */
#define ARM64_CURRENTEL_MASK        0xCULL
#define ARM64_CURRENTEL_SHIFT       2

/* GLOBALS *******************************************************************/

/* ARM64 processor information */
ULONG KiProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
ULONG KiProcessorLevel = 0;
ULONG KiProcessorRevision = 0;

/* ARM64 CPU features */
ULONG64 KiArm64Features = 0;

/* ARM64 Cache information - initialized dynamically from CTR_EL0 */
ULONG KiDcacheLineSize = 0;
ULONG KiIcacheLineSize = 0;

/* ARM64 Timer frequency */
ULONG64 KiTimerFrequency = 0;

/* Process counter for performance monitoring */
ULONG ProcessCount;

/* PCR and PRCB - exported for kernel drivers */
#if defined(__clang__)
KIPCR KiInitialPcr;
#else
__declspec(dllexport) KIPCR KiInitialPcr;
#endif
KPRCB KiInitialPrcb;

/* FUNCTIONS *****************************************************************/

/**
 * @brief Initialize ARM64 processor features and capabilities
 */
VOID
NTAPI
KiInitializeArm64Features(VOID)
{
    ULONGLONG midr, idr0, pfr0;
    
    /* Read processor identification registers with proper barriers */
    ARM64_ISB();  /* Ensure previous operations complete */
    midr = __readmidr();
    idr0 = ARM64_READ_SYSREG(id_aa64isar0_el1);
    pfr0 = ARM64_READ_SYSREG(id_aa64pfr0_el1);
    ARM64_ISB();  /* Synchronize register reads */
 
    /* Extract processor information */
    KiProcessorLevel = (ULONG)((midr >> 4) & 0xFFF);
    KiProcessorRevision = (ULONG)(midr & 0xF);
    KiProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
    KeProcessorLevel = (USHORT)KiProcessorLevel;
    KeProcessorRevision = (USHORT)KiProcessorRevision;
    
    /* Detect CPU features */
    KiArm64Features = 0;
    
    /* Check for AES support - ID_AA64ISAR0_EL1.AES[7:4] */
    if ((idr0 & ARM64_ISAR0_AES_MASK) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_AES;
        DPRINT("ARM64: AES encryption support detected\n");
    }

    /* Check for SHA support - ID_AA64ISAR0_EL1.SHA1[11:8] */
    if ((idr0 & ARM64_ISAR0_SHA_MASK) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_SHA;
        DPRINT("ARM64: SHA hash support detected\n");
    }
    
    /* Check for floating point support - ID_AA64PFR0_EL1.FP[3:0] */
    if ((pfr0 & ARM64_PFR0_FP_MASK) != 0xF)
    {
        KiArm64Features |= ARM64_FEATURE_FP;
        DPRINT("ARM64: Floating point support detected\n");
    }

    /* Check for Advanced SIMD support - ID_AA64PFR0_EL1.AdvSIMD[7:4] */
    if ((pfr0 & ARM64_PFR0_ASIMD_MASK) != 0xF0)
    {
        KiArm64Features |= ARM64_FEATURE_ASIMD;
        DPRINT("ARM64: Advanced SIMD support detected\n");
    }
    
    /* Check for CRC32 support - ID_AA64ISAR0_EL1.CRC32[19:16] */
    if ((idr0 & ARM64_ISAR0_CRC32_MASK) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_CRC32;
        DPRINT("ARM64: CRC32 support detected\n");
    }

    /* Check for Atomic instructions support - ID_AA64ISAR0_EL1.Atomic[23:20] */
    if ((idr0 & ARM64_ISAR0_ATOMIC_MASK) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_ATOMIC;
        DPRINT("ARM64: LSE atomic instructions support detected\n");
    }
    
    KeFeatureBits = KiArm64Features;

    DPRINT("ARM64: Features=0x%llx, Revision=%u\n", KiArm64Features, KiProcessorRevision);
}

/**
 * @brief Initialize ARM64 cache information
 */
VOID
NTAPI
KiInitializeArm64Cache(VOID)
{
    ULONGLONG ctr, ccsidr;

    /* Read cache type register - accessible from all exception levels */
    ARM64_ISB();  /* Synchronize before reading CTR_EL0 */
    ctr = ARM64_READ_SYSREG(ctr_el0);
    
    /* Extract cache line sizes dynamically from CTR_EL0 */
    KiDcacheLineSize = 4 << ((ctr & 0xF0000) >> 16);  /* DminLine field */
    KiIcacheLineSize = 4 << (ctr & 0xF);  /* IminLine field */
    
    /* Select L1 data cache for CCSIDR_EL1 reading */
    ARM64_WRITE_SYSREG(csselr_el1, ARM64_CSSELR_L1_DCACHE);
    ARM64_ISB();  /* Synchronize CSSELR_EL1 write before reading CCSIDR_EL1 */

    ccsidr = ARM64_READ_SYSREG(ccsidr_el1);
    ARM64_ISB();  /* Synchronize CCSIDR_EL1 read */
    
    DPRINT("ARM64: DCache line=%u bytes, ICache line=%u bytes\n", 
           KiDcacheLineSize, KiIcacheLineSize);
    DPRINT("ARM64: L1 DCache CCSIDR=0x%llx\n", ccsidr);
}

/**
 * @brief Initialize ARM64 Generic Timer
 */
VOID
NTAPI
KiInitializeArm64Timer(VOID)
{
    /* Read timer frequency - CNTFRQ_EL0 set by firmware/EL2/EL3 */
    ARM64_ISB();  /* Synchronize before reading timer frequency */
    KiTimerFrequency = __readcntfrq();

    if (KiTimerFrequency == 0)
    {
        /* Use default frequency if not set by firmware */
        KiTimerFrequency = ARM64_DEFAULT_TIMER_FREQ;
        DPRINT1("ARM64: WARNING - Timer frequency not set by firmware, using default %llu Hz\n", KiTimerFrequency);
    }
    else
    {
        DPRINT("ARM64: Timer frequency %llu Hz\n", KiTimerFrequency);
    }
    
    /* Disable timer interrupt initially - clear all control bits */
    __writecntp_ctl_el0(0);
    ARM64_ISB();  /* Synchronize timer control register write */
}

/**
 * @brief Initialize ARM64 Memory Management Unit
 */
VOID
NTAPI
KiInitializeArm64Mmu(VOID)
{
    ULONGLONG tcr, mair, sctlr;

    /* Configure Memory Attribute Indirection Register first */
    mair = ARM64_MAIR_VALUE;
    __writemair_el1(mair);
    ARM64_ISB();  /* Synchronize MAIR_EL1 write */

    /* Configure Translation Control Register */
    tcr = ARM64_TCR_DEFAULT;
    __writetcr_el1(tcr);
    ARM64_ISB();  /* Synchronize TCR_EL1 write */

    /* Ensure all translation table setup is visible */
    ARM64_DSB_SY();  /* Data synchronization barrier */
    
    /* Read current SCTLR */
    sctlr = __readsctlr_el1();
    
    /* Enable MMU if not already enabled - critical for Windows kernel */
    if (!(sctlr & ARM64_SCTLR_M))
    {
        DPRINT("ARM64: Enabling MMU with Windows-compatible settings\n");
        sctlr |= ARM64_SCTLR_DEFAULT;

        /* Critical synchronization sequence for MMU enable */
        ARM64_DSB_SY();   /* Ensure all memory operations complete */
        __writesctlr_el1(sctlr);
        ARM64_ISB();      /* Critical: Synchronize SCTLR_EL1 write */

        /* Verify MMU is now enabled */
        sctlr = __readsctlr_el1();
        if (!(sctlr & ARM64_SCTLR_M))
        {
            DPRINT1("ARM64: FATAL - MMU enable failed\n");
            KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 0x1001, sctlr, ARM64_SCTLR_M, 0);
        }
    }
    else
    {
        DPRINT("ARM64: MMU already enabled by firmware\n");
        /* Update SCTLR with Windows-specific settings while keeping MMU enabled */
        sctlr |= (ARM64_SCTLR_DEFAULT & ~ARM64_SCTLR_M) | ARM64_SCTLR_M;
        ARM64_DSB_SY();
        __writesctlr_el1(sctlr);
        ARM64_ISB();
    }
    
    DPRINT("ARM64: TCR=0x%llx, MAIR=0x%llx, SCTLR=0x%llx\n", tcr, mair, sctlr);
}

/**
 * @brief Initialize ARM64 Exception Handling
 */
VOID
NTAPI
KiInitializeArm64Exceptions(VOID)
{
    extern VOID KiExceptionVectors(VOID);
    ULONGLONG vbar, current_vbar;

    /* Validate exception vector alignment (must be 2048-byte aligned) */
    vbar = (ULONGLONG)&KiExceptionVectors;
    if (vbar & 0x7FF)
    {
        DPRINT1("ARM64: FATAL - Exception vectors not properly aligned: 0x%llx\n", vbar);
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 0x1002, vbar, 0x800, 0);
    }

    /* Set Vector Base Address Register */
    __writevbar_el1(vbar);
    ARM64_ISB();  /* Synchronize VBAR_EL1 write */

    /* Verify VBAR_EL1 was set correctly */
    current_vbar = __readvbar_el1();
    if (current_vbar != vbar)
    {
        DPRINT1("ARM64: WARNING - VBAR_EL1 verification failed. Expected: 0x%llx, Got: 0x%llx\n", vbar, current_vbar);
    }
    
    DPRINT("ARM64: Exception vectors at 0x%llx\n", vbar);
}

/**
 * @brief Initialize ARM64 Processor Control Region (PCR)
 */
VOID
NTAPI
KiInitializeArm64Pcr(
    IN PKIPCR Pcr,
    IN ULONG ProcessorNumber,
    IN PKTHREAD IdleThread,
    IN PVOID IdleStack,
    IN PVOID DpcStack
)
{
    if (!Pcr)
        return;

    /* Set up self-referential members required by common macros */
    Pcr->Self = (PKPCR)(PVOID)Pcr;
    Pcr->Used_Self = (PVOID)Pcr;
    Pcr->PcrReserved0 = NULL;
    Pcr->LockArray = NULL;
    Pcr->CurrentIrql = PASSIVE_LEVEL;

    /* Set up basic PCR fields */
    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;

    /* Initialize PRCB header */
    Pcr->Prcb.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->Prcb.MinorVersion = PRCB_MINOR_VERSION;
    Pcr->Prcb.Number = (UCHAR)ProcessorNumber;
    Pcr->Prcb.SetMember = 1ULL << ProcessorNumber;
    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;
    Pcr->Prcb.CacheLineSize = (KiDcacheLineSize != 0) ? KiDcacheLineSize : 64;
    Pcr->Prcb.FeatureBits = (ULONG)KeFeatureBits;
    Pcr->Prcb.DpcStack = DpcStack;
    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.NextThread = NULL;
    Pcr->Prcb.IdleThread = IdleThread;
#ifndef CONFIG_SMP
    Pcr->Prcb.BuildType = PRCB_BUILD_UNIPROCESSOR;
#else
    Pcr->Prcb.BuildType = 0;
#endif
#if DBG
    Pcr->Prcb.BuildType |= PRCB_BUILD_DEBUG;
#endif

    /* Basic cache/STALL defaults */
    Pcr->SecondLevelCacheSize = 0;
    Pcr->StallScaleFactor = 50;

    /* Record PCR in processor block array */
    KiProcessorBlock[ProcessorNumber] = &Pcr->Prcb;

    /* Ensure idle thread has consistent stack pointers */
    if (IdleThread && IdleStack)
    {
        IdleThread->InitialStack = IdleStack;
        IdleThread->KernelStack = IdleStack;
    }

    /* Load PCR into TPIDR_EL1 for fast per-CPU access */
    ARM64_WRITE_SYSREG(tpidr_el1, (ULONG_PTR)Pcr);
    ARM64_ISB();

    DPRINT("ARM64: PCR initialized for processor %u, loaded into TPIDR_EL1\n", ProcessorNumber);
}

/**
 * @brief Early ARM64 processor initialization
 */
VOID
NTAPI
KiInitializeProcessor(VOID)
{
    ULONGLONG el, current_el_reg;

    /* Read and validate current Exception Level */
    ARM64_ISB();  /* Synchronize before reading CurrentEL */
    current_el_reg = __readcurrentel();
    el = (current_el_reg & ARM64_CURRENTEL_MASK) >> ARM64_CURRENTEL_SHIFT;
    DPRINT("ARM64: Running at Exception Level %llu\n", el);
    
    if (el != ARM64_EL1)
    {
        DPRINT1("ARM64: FATAL - Kernel must run at EL1! Current EL: %llu (CurrentEL: 0x%llx)\n", el, current_el_reg);
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, el, ARM64_EL1, current_el_reg, 0);
    }
    
    /* Initialize ARM64 features */
    KiInitializeArm64Features();
    
    /* Initialize cache information */
    KiInitializeArm64Cache();
    
    /* Initialize Generic Timer */
    KiInitializeArm64Timer();
    
    /* Initialize MMU */
    KiInitializeArm64Mmu();
    
    /* Initialize exception handling */
    KiInitializeArm64Exceptions();
    
    DPRINT("ARM64: Processor initialization completed\n");
}

/**
 * @brief Initialize ARM64 kernel
 */
NTSTATUS
NTAPI
KiInitializeKernel(
    IN PKPROCESS InitProcess,
    IN PKTHREAD InitThread,
    IN PVOID IdleStack,
    IN PKPRCB Prcb,
    IN CCHAR Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
)
{
    PKIPCR Pcr;
    KAFFINITY ProcessorMask;
    PVOID AlignedIdleStack;
    PVOID DpcStack;
    NTSTATUS Status;

    if (!InitProcess || !InitThread || !Prcb)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);
    ProcessorMask = ((KAFFINITY)1) << Number;
    AlignedIdleStack = (PVOID)ALIGN_DOWN_BY((ULONG_PTR)IdleStack, STACK_ALIGN);

    /* Initialize per-processor spin locks and queues */
    KiInitSpinLocks(Prcb, Number);

    /* Boot processor initialization */
    if (Number == 0)
    {
        ULONG_PTR PageDirectory[2] = {0, 0};

        SharedUserData->NXSupportPolicy = NX_SUPPORT_POLICY_ALWAYSON;

        KiInitSystem();

        InitializeListHead(&KiProcessListHead);

        KeInitializeProcess(InitProcess,
                            0,
                            MAXULONG_PTR,
                            PageDirectory,
                            FALSE);
        InitProcess->QuantumReset = MAXCHAR;
    }

    /* Build the idle thread */
    KeInitializeThread(InitProcess,
                       InitThread,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       AlignedIdleStack);

    InitThread->ApcState.Process = InitProcess;
    InitThread->NextProcessor = Number;
    InitThread->Priority = HIGH_PRIORITY;
    InitThread->State = Running;
    InitThread->Affinity = ProcessorMask;
    InitThread->WaitIrql = DISPATCH_LEVEL;
    InitProcess->ActiveProcessors |= ProcessorMask;
    ((PETHREAD)InitThread)->ThreadsProcess = (PEPROCESS)InitProcess;

    /* Initialize PRCB thread pointers */
    Prcb->CurrentThread = InitThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitThread;
    Prcb->SetMember = ProcessorMask;
    Prcb->MultiThreadProcessorSet = ProcessorMask;

    /* Ensure a DPC stack exists for this processor */
    if (Prcb->DpcStack == NULL)
    {
        DpcStack = MmCreateKernelStack(FALSE, 0);
        if (!DpcStack)
        {
            return STATUS_NO_MEMORY;
        }
        Prcb->DpcStack = DpcStack;
    }
    else
    {
        DpcStack = Prcb->DpcStack;
    }

    /* Initialize the PCR for this processor */
    KiInitializeArm64Pcr(Pcr, Number, InitThread, InitThread->KernelStack, DpcStack);

    /* Update PRCB scheduling parameters */
    Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
    Prcb->MinimumDpcRate = KiMinimumDpcRate;
    Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

    if (Number == 0)
    {
        Status = KiSchedulerStartup(LoaderBlock);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        ExpInitializeExecutive(Number, LoaderBlock);

        KiTimeIncrementReciprocal =
            KiComputeReciprocal(KeMaximumIncrement,
                                &KiTimeIncrementShiftCount);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief System startup routine called from boot code
 */
CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupBootStack(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
)
{
    PKTHREAD Thread;
    PKPROCESS Process;
    PKPRCB Prcb;
    PKIPCR Pcr;
    KAFFINITY ProcessorMask;
    CCHAR Cpu;
    ULONGLONG Mpidr;
    PVOID IdleStack;
    NTSTATUS Status;

    if (!LoaderBlock)
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    KeLoaderBlock = LoaderBlock;

    /* Determine the boot processor */
    Mpidr = __readmpidr();
    Cpu = (CCHAR)(Mpidr & ARM64_MPIDR_AFF0_MASK);

    if (Cpu != 0)
    {
        KeBugCheckEx(UNSUPPORTED_PROCESSOR, Cpu, 0, Mpidr, 0);
    }

    /* Ensure loader block fields are populated */
    if (LoaderBlock->Process == 0)
    {
        LoaderBlock->Process = (ULONG_PTR)&KiInitialProcess.Pcb;
    }

    if (LoaderBlock->Thread == 0)
    {
        LoaderBlock->Thread = (ULONG_PTR)&KiInitialThread.Tcb;
    }

    if (LoaderBlock->Prcb == 0)
    {
        LoaderBlock->Prcb = (ULONG_PTR)&KiInitialPrcb;
    }

    if (LoaderBlock->KernelStack == 0)
    {
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 0xA6400000, 0, 0, (ULONG_PTR)LoaderBlock);
    }

    Process = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;
    Thread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    Prcb = (PKPRCB)(ULONG_PTR)LoaderBlock->Prcb;
    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);
    IdleStack = (PVOID)ALIGN_DOWN_BY((ULONG_PTR)LoaderBlock->KernelStack, STACK_ALIGN);
    ProcessorMask = ((KAFFINITY)1) << Cpu;

    /* Clear PCR/PRCB structures and initialize basic relationships */
    RtlZeroMemory(Pcr, sizeof(*Pcr));

    if (KeNodeBlock[0] == NULL)
    {
        extern KNODE KiNode0;
        Prcb->ParentNode = &KiNode0;
    }
    else
    {
        Prcb->ParentNode = KeNodeBlock[0];
    }
    Prcb->ParentNode->ProcessorMask |= ProcessorMask;

    PoInitializePrcb(Prcb);

    KeNumberProcessors = Cpu + 1;
    KeActiveProcessors = ProcessorMask;

    KiProcessorBlock[Cpu] = Prcb;

    InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
    Thread->ApcState.Process = Process;

    /* Detect processor capabilities */
    KiInitializeProcessor();

    /* Initialize kernel debugger early */
    KdInitSystem(0, KeLoaderBlock);
    if (KdPollBreakIn())
    {
        DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
    }

    /* Initialize the HAL for this processor */
    HalInitializeProcessor(Cpu, LoaderBlock);

    /* Set processor affinity information */
    Prcb->SetMember = ProcessorMask;

    /* Prepare per-processor pool structures */
    ExInitPoolLookasidePointers();

    /* Raise IRQL to HIGH_LEVEL and then lower to APC_LEVEL for initialization */
    KfRaiseIrql(HIGH_LEVEL);
    KeLowerIrql(APC_LEVEL);

    Status = KiInitializeKernel(Process,
                                Thread,
                                IdleStack,
                                Prcb,
                                Cpu,
                                LoaderBlock);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED,
                     0xA6400001,
                     Status,
                     Cpu,
                     (ULONG_PTR)LoaderBlock);
    }

    /* Update loader block state */
    LoaderBlock->KernelStack = (ULONG_PTR)IdleStack;

    /* Finalize PRCB scheduler bookkeeping */
    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(Thread, 0);

    KiAcquirePrcbLock(Prcb);
    if (!Prcb->NextThread)
    {
        KiIdleSummary |= ProcessorMask;
    }
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    Thread->Priority = 0;

    ARM64_ENABLE_INTERRUPTS();
    KeLowerIrql(DISPATCH_LEVEL);
    Thread->WaitIrql = DISPATCH_LEVEL;

    KiIdleLoop();
}

/**
 * @brief Initialize machine-dependent kernel features for ARM64
 *
 * This function is called by KeInitSystem to initialize
 * architecture-specific features after the portable kernel
 * initialization is complete.
 */
VOID
NTAPI
KiInitMachineDependent(VOID)
{
    /* Initialize ARM64 specific features */
    DPRINT("ARM64: Initializing machine-dependent features\n");

    /* Check for and initialize Advanced SIMD if present */
    if (KiArm64Features & ARM64_FEATURE_ASIMD)
    {
        /* Enable Advanced SIMD for the kernel */
        DPRINT("ARM64: Advanced SIMD support enabled\n");
        /* TODO: Initialize SIMD state management
         * - Set up FPCR/FPSR defaults
         * - Configure SIMD context switching
         * - Enable SIMD for kernel if needed
         */
    }

    /* Check for and initialize crypto extensions if present */
    if (KiArm64Features & ARM64_FEATURE_AES)
    {
        DPRINT("ARM64: AES crypto acceleration available\n");
        /* TODO: Register AES crypto acceleration routines
         * - Hook into kernel crypto APIs
         * - Enable hardware-accelerated AES operations
         */
    }

    if (KiArm64Features & ARM64_FEATURE_SHA)
    {
        DPRINT("ARM64: SHA crypto acceleration available\n");
        /* TODO: Register SHA crypto acceleration routines
         * - Hook into kernel hash APIs
         * - Enable hardware-accelerated SHA operations
         */
    }

    /* Check for and initialize CRC32 if present */
    if (KiArm64Features & ARM64_FEATURE_CRC32)
    {
        DPRINT("ARM64: CRC32 hardware acceleration available\n");
        /* TODO: Register CRC32 acceleration routines
         * - Hook into kernel checksum APIs
         * - Use CRC32 instructions for performance
         */
    }

    /* Initialize cache management */
    DPRINT("ARM64: D-Cache line size: %u bytes\n", KiDcacheLineSize);
    DPRINT("ARM64: I-Cache line size: %u bytes\n", KiIcacheLineSize);

    /* Initialize timer frequency for performance counters */
    if (KiTimerFrequency != 0)
    {
        DPRINT("ARM64: System timer frequency: %llu Hz\n", KiTimerFrequency);
        /* TODO: Initialize performance counter infrastructure
         * - Set up PMU (Performance Monitoring Unit)
         * - Configure cycle counters and event counters
         * - Enable performance monitoring for profiling
         */
    }

    /* Initialize atomics support */
    if (KiArm64Features & ARM64_FEATURE_ATOMIC)
    {
        DPRINT("ARM64: Large System Extensions (LSE) atomics available\n");
        /* TODO: Use LSE atomics instead of LL/SC sequences
         * - Replace compiler atomic builtins with LSE instructions
         * - Improve performance of atomic operations
         * - Update spinlock implementations
         */
    }

    /* Platform-specific initialization (if needed) */
    /* TODO: Initialize platform-specific features
         * - Configure GIC (Generic Interrupt Controller)
         * - Set up interrupt routing and priorities
         * - Initialize SMMU if present
         * - Configure platform-specific timers
         */

    DPRINT("ARM64: Machine-dependent initialization complete\n");
}

/**
 * @brief Get the Processor Control Region for ARM64
 *
 * This function returns the PCR for the current processor.
 * For external drivers that can't access KiInitialPcr directly.
 */
#undef KeGetPcr
#undef KeGetCurrentPrcb
#undef KeGetCurrentIrql

PKIPCR
NTAPI
KeGetPcr(VOID)
{
    ASSERTMSG("TODO: ARM64 SMP PCR accessor not implemented. Update KeGetPcr before enabling SMP.",
              KeNumberProcessors <= 1);

    /* For SMP support, this should be: */
    /* return (PKIPCR)ARM64_READ_SYSREG(tpidr_el1); */
    return PCR;
}

/**
 * @brief Get the Processor Control Block for ARM64
 *
 * This function returns the PRCB for the current processor.
 * For external drivers that can't access KiInitialPcr directly.
 */
PKPRCB
NTAPI
KeGetCurrentPrcb(VOID)
{
    ASSERTMSG("TODO: ARM64 SMP PRCB accessor not implemented. Update KeGetCurrentPrcb before enabling SMP.",
              KeNumberProcessors <= 1);

    /* For SMP support, this should be: */
    /* PKIPCR Pcr = (PKIPCR)ARM64_READ_SYSREG(tpidr_el1); */
    /* return &Pcr->Prcb; */
    return &PCR->Prcb;
}

/**
 * @brief Get the current IRQL for ARM64
 *
 * This function returns the current interrupt request level.
 * For external drivers that can't access the PCR directly.
 */
KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    ASSERTMSG("TODO: ARM64 SMP IRQL accessor not implemented. Update KeGetCurrentIrql before enabling SMP.",
              KeNumberProcessors <= 1);

    /* For SMP support, this should be: */
    /* PKIPCR Pcr = (PKIPCR)ARM64_READ_SYSREG(tpidr_el1); */
    /* return Pcr->CurrentIrql; */
    return PCR->CurrentIrql;
}

#define KeGetPcr() PCR
#define KeGetCurrentPrcb() (&(PCR->Prcb))
#define KeGetCurrentIrql()             KeGetPcr()->CurrentIrql

/**
 * @brief Start all Application Processors (APs) for ARM64
 *
 * This function is responsible for starting secondary processors
 * on multiprocessor ARM64 systems. For now, it's a stub as
 * ReactOS ARM64 doesn't yet support SMP.
 */
VOID
NTAPI
KeStartAllProcessors(VOID)
{
    /* ARM64 SMP support not yet implemented */
    DPRINT1("ARM64: KeStartAllProcessors called - SMP not yet supported\n");

    /* TODO: Implement ARM64 SMP support:
     * 1. Detect number of CPUs from device tree or ACPI MADT
     * 2. Allocate per-CPU stacks (idle, DPC, interrupt)
     * 3. Initialize per-CPU PCR/PRCB structures
     * 4. Set up per-CPU TPIDR_EL1 pointing to respective PCRs
     * 5. Use PSCI or platform-specific method to start APs
     * 6. Send SGI (Software Generated Interrupt) to synchronize
     * 7. Wait for APs to complete initialization
     * 8. Update KeNumberProcessors and processor affinity masks
     */
}
