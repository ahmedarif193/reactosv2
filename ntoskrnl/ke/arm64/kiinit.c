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

/* ARM64 Specific Constants */
#define PCR_MAJOR_VERSION       1
#define PCR_MINOR_VERSION       1

/* FORWARD DECLARATIONS *****************************************************/

DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupReal(IN PLOADER_PARAMETER_BLOCK LoaderBlock);

/* GLOBALS *******************************************************************/

/* ARM64 processor information */
ULONG KiProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM;
ULONG KiProcessorLevel = 8;  /* ARMv8 */
ULONG KiProcessorRevision = 0;

/* ARM64 CPU features */
ULONG64 KiArm64Features = 0;

/* ARM64 Cache information */
ULONG KiDcacheLineSize = 64;
ULONG KiIcacheLineSize = 64;

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
    
    /* Read processor identification */
    midr = __readmidr();
    idr0 = ARM64_READ_SYSREG(id_aa64isar0_el1);
    pfr0 = ARM64_READ_SYSREG(id_aa64pfr0_el1);
    
    /* Extract processor information */
    KiProcessorRevision = (ULONG)(midr & 0xF);
    
    /* Detect CPU features */
    KiArm64Features = 0;
    
    /* Check for AES support */
    if ((idr0 & 0xF0) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_AES;
        DPRINT("ARM64: AES encryption support detected\n");
    }
    
    /* Check for SHA support */
    if (((idr0 >> 8) & 0xF) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_SHA;
        DPRINT("ARM64: SHA hash support detected\n");
    }
    
    /* Check for floating point support */
    if ((pfr0 & 0xF) != 0xF)
    {
        KiArm64Features |= ARM64_FEATURE_FP;
        DPRINT("ARM64: Floating point support detected\n");
    }
    
    /* Check for Advanced SIMD support */
    if (((pfr0 >> 4) & 0xF) != 0xF)
    {
        KiArm64Features |= ARM64_FEATURE_ASIMD;
        DPRINT("ARM64: Advanced SIMD support detected\n");
    }
    
    /* Check for CRC32 support */
    if (((idr0 >> 16) & 0xF) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_CRC32;
        DPRINT("ARM64: CRC32 support detected\n");
    }
    
    /* Check for Atomic instructions support */
    if (((idr0 >> 20) & 0xF) != 0)
    {
        KiArm64Features |= ARM64_FEATURE_ATOMIC;
        DPRINT("ARM64: Atomic instructions support detected\n");
    }
    
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
    
    /* Read cache type register */
    ctr = ARM64_READ_SYSREG(ctr_el0);
    
    /* Extract cache line sizes */
    KiDcacheLineSize = 4 << ((ctr & 0xF0000) >> 16);
    KiIcacheLineSize = 4 << (ctr & 0xF);
    
    /* Select L1 data cache */
    ARM64_WRITE_SYSREG(csselr_el1, 0);
    ARM64_ISB();
    
    ccsidr = ARM64_READ_SYSREG(ccsidr_el1);
    
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
    /* Read timer frequency */
    KiTimerFrequency = __readcntfrq();
    
    if (KiTimerFrequency == 0)
    {
        /* Use default frequency if not set by firmware */
        KiTimerFrequency = ARM64_TIMER_FREQ_DEFAULT;
        DPRINT("ARM64: Using default timer frequency %llu Hz\n", KiTimerFrequency);
    }
    else
    {
        DPRINT("ARM64: Timer frequency %llu Hz\n", KiTimerFrequency);
    }
    
    /* Disable timer interrupt initially */
    __writecntp_ctl_el0(0);
    ARM64_ISB();
}

/**
 * @brief Initialize ARM64 Memory Management Unit
 */
VOID
NTAPI
KiInitializeArm64Mmu(VOID)
{
    ULONGLONG tcr, mair, sctlr;
    
    /* Configure Memory Attribute Indirection Register */
    mair = ARM64_MAIR_VALUE;
    __writemair_el1(mair);
    
    /* Configure Translation Control Register */
    tcr = ARM64_TCR_DEFAULT;
    __writetcr_el1(tcr);
    
    /* Read current SCTLR */
    sctlr = __readsctlr_el1();
    
    /* Enable MMU if not already enabled */
    if (!(sctlr & ARM64_SCTLR_M))
    {
        DPRINT("ARM64: Enabling MMU\n");
        sctlr |= ARM64_SCTLR_DEFAULT;
        __writesctlr_el1(sctlr);
    }
    else
    {
        DPRINT("ARM64: MMU already enabled\n");
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
    ULONGLONG vbar;
    
    /* Set Vector Base Address Register */
    vbar = (ULONGLONG)&KiExceptionVectors;
    __writevbar_el1(vbar);
    
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
    IN PVOID DpcStack
)
{
    /* Clear the PCR */
    RtlZeroMemory(Pcr, sizeof(KIPCR));

    /* Set up self-referential members required by common macros */
    Pcr->Self = (PKPCR)(PVOID)Pcr;
    Pcr->Used_Self = (PVOID)Pcr;
    Pcr->PcrReserved0 = NULL;
    Pcr->LockArray = NULL;
    Pcr->CurrentIrql = PASSIVE_LEVEL;
    Pcr->Prcb.CurrentThread = NULL;
    Pcr->Prcb.NextThread = NULL;
    Pcr->Prcb.IdleThread = NULL;
    Pcr->Prcb.DpcStack = NULL;
    Pcr->Prcb.MultiThreadProcessorSet = 0;
    Pcr->Prcb.BuildType = 0;
    Pcr->Prcb.FeatureBits = 0;

    /* Set up basic PCR fields */
    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;
    Pcr->Prcb.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->Prcb.MinorVersion = PRCB_MINOR_VERSION;
    
    /* Set processor number */
    Pcr->Prcb.Number = (UCHAR)ProcessorNumber;
    Pcr->Prcb.SetMember = 1ULL << ProcessorNumber;
    
    /* Initialize PRCB */
    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.IdleThread = IdleThread;
    Pcr->Prcb.DpcStack = DpcStack;
    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;

    /* Initialize processor features */
    Pcr->Prcb.FeatureBits = (ULONG)KiArm64Features;
    
    /* Initialize cache information */
    Pcr->Prcb.CacheLineSize = KiDcacheLineSize;
    
    DPRINT("ARM64: PCR initialized for processor %u\n", ProcessorNumber);
}

/**
 * @brief Early ARM64 processor initialization
 */
VOID
NTAPI
KiInitializeProcessor(VOID)
{
    ULONGLONG el;
    
    /* Check current Exception Level */
    el = __readcurrentel() >> 2;
    DPRINT("ARM64: Running at Exception Level %llu\n", el);
    
    if (el != 1)
    {
        DPRINT1("ARM64: Warning - Not running at EL1!\n");
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
    UNREFERENCED_PARAMETER(Prcb);
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* Early processor initialization */
    KiInitializeProcessor();

    /* Initialize PCR */
    KiInitializeArm64Pcr((PKIPCR)&KiInitialPcr, Number, InitThread, IdleStack);
    
    /* Initialize PRCB */
    RtlCopyMemory(&KiInitialPrcb, &KiInitialPcr.Prcb, sizeof(KPRCB));

    /* Set up initial thread/process information when available */
    if (InitThread != NULL && InitProcess != NULL)
    {
        InitThread->ApcState.Process = InitProcess;

        /* Preserve the loader supplied top-level translation base */
        InitProcess->DirectoryTableBase[0] = __readttbr1_el1();
    }
    else
    {
        DPRINT1("ARM64: KiInitializeKernel called without initial thread/process context\n");
    }

    DPRINT("ARM64: Kernel initialization completed\n");

    return STATUS_SUCCESS;
}

/**
 * @brief System startup routine called from boot code
 */
VOID
NTAPI
KiSystemStartupBootStack(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
)
{
    /* Emit early debug message to track kernel handoff */
    {
        static const CHAR Message[] = "ARM64: KiSystemStartupBootStack entry - LoaderBlock: 0x";
        const CHAR *Current = Message;
        volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;
        ULONG_PTR Address = (ULONG_PTR)LoaderBlock;
        ULONG i;

        /* Print the message */
        while (*Current != '\0')
        {
            *Pl011Dr = (UCHAR)(*Current++);
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
                __asm__ __volatile__("nop");
        }

        /* Print LoaderBlock address in hex */
        for (i = 0; i < 16; i++)
        {
            UCHAR nibble = (UCHAR)((Address >> (60 - i * 4)) & 0xF);
            UCHAR hexChar = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            *Pl011Dr = hexChar;
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
                __asm__ __volatile__("nop");
        }

        *Pl011Dr = '\r';
        *Pl011Dr = '\n';
    }

    DPRINT("ARM64: System startup - LoaderBlock at 0x%p\n", LoaderBlock);

    /* Validate LoaderBlock pointer */
    if (!LoaderBlock)
    {
        static const CHAR ErrorMsg[] = "ARM64: FATAL - NULL LoaderBlock\r\n";
        const CHAR *Current = ErrorMsg;
        volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

        while (*Current != '\0')
        {
            *Pl011Dr = (UCHAR)(*Current++);
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
                __asm__ __volatile__("nop");
        }

        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    /* Disable interrupts during initialization */
    ARM64_DISABLE_INTERRUPTS();

    /* Emit debug checkpoint */
    {
        static const CHAR Message[] = "ARM64: Starting kernel initialization\r\n";
        const CHAR *Current = Message;
        volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

        while (*Current != '\0')
        {
            *Pl011Dr = (UCHAR)(*Current++);
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
                __asm__ __volatile__("nop");
        }
    }

    /* Early kernel initialization */
    KiInitializeKernel(NULL, NULL, NULL, NULL, 0, LoaderBlock);

    /* Emit debug checkpoint */
    {
        static const CHAR Message[] = "ARM64: Calling KiSystemStartup\r\n";
        const CHAR *Current = Message;
        volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

        while (*Current != '\0')
        {
            *Pl011Dr = (UCHAR)(*Current++);
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
                __asm__ __volatile__("nop");
        }
    }

    /* Call generic kernel startup */
    KiSystemStartupReal(LoaderBlock);

    /* Should never reach here */
    KeBugCheck(PHASE0_INITIALIZATION_FAILED);
}

/**
 * @brief Main system startup routine for ARM64
 */
CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupReal(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    CCHAR Cpu;

    DPRINT("ARM64: KiSystemStartupReal - LoaderBlock at 0x%p\n", LoaderBlock);

    /* Emit a serial banner so automated tests can confirm kernel entry. */
    {
        static const CHAR Message[] = "ARM64 kernel entry reached\r\n";
        const CHAR *Current = Message;
        volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

        while (*Current != '\0')
        {
            *Pl011Dr = (UCHAR)(*Current++);

            /* Crude delay to give the UART time to shift out data */
            for (volatile ULONG Delay = 0; Delay < 1000; ++Delay)
            {
                __asm__ __volatile__("nop");
            }
        }
    }

    /* TODO: Get the current CPU number - for now assume CPU 0 */
    Cpu = 0;
    KeNumberProcessors = 1; // Start with 1 processor

    /* LoaderBlock initialization for Cpu 0 */
    if (Cpu == 0)
    {
        /* Set the initial LoaderBlock pointer */
        KeLoaderBlock = LoaderBlock;

        /* TODO: Initialize PCR for ARM64 */
        /* TODO: Initialize IDT/Exception vectors */
        /* TODO: Initialize memory management */
        /* TODO: Initialize scheduler */
    }

    /* TODO: For now, just loop to prevent returning */
    /* Real implementation would initialize the system and start the scheduler */
    DPRINT1("ARM64: KiSystemStartupReal not yet implemented - bugchecking to avoid hang\n");

    /* Fail fast until full bring-up is implemented */
    ARM64_DISABLE_INTERRUPTS();
    KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 0, 0, 0, 0);
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
        /* TODO: Initialize SIMD state management */
    }

    /* Check for and initialize crypto extensions if present */
    if (KiArm64Features & ARM64_FEATURE_AES)
    {
        DPRINT("ARM64: AES crypto acceleration available\n");
        /* TODO: Register crypto acceleration routines */
    }

    if (KiArm64Features & ARM64_FEATURE_SHA)
    {
        DPRINT("ARM64: SHA crypto acceleration available\n");
        /* TODO: Register SHA acceleration routines */
    }

    /* Check for and initialize CRC32 if present */
    if (KiArm64Features & ARM64_FEATURE_CRC32)
    {
        DPRINT("ARM64: CRC32 hardware acceleration available\n");
        /* TODO: Register CRC32 acceleration routines */
    }

    /* Initialize cache management */
    DPRINT("ARM64: D-Cache line size: %u bytes\n", KiDcacheLineSize);
    DPRINT("ARM64: I-Cache line size: %u bytes\n", KiIcacheLineSize);

    /* Initialize timer frequency for performance counters */
    if (KiTimerFrequency != 0)
    {
        DPRINT("ARM64: System timer frequency: %llu Hz\n", KiTimerFrequency);
        /* TODO: Initialize performance counter infrastructure */
    }

    /* Initialize atomics support */
    if (KiArm64Features & ARM64_FEATURE_ATOMIC)
    {
        DPRINT("ARM64: Large System Extensions (LSE) atomics available\n");
        /* TODO: Use LSE atomics instead of LL/SC sequences */
    }

    /* Platform-specific initialization (if needed) */
    /* TODO: Initialize platform-specific features like GIC, etc. */

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
    /* TODO: In SMP, this should read TPIDR_EL1 to get per-CPU PCR */
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
    /* TODO: In SMP, this should read TPIDR_EL1 to get per-CPU PCR */
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
    /* TODO: In SMP, this should read from per-CPU PCR */
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
     * 2. Allocate stacks for each AP
     * 3. Send SGI (Software Generated Interrupt) to wake APs
     * 4. Initialize each AP's PCR/PRCB
     * 5. Start AP initialization sequence
     */
}
