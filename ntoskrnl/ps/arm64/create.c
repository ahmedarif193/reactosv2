/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Process Creation Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* ARM64-specific includes */
#include <internal/arm64/ke.h>

/* DEFINITIONS ***************************************************************/

/* RTL Bitmap constants */
#ifndef RTL_BITMAP_RUN_NOT_FOUND
#define RTL_BITMAP_RUN_NOT_FOUND    0xFFFFFFFF
#endif

/* Memory Management constants */
#ifndef MM_KERNEL_STACK_SIZE
#define MM_KERNEL_STACK_SIZE        0x10000     /* 64KB kernel stack for ARM64 */
#endif

/* ARM64 ASID allocation constants */
#define ARM64_ASID_MAX          256     /* Maximum ASID value */
#define ARM64_ASID_RESERVED     1       /* Reserved ASID for kernel */

/* ARM64 process creation flags */
#define ARM64_PROCESS_FLAG_SECURE       0x00000001
#define ARM64_PROCESS_FLAG_COMPAT       0x00000002  /* 32-bit compatibility */

/* ARM64 Thread Context initialization values */
#define ARM64_PSTATE_EL0t       0x00000000  /* User mode with SP_EL0 */
#define ARM64_PSTATE_EL1h       0x00000005  /* Kernel mode with SP_ELx */
#define ARM64_PSTATE_DAIF_MASK  0x000003C0  /* Debug, SError, IRQ, FIQ mask */

/* ARM64 Thread/FPU state constants */
#ifndef NPX_STATE_NOT_LOADED
#define NPX_STATE_NOT_LOADED    0xA
#endif

#ifndef THREAD_PRIORITY_NORMAL
#define THREAD_PRIORITY_NORMAL  0
#endif

/* GLOBALS *******************************************************************/

/* ASID allocation bitmap - single ASID generation for simplicity */
static RTL_BITMAP AsidBitmap;
static ULONG AsidBitmapBuffer[(ARM64_ASID_MAX + 31) / 32];
static KSPIN_LOCK AsidLock;
static ULONG CurrentAsidGeneration = 1;

/* ARM64 process creation statistics */
static ULONG Arm64ProcessesCreated = 0;
static ULONG Arm64ThreadsCreated = 0;

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Allocate a new ASID (Address Space ID) for ARM64 TLB management
 */
NTSTATUS
NTAPI
PspAllocateAsid(
    OUT PUSHORT Asid)
{
    KIRQL OldIrql;
    ULONG AsidValue;

    DPRINT("Allocating ARM64 ASID\n");

    /* Acquire ASID lock */
    KeAcquireSpinLock(&AsidLock, &OldIrql);

    /* Find a free ASID */
    AsidValue = RtlFindClearBits(&AsidBitmap, 1, ARM64_ASID_RESERVED);

    if (AsidValue == RTL_BITMAP_RUN_NOT_FOUND || AsidValue >= ARM64_ASID_MAX)
    {
        DPRINT1("No free ASIDs available, implementing ASID rollover\n");

        /* For simplicity, just reuse ASIDs by clearing the bitmap */
        /* In a real implementation, would need to flush TLBs */
        RtlClearAllBits(&AsidBitmap);
        RtlSetBit(&AsidBitmap, 0);  /* Keep ASID 0 reserved */

        /* Increment generation */
        CurrentAsidGeneration++;
        if (CurrentAsidGeneration == 0)
            CurrentAsidGeneration = 1;

        /* Try again */
        AsidValue = RtlFindClearBits(&AsidBitmap, 1, ARM64_ASID_RESERVED);
    }

    if (AsidValue != RTL_BITMAP_RUN_NOT_FOUND && AsidValue < ARM64_ASID_MAX)
    {
        RtlSetBit(&AsidBitmap, AsidValue);
        *Asid = (USHORT)AsidValue;

        KeReleaseSpinLock(&AsidLock, OldIrql);

        DPRINT("Allocated ASID %d (generation %d)\n", AsidValue, CurrentAsidGeneration);
        return STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&AsidLock, OldIrql);

    DPRINT1("Failed to allocate ASID\n");
    return STATUS_NO_MEMORY;
}

/**
 * @brief Free an ASID when process is destroyed
 */
VOID
NTAPI
PspFreeAsid(
    IN USHORT Asid)
{
    KIRQL OldIrql;

    if (Asid == 0 || Asid >= ARM64_ASID_MAX)
        return;

    DPRINT("Freeing ARM64 ASID %d\n", Asid);

    KeAcquireSpinLock(&AsidLock, &OldIrql);
    RtlClearBit(&AsidBitmap, Asid);
    KeReleaseSpinLock(&AsidLock, OldIrql);
}

/**
 * @brief Create ARM64 process address space with TTBR0 setup
 */
NTSTATUS
NTAPI
PspCreateArm64AddressSpace(
    IN PEPROCESS Process,
    OUT PFN_NUMBER *DirectoryTableBase)
{
    NTSTATUS Status;
    PVOID PageDirectory;
    PFN_NUMBER PageDirectoryPfn;
    USHORT Asid;

    DPRINT("Creating ARM64 address space for process %p\n", Process);

    /* Allocate ASID for this process */
    Status = PspAllocateAsid(&Asid);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to allocate ASID: 0x%08x\n", Status);
        return Status;
    }

    /* Allocate page directory (L0 table for 48-bit VA) */
    PageDirectory = MiAllocatePoolPages(NonPagedPool, PAGE_SIZE);
    if (!PageDirectory)
    {
        DPRINT1("Failed to allocate page directory\n");
        PspFreeAsid(Asid);
        return STATUS_NO_MEMORY;
    }

    /* Zero the page directory */
    RtlZeroMemory(PageDirectory, PAGE_SIZE);

    /* Get physical address of page directory */
    PageDirectoryPfn = MiGetPfnForVirtualAddress(PageDirectory);
    *DirectoryTableBase = PageDirectoryPfn;

    /* Store ASID in process structure for later use */
    Process->Pcb.DirectoryTableBase[0] = PageDirectoryPfn << PAGE_SHIFT;
    Process->Pcb.DirectoryTableBase[1] = (ULONG_PTR)Asid;  /* Store ASID in second slot */

    /* Copy kernel mappings from system page tables */
    /* This would copy the upper half (kernel space) mappings */
    /* For now, we leave user space empty - it will be populated on demand */

    DPRINT("ARM64 address space created: TTBR0=%016llx, ASID=%d\n",
           *DirectoryTableBase << PAGE_SHIFT, Asid);

    return STATUS_SUCCESS;
}

/**
 * @brief Initialize ARM64 thread context for process creation
 */
NTSTATUS
NTAPI
PspInitializeArm64ThreadContext(
    IN PETHREAD Thread,
    IN PCONTEXT InitialContext OPTIONAL,
    IN PVOID StartAddress,
    IN BOOLEAN IsSystemThread)
{
    PKTRAP_FRAME TrapFrame;
    PVOID KernelStack;
    ULONG_PTR StackTop;

    DPRINT("Initializing ARM64 thread context for thread %p\n", Thread);

    /* Allocate kernel stack for the thread */
    KernelStack = MmCreateKernelStack(FALSE, 0);
    if (!KernelStack)
    {
        DPRINT1("Failed to allocate kernel stack\n");
        return STATUS_NO_MEMORY;
    }

    /* Set up stack pointers */
    StackTop = (ULONG_PTR)KernelStack + MM_KERNEL_STACK_SIZE;
    Thread->Tcb.InitialStack = (PVOID)StackTop;
    Thread->Tcb.StackLimit = (ULONG_PTR)KernelStack;
    Thread->Tcb.StackBase = (PVOID)StackTop;
    Thread->Tcb.KernelStack = (PVOID)(StackTop - sizeof(KTRAP_FRAME));

    /* Initialize trap frame */
    TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)StackTop - sizeof(KTRAP_FRAME));
    RtlZeroMemory(TrapFrame, sizeof(KTRAP_FRAME));

    /* Set up ARM64 registers */
    if (InitialContext)
    {
        /* Copy from provided context */
        TrapFrame->X0 = InitialContext->X0;
        TrapFrame->X1 = InitialContext->X1;
        TrapFrame->X2 = InitialContext->X2;
        TrapFrame->X3 = InitialContext->X3;
        TrapFrame->Sp = InitialContext->Sp;
        TrapFrame->Pc = InitialContext->Pc;
        TrapFrame->Pstate = InitialContext->Pstate;

        /* Copy all general purpose registers */
        TrapFrame->X4 = InitialContext->X4;
        TrapFrame->X5 = InitialContext->X5;
        TrapFrame->X6 = InitialContext->X6;
        TrapFrame->X7 = InitialContext->X7;
        TrapFrame->X8 = InitialContext->X8;
        TrapFrame->X9 = InitialContext->X9;
        TrapFrame->X10 = InitialContext->X10;
        TrapFrame->X11 = InitialContext->X11;
        TrapFrame->X12 = InitialContext->X12;
        TrapFrame->X13 = InitialContext->X13;
        TrapFrame->X14 = InitialContext->X14;
        TrapFrame->X15 = InitialContext->X15;
        TrapFrame->X16 = InitialContext->X16;
        TrapFrame->X17 = InitialContext->X17;
        TrapFrame->X18 = InitialContext->X18;
        TrapFrame->X19 = InitialContext->X19;
        TrapFrame->X20 = InitialContext->X20;
        TrapFrame->X21 = InitialContext->X21;
        TrapFrame->X22 = InitialContext->X22;
        TrapFrame->X23 = InitialContext->X23;
        TrapFrame->X24 = InitialContext->X24;
        TrapFrame->X25 = InitialContext->X25;
        TrapFrame->X26 = InitialContext->X26;
        TrapFrame->X27 = InitialContext->X27;
        TrapFrame->X28 = InitialContext->X28;
        TrapFrame->Fp = InitialContext->Fp;   /* X29 */
        TrapFrame->Lr = InitialContext->Lr;   /* X30 */
    }
    else
    {
        /* Initialize for thread start */
        TrapFrame->X0 = 0;  /* Will be set to start context */
        TrapFrame->X1 = 0;
        TrapFrame->X2 = 0;
        TrapFrame->X3 = 0;
        TrapFrame->Pc = (ULONG_PTR)StartAddress;

        /* Set processor state */
        if (IsSystemThread)
        {
            /* Kernel thread - EL1h with interrupts enabled */
            TrapFrame->Pstate = ARM64_PSTATE_EL1h;
            TrapFrame->Sp = (ULONG_PTR)StackTop - 16;  /* Leave some space */
        }
        else
        {
            /* User thread - EL0t */
            TrapFrame->Pstate = ARM64_PSTATE_EL0t;
            /* User stack will be set up later */
            TrapFrame->Sp = 0x7FFFF000;  /* Default user stack location */
        }

        /* Initialize frame pointer and link register */
        TrapFrame->Fp = 0;
        TrapFrame->Lr = 0;  /* Will cause crash if thread tries to return */
    }

    /* Set up thread execution state */
    Thread->Tcb.TrapFrame = TrapFrame;
    Thread->Tcb.PreviousMode = IsSystemThread ? KernelMode : UserMode;

    /* Initialize floating point state */
    Thread->Tcb.NpxState = NPX_STATE_NOT_LOADED;

    DPRINT("ARM64 thread context initialized: PC=%016llx, SP=%016llx, PSTATE=%08x\n",
           TrapFrame->Pc, TrapFrame->Sp, TrapFrame->Pstate);

    return STATUS_SUCCESS;
}

/**
 * @brief Create Process Environment Block (PEB) for ARM64
 */
NTSTATUS
NTAPI
PspCreateArm64Peb(
    IN PEPROCESS Process,
    OUT PPEB *Peb)
{
    PPEB ProcessPeb;
    PVOID PebBase;
    SIZE_T ViewSize;
    LARGE_INTEGER SectionOffset;
    NTSTATUS Status;

    DPRINT("Creating PEB for ARM64 process %p\n", Process);

    /* Allocate PEB in user space */
    PebBase = (PVOID)0x7FFDF000;  /* Standard PEB location for ARM64 */
    ViewSize = PAGE_SIZE;
    SectionOffset.QuadPart = 0;

    /* For now, allocate PEB using VM functions */
    Status = MmMapViewOfSystemSection(Process,
                                      &PebBase,
                                      &ViewSize,
                                      PAGE_READWRITE);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to allocate PEB: 0x%08x\n", Status);
        return Status;
    }

    ProcessPeb = (PPEB)PebBase;

    /* Zero PEB structure */
    RtlZeroMemory(ProcessPeb, sizeof(PEB));

    /* Initialize PEB fields */
    ProcessPeb->InheritedAddressSpace = FALSE;
    ProcessPeb->ImageBaseAddress = (PVOID)0x00400000;  /* Default image base */
    ProcessPeb->ProcessParameters = NULL;  /* Will be set up later */
    ProcessPeb->ProcessHeap = NULL;        /* Will be created later */
    ProcessPeb->OSMajorVersion = 6;        /* Windows compatibility */
    ProcessPeb->OSMinorVersion = 1;
    ProcessPeb->OSBuildNumber = 7601;
    ProcessPeb->OSPlatformId = VER_PLATFORM_WIN32_NT;

    /* ARM64 specific fields */
    ProcessPeb->NumberOfProcessors = KeNumberProcessors;

    /* ARM64 processor features are set in SharedUserData, not PEB */
    /* These will be populated by the HAL during system initialization */
    SharedUserData->ProcessorFeatures[PF_ARM_64BIT_LOADSTORE_ATOMIC] = TRUE;
    SharedUserData->ProcessorFeatures[PF_ARM_DIVIDE_INSTRUCTION_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_ARM_EXTERNAL_CACHE_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_ARM_VFP_32_REGISTERS_AVAILABLE] = TRUE;

    /* Set PEB in process */
    Process->Peb = ProcessPeb;
    *Peb = ProcessPeb;

    DPRINT("ARM64 PEB created at %p\n", ProcessPeb);

    return STATUS_SUCCESS;
}

/**
 * @brief Initialize process security context for ARM64
 */
NTSTATUS
NTAPI
PspInitializeArm64ProcessSecurity(
    IN PEPROCESS Process,
    IN PEPROCESS ParentProcess OPTIONAL,
    IN ULONG ProcessFlags)
{
    NTSTATUS Status;
    PACL DefaultDacl = NULL;
    PSECURITY_DESCRIPTOR SecurityDescriptor;

    DPRINT("Initializing ARM64 process security for process %p\n", Process);

    /* Initialize security fields using proper EX_FAST_REF initialization */
    ObInitializeFastReference(&Process->Token, NULL);
    Process->ObjectTable = NULL;

    /* Create basic security context */
    if (ParentProcess)
    {
        /* Inherit from parent process */
        PTOKEN ParentToken = ObFastReferenceObject(&ParentProcess->Token);
        if (ParentToken)
        {
            PTOKEN NewToken = NULL;
            /* Use SepDuplicateToken for kernel-mode token duplication */
            Status = SepDuplicateToken(ParentToken,
                                      NULL,
                                      FALSE,  /* EffectiveOnly */
                                      TokenPrimary,
                                      SecurityDelegation,
                                      KernelMode,
                                      &NewToken);
            if (NT_SUCCESS(Status))
            {
                ObInitializeFastReference(&Process->Token, NewToken);
            }
            else
            {
                DPRINT1("Failed to duplicate parent token: 0x%08x\n", Status);
                /* Continue with NULL token - will get default */
                ObInitializeFastReference(&Process->Token, NULL);
            }
            ObFastDereferenceObject(&ParentProcess->Token, ParentToken);
        }
        else
        {
            ObInitializeFastReference(&Process->Token, NULL);
        }
    }
    else
    {
        /* System process or initial process - use default security */
        ObInitializeFastReference(&Process->Token, NULL);  /* Will get system token */
    }

    /* Create handle table for the process */
    Process->ObjectTable = ExCreateHandleTable(Process);
    if (!Process->ObjectTable)
    {
        DPRINT1("Failed to create handle table\n");
        return STATUS_NO_MEMORY;
    }

    /* ARM64 specific security features */
    if (ProcessFlags & ARM64_PROCESS_FLAG_SECURE)
    {
        /* Enable ARM64 security features like Pointer Authentication */
        DPRINT("Enabling ARM64 security features\n");
        /* TODO: Set up Pointer Authentication keys */
        /* TODO: Enable Branch Target Identification (BTI) */
        /* TODO: Configure Memory Tagging Extension (MTE) if available */
    }

    DPRINT("ARM64 process security initialized\n");

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Initialize ARM64 process creation subsystem
 */
NTSTATUS
NTAPI
PspInitializeArm64ProcessCreation(VOID)
{
    DPRINT("Initializing ARM64 process creation subsystem\n");

    /* Initialize ASID allocation */
    RtlInitializeBitMap(&AsidBitmap, AsidBitmapBuffer, ARM64_ASID_MAX);
    RtlClearAllBits(&AsidBitmap);
    RtlSetBit(&AsidBitmap, 0);  /* Reserve ASID 0 */
    KeInitializeSpinLock(&AsidLock);

    DPRINT("ARM64 process creation subsystem initialized\n");

    return STATUS_SUCCESS;
}

/**
 * @brief Create ARM64 system process (PID 4 - System)
 */
NTSTATUS
NTAPI
PspCreateArm64SystemProcess(
    OUT PEPROCESS *SystemProcess,
    OUT PETHREAD *SystemThread)
{
    NTSTATUS Status;
    PEPROCESS Process = NULL;
    PETHREAD Thread = NULL;
    PFN_NUMBER DirectoryTableBase;
    PPEB Peb;

    DPRINT("Creating ARM64 system process\n");

    /* Allocate EPROCESS structure */
    Process = ExAllocatePoolWithTag(NonPagedPool, sizeof(EPROCESS), 'corP');
    if (!Process)
    {
        DPRINT1("Failed to allocate EPROCESS\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Process, sizeof(EPROCESS));

    /* Initialize basic EPROCESS fields */
    Process->Pcb.Header.Type = ProcessObject;
    Process->Pcb.Header.Size = sizeof(KPROCESS) / sizeof(ULONG);
    InitializeListHead(&Process->Pcb.Header.WaitListHead);
    InitializeListHead(&Process->ActiveProcessLinks);
    InitializeListHead(&Process->ThreadListHead);

    /* Set process ID */
    Process->UniqueProcessId = (HANDLE)4;  /* System process is always PID 4 */
    Process->InheritedFromUniqueProcessId = NULL;

    /* Initialize locks */
    ExInitializePushLock(&Process->ProcessLock);
    ExInitializeRundownProtection(&Process->RundownProtect);

    /* Create address space */
    Status = PspCreateArm64AddressSpace(Process, &DirectoryTableBase);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create address space: 0x%08x\n", Status);
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Initialize security context */
    Status = PspInitializeArm64ProcessSecurity(Process, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize security: 0x%08x\n", Status);
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Create PEB for the system process */
    Status = PspCreateArm64Peb(Process, &Peb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create PEB: 0x%08x\n", Status);
        /* Continue without PEB for system process */
    }

    /* Allocate initial thread */
    Thread = ExAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), 'erhT');
    if (!Thread)
    {
        DPRINT1("Failed to allocate ETHREAD\n");
        ExFreePoolWithTag(Process, 'corP');
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Thread, sizeof(ETHREAD));

    /* Initialize basic ETHREAD fields */
    Thread->Tcb.Header.Type = ThreadObject;
    Thread->Tcb.Header.Size = sizeof(KTHREAD) / sizeof(ULONG);
    InitializeListHead(&Thread->Tcb.Header.WaitListHead);
    InitializeListHead(&Thread->ThreadListEntry);

    /* Set thread identifiers */
    Thread->Cid.UniqueProcess = Process->UniqueProcessId;
    Thread->Cid.UniqueThread = (HANDLE)8;  /* First thread ID */
    Thread->ThreadsProcess = Process;

    /* Initialize ARM64 thread context */
    Status = PspInitializeArm64ThreadContext(Thread,
                                              NULL,
                                              (PVOID)KiSystemStartup,
                                              TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize thread context: 0x%08x\n", Status);
        ExFreePoolWithTag(Thread, 'erhT');
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Link thread to process */
    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);
    Process->ActiveThreads = 1;

    /* Set thread state */
    Thread->Tcb.State = Ready;
    Thread->Tcb.Priority = HIGH_PRIORITY;
    Thread->Tcb.BasePriority = HIGH_PRIORITY;

    /* Update statistics */
    Arm64ProcessesCreated++;
    Arm64ThreadsCreated++;

    *SystemProcess = Process;
    *SystemThread = Thread;

    DPRINT("ARM64 system process created: Process=%p Thread=%p\n", Process, Thread);

    return STATUS_SUCCESS;
}

/**
 * @brief Create user process with ARM64 support
 */
NTSTATUS
NTAPI
PspCreateArm64UserProcess(
    OUT PEPROCESS *UserProcess,
    IN PEPROCESS ParentProcess OPTIONAL,
    IN PUNICODE_STRING ImageFileName,
    IN ULONG ProcessFlags)
{
    NTSTATUS Status;
    PEPROCESS Process = NULL;
    PFN_NUMBER DirectoryTableBase;
    PPEB Peb;
    HANDLE ParentPid = NULL;

    DPRINT("Creating ARM64 user process: %wZ\n", ImageFileName);

    if (ParentProcess)
        ParentPid = ParentProcess->UniqueProcessId;

    /* Allocate EPROCESS structure */
    Process = ExAllocatePoolWithTag(NonPagedPool, sizeof(EPROCESS), 'corP');
    if (!Process)
    {
        DPRINT1("Failed to allocate EPROCESS\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Process, sizeof(EPROCESS));

    /* Initialize basic EPROCESS fields */
    Process->Pcb.Header.Type = ProcessObject;
    Process->Pcb.Header.Size = sizeof(KPROCESS) / sizeof(ULONG);
    InitializeListHead(&Process->Pcb.Header.WaitListHead);
    InitializeListHead(&Process->ActiveProcessLinks);
    InitializeListHead(&Process->ThreadListHead);

    /* Assign unique process ID */
    static ULONG NextPid = 100;  /* Start user PIDs at 100 */
    Process->UniqueProcessId = (HANDLE)(ULONG_PTR)(NextPid += 4);
    Process->InheritedFromUniqueProcessId = ParentPid;

    /* Initialize locks */
    ExInitializePushLock(&Process->ProcessLock);
    ExInitializeRundownProtection(&Process->RundownProtect);

    /* Store image file name */
    if (ImageFileName && ImageFileName->Buffer)
    {
        USHORT Length = min(ImageFileName->Length, sizeof(Process->ImageFileName) - sizeof(WCHAR));
        RtlCopyMemory(Process->ImageFileName, ImageFileName->Buffer, Length);
        Process->ImageFileName[Length / sizeof(WCHAR)] = L'\0';
    }

    /* Create address space with TTBR0 */
    Status = PspCreateArm64AddressSpace(Process, &DirectoryTableBase);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create address space: 0x%08x\n", Status);
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Initialize security context */
    Status = PspInitializeArm64ProcessSecurity(Process, ParentProcess, ProcessFlags);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize security: 0x%08x\n", Status);
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Create PEB */
    Status = PspCreateArm64Peb(Process, &Peb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create PEB: 0x%08x\n", Status);
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }

    /* Set process priority class */
    Process->Pcb.BasePriority = PROCESS_PRIORITY_NORMAL;

    /* Update statistics */
    Arm64ProcessesCreated++;

    *UserProcess = Process;

    DPRINT("ARM64 user process created: Process=%p PID=%d\n",
           Process, HandleToUlong(Process->UniqueProcessId));

    return STATUS_SUCCESS;
}

/**
 * @brief Create thread for ARM64 process
 */
NTSTATUS
NTAPI
PspCreateArm64ProcessThread(
    IN PEPROCESS Process,
    OUT PETHREAD *ProcessThread,
    IN PVOID StartAddress,
    IN PVOID StartParameter,
    IN PCONTEXT InitialContext OPTIONAL,
    IN BOOLEAN CreateSuspended)
{
    NTSTATUS Status;
    PETHREAD Thread = NULL;

    DPRINT("Creating ARM64 thread for process %p\n", Process);

    /* Allocate ETHREAD structure */
    Thread = ExAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), 'erhT');
    if (!Thread)
    {
        DPRINT1("Failed to allocate ETHREAD\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Thread, sizeof(ETHREAD));

    /* Initialize basic ETHREAD fields */
    Thread->Tcb.Header.Type = ThreadObject;
    Thread->Tcb.Header.Size = sizeof(KTHREAD) / sizeof(ULONG);
    InitializeListHead(&Thread->Tcb.Header.WaitListHead);
    InitializeListHead(&Thread->ThreadListEntry);
    InitializeListHead(&Thread->IrpList);
    InitializeListHead(&Thread->ActiveTimerListHead);

    /* Assign unique thread ID */
    static ULONG NextTid = 100;
    Thread->Cid.UniqueProcess = Process->UniqueProcessId;
    Thread->Cid.UniqueThread = (HANDLE)(ULONG_PTR)(NextTid += 4);
    Thread->ThreadsProcess = Process;

    /* Initialize ARM64 thread context */
    Status = PspInitializeArm64ThreadContext(Thread,
                                              InitialContext,
                                              StartAddress,
                                              FALSE);  /* User thread */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize thread context: 0x%08x\n", Status);
        ExFreePoolWithTag(Thread, 'erhT');
        return Status;
    }

    /* Set start parameter in X0 register */
    if (Thread->Tcb.TrapFrame)
    {
        ((PKTRAP_FRAME)Thread->Tcb.TrapFrame)->X0 = (ULONG_PTR)StartParameter;
    }

    /* Link thread to process */
    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);
    Process->ActiveThreads++;

    /* Set initial thread state */
    if (CreateSuspended)
    {
        Thread->Tcb.State = Suspended;
        Thread->Tcb.SuspendCount = 1;
    }
    else
    {
        Thread->Tcb.State = Ready;
        Thread->Tcb.SuspendCount = 0;
    }

    Thread->Tcb.Priority = THREAD_PRIORITY_NORMAL;
    Thread->Tcb.BasePriority = THREAD_PRIORITY_NORMAL;
    Thread->ExitStatus = STATUS_PENDING;

    /* Update statistics */
    Arm64ThreadsCreated++;

    *ProcessThread = Thread;

    DPRINT("ARM64 thread created: Thread=%p TID=%d PC=%016llx\n",
           Thread, HandleToUlong(Thread->Cid.UniqueThread),
           ((PKTRAP_FRAME)Thread->Tcb.TrapFrame)->Pc);

    return STATUS_SUCCESS;
}

/**
 * @brief Get ARM64 process creation statistics
 */
VOID
NTAPI
PspGetArm64ProcessStatistics(
    OUT PULONG ProcessesCreated,
    OUT PULONG ThreadsCreated,
    OUT PULONG AsidsAllocated)
{
    KIRQL OldIrql;
    ULONG AsidCount = 0;

    *ProcessesCreated = Arm64ProcessesCreated;
    *ThreadsCreated = Arm64ThreadsCreated;

    /* Count allocated ASIDs */
    KeAcquireSpinLock(&AsidLock, &OldIrql);
    AsidCount = RtlNumberOfSetBits(&AsidBitmap);
    KeReleaseSpinLock(&AsidLock, OldIrql);

    *AsidsAllocated = AsidCount;
}

/* EOF */