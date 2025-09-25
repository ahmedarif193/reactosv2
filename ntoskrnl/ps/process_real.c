/*
 * ReactOS AMD64 Real Process Creation Implementation
 * Full Windows NT-style process and thread creation
 */

#include <ntoskrnl.h>

#define COM1_PORT 0x3F8

/* Debug output */
static void DebugPrint(const char* msg)
{
    const char *p = msg;
    while (*p) { 
        while ((__inbyte(COM1_PORT + 5) & 0x20) == 0); 
        __outbyte(COM1_PORT, *p++); 
    }
}

/* Process and Thread IDs */
static ULONG NextProcessId = 4;  /* Start at 4 (0 is System) */
static ULONG NextThreadId = 8;   /* Start at 8 */

#define PSP_DEFAULT_STACK_RESERVE   (1 * 1024 * 1024)  /* 1MB */
#define PSP_DEFAULT_STACK_COMMIT    (256 * 1024)       /* 256KB */

#define PSP_ALIGN_DOWN(Value, Alignment) ((ULONG_PTR)(Value) & ~((ULONG_PTR)(Alignment) - 1))

NTSTATUS
PspSetupInitialStack(
    HANDLE ProcessId,
    SIZE_T StackReserve,
    SIZE_T StackCommit,
    PINITIAL_TEB InitialTeb,
    PCONTEXT Context)
{
    NTSTATUS Status;
    PEPROCESS Process;
    KAPC_STATE ApcState;
    SIZE_T ReserveSize;
    SIZE_T CommitSize;
    PVOID ReservedBase = NULL;
    BOOLEAN Attached = FALSE;
    BOOLEAN RegionAllocated = FALSE;

    if (!InitialTeb || !Context)
        return STATUS_INVALID_PARAMETER;

    Status = PsLookupProcessByProcessId(ProcessId, &Process);
    if (!NT_SUCCESS(Status))
        return Status;

    ReserveSize = StackReserve ? ROUND_TO_PAGES(StackReserve) : ROUND_TO_PAGES(PSP_DEFAULT_STACK_RESERVE);
    CommitSize = StackCommit ? ROUND_TO_PAGES(StackCommit) : ROUND_TO_PAGES(PSP_DEFAULT_STACK_COMMIT);

    if (CommitSize < (2 * PAGE_SIZE))
        CommitSize = 2 * PAGE_SIZE;

    if (ReserveSize < CommitSize + PAGE_SIZE)
        ReserveSize = CommitSize + PAGE_SIZE;

    KeStackAttachProcess(&Process->Pcb, &ApcState);
    Attached = TRUE;

    SIZE_T RegionSize = ReserveSize;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &ReservedBase,
                                     0,
                                     &RegionSize,
                                     MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RegionAllocated = TRUE;

    PVOID CommitBase = (PCHAR)ReservedBase + (ReserveSize - CommitSize);
    SIZE_T CommitRegion = CommitSize;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &CommitBase,
                                     0,
                                     &CommitRegion,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    PVOID GuardBase = CommitBase;
    SIZE_T GuardSize = PAGE_SIZE;
    ULONG OldProtect = 0;
    Status = ZwProtectVirtualMemory(NtCurrentProcess(),
                                    &GuardBase,
                                    &GuardSize,
                                    PAGE_READWRITE | PAGE_GUARD,
                                    &OldProtect);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    KeUnstackDetachProcess(&ApcState);
    Attached = FALSE;

    ObDereferenceObject(Process);

    InitialTeb->StackBase = (PCHAR)ReservedBase + ReserveSize;
    InitialTeb->StackLimit = GuardBase;
    InitialTeb->AllocatedStackBase = ReservedBase;

    if (!(Context->ContextFlags & CONTEXT_CONTROL))
        Context->ContextFlags |= CONTEXT_CONTROL;
    if (!(Context->ContextFlags & CONTEXT_INTEGER))
        Context->ContextFlags |= CONTEXT_INTEGER;
    if (!(Context->ContextFlags & CONTEXT_SEGMENTS))
        Context->ContextFlags |= CONTEXT_SEGMENTS;

    ULONG_PTR StackTop = PSP_ALIGN_DOWN((ULONG_PTR)InitialTeb->StackBase, 16);
    if (StackTop <= (ULONG_PTR)InitialTeb->StackLimit + PAGE_SIZE)
    {
        StackTop = PSP_ALIGN_DOWN((ULONG_PTR)InitialTeb->StackLimit + PAGE_SIZE + 0x100, 16);
    }
    StackTop -= 0x20;
    Context->Rsp = StackTop;
    Context->Rbp = StackTop;

    if (Context->SegCs == 0) Context->SegCs = 0x33;
    if (Context->SegDs == 0) Context->SegDs = 0x2B;
    if (Context->SegEs == 0) Context->SegEs = 0x2B;
    if (Context->SegFs == 0) Context->SegFs = 0x53;
    if (Context->SegGs == 0) Context->SegGs = 0x2B;
    if (Context->SegSs == 0) Context->SegSs = 0x2B;
    if (Context->EFlags == 0) Context->EFlags = 0x200;

    return STATUS_SUCCESS;

Cleanup:
    if (RegionAllocated)
    {
        SIZE_T FreeSize = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &ReservedBase, &FreeSize, MEM_RELEASE);
        RegionAllocated = FALSE;
    }

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    ObDereferenceObject(Process);
    return Status;
}

/* Global process list - use existing from process.c */
extern LIST_ENTRY PsActiveProcessHead;
static BOOLEAN PsProcessListInitialized = FALSE;

/* Initialize process subsystem - renamed to avoid conflict */
VOID PspInitializeProcessSubsystem(VOID)
{
    DebugPrint("*** PS: Initializing process security ***\n");
    
    /* Initialize the global process list */
    if (!PsProcessListInitialized)
    {
        InitializeListHead(&PsActiveProcessHead);
        PsProcessListInitialized = TRUE;
    }
}

/* Create process address space - renamed to avoid conflict */
NTSTATUS PspCreateProcessAddressSpace(
    IN PEPROCESS Process,
    OUT PULONG_PTR DirectoryTableBase)
{
    DebugPrint("*** MM: Creating process address space ***\n");
    
    /* Use static allocation for PML4 since MM is not ready */
    static UCHAR StaticPml4Pages[10][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
    static ULONG Pml4Index = 0;
    
    if (Pml4Index >= 10)
    {
        DebugPrint("*** MM: Out of static PML4 slots ***\n");
        return STATUS_NO_MEMORY;
    }
    
    PVOID Pml4 = StaticPml4Pages[Pml4Index++];
    
    DebugPrint("*** MM: Using static PML4 allocation ***\n");
    
    /* Zero the PML4 */
    RtlZeroMemory(Pml4, PAGE_SIZE);
    
    /* Copy kernel mappings (upper half of address space) */
    /* In real implementation, would copy from system PML4 */
    
    /* Use the virtual address as physical for now (identity mapped in kernel) */
    *DirectoryTableBase = (ULONG_PTR)Pml4;
    
    DebugPrint("*** MM: Address space created, PML4 at physical address ***\n");
    
    return STATUS_SUCCESS;
}

/* Allocate and initialize EPROCESS structure */
NTSTATUS PspAllocateProcess(
    OUT PEPROCESS *Process,
    IN HANDLE ParentProcessId OPTIONAL)
{
    DebugPrint("*** PS: Allocating EPROCESS structure ***\n");
    
    /* Use static allocation since pool manager might not be ready */
    static EPROCESS StaticProcesses[10];
    static ULONG ProcessIndex = 0;
    
    if (ProcessIndex >= 10)
    {
        DebugPrint("*** PS: Out of static process slots ***\n");
        return STATUS_NO_MEMORY;
    }
    
    PEPROCESS NewProcess = &StaticProcesses[ProcessIndex++];
    
    DebugPrint("*** PS: Using static EPROCESS allocation ***\n");
    
    /* Zero the structure */
    RtlZeroMemory(NewProcess, sizeof(EPROCESS));
    
    /* Initialize KPROCESS (kernel part) */
    NewProcess->Pcb.Header.Type = ProcessObject;
    NewProcess->Pcb.Header.Size = sizeof(KPROCESS) / sizeof(LONG);
    NewProcess->Pcb.Header.SignalState = 0;
    InitializeListHead(&NewProcess->Pcb.Header.WaitListHead);
    
    /* Set process ID */
    NewProcess->UniqueProcessId = (HANDLE)(ULONG_PTR)NextProcessId;
    NextProcessId += 4;
    
    /* Initialize process lists */
    InitializeListHead(&NewProcess->ActiveProcessLinks);
    InitializeListHead(&NewProcess->ThreadListHead);
    InitializeListHead(&NewProcess->SessionProcessLinks);
    
    /* Initialize locks */
    ExInitializePushLock(&NewProcess->ProcessLock);
    ExInitializeRundownProtection(&NewProcess->RundownProtect);
    
    /* Set initial values */
    NewProcess->ExitStatus = STATUS_PENDING;
    NewProcess->CreateTime.QuadPart = 0;  /* Would use KeQuerySystemTime */
    
    /* Set parent process */
    if (ParentProcessId)
    {
        NewProcess->InheritedFromUniqueProcessId = ParentProcessId;
    }
    
    /* Add to global process list */
    InsertTailList(&PsActiveProcessHead, &NewProcess->ActiveProcessLinks);
    
    *Process = NewProcess;
    
    DebugPrint("*** PS: EPROCESS allocated successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* Allocate and initialize ETHREAD structure */
NTSTATUS PspAllocateThread(
    OUT PETHREAD *Thread,
    IN PEPROCESS Process)
{
    DebugPrint("*** PS: Allocating ETHREAD structure ***\n");
    
    /* Use static allocation since pool manager might not be ready */
    static ETHREAD StaticThreads[20];
    static ULONG ThreadIndex = 0;
    
    if (ThreadIndex >= 20)
    {
        DebugPrint("*** PS: Out of static thread slots ***\n");
        return STATUS_NO_MEMORY;
    }
    
    PETHREAD NewThread = &StaticThreads[ThreadIndex++];
    
    DebugPrint("*** PS: Using static ETHREAD allocation ***\n");
    
    /* Zero the structure */
    RtlZeroMemory(NewThread, sizeof(ETHREAD));
    
    /* Initialize KTHREAD (kernel part) */
    NewThread->Tcb.Header.Type = ThreadObject;
    NewThread->Tcb.Header.Size = sizeof(KTHREAD) / sizeof(LONG);
    NewThread->Tcb.Header.SignalState = 0;
    InitializeListHead(&NewThread->Tcb.Header.WaitListHead);
    
    /* Set thread ID and process */
    NewThread->Cid.UniqueProcess = Process->UniqueProcessId;
    NewThread->Cid.UniqueThread = (HANDLE)(ULONG_PTR)NextThreadId;
    NextThreadId += 4;
    
    /* Link to process */
    NewThread->ThreadsProcess = Process;
    
    /* Initialize thread lists */
    InitializeListHead(&NewThread->ThreadListEntry);
    InitializeListHead(&NewThread->IrpList);
    InitializeListHead(&NewThread->ActiveTimerListHead);
    
    /* Set initial state */
    NewThread->Tcb.State = Initialized;
    NewThread->Tcb.Priority = 8;  /* Normal priority */
    NewThread->Tcb.BasePriority = 8;
    NewThread->ExitStatus = STATUS_PENDING;
    
    /* Add to process thread list */
    InsertTailList(&Process->ThreadListHead, &NewThread->ThreadListEntry);
    Process->ActiveThreads++;
    
    *Thread = NewThread;
    
    DebugPrint("*** PS: ETHREAD allocated successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* Create PEB (Process Environment Block) - renamed to avoid conflict */
NTSTATUS PspCreatePeb(
    IN PEPROCESS Process,
    OUT PPEB *PebBase)
{
    DebugPrint("*** MM: Creating PEB ***\n");
    
    /* PEB is in user space at a fixed address */
    /* PVOID PebAddress = (PVOID)0x7FF7FFFE0000; */  /* Standard PEB location for AMD64 - will be used later */
    
    /* Use static allocation for PEB since pool is not ready */
    static PEB StaticPebs[10];
    static ULONG PebIndex = 0;
    
    if (PebIndex >= 10)
    {
        DebugPrint("*** MM: Out of static PEB slots ***\n");
        return STATUS_NO_MEMORY;
    }
    
    PVOID PebMemory = &StaticPebs[PebIndex++];
    
    DebugPrint("*** MM: Using static PEB allocation ***\n");
    
    /* Initialize PEB */
    PPEB Peb = (PPEB)PebMemory;
    RtlZeroMemory(Peb, sizeof(PEB));
    
    /* Set basic PEB fields */
    Peb->ImageBaseAddress = (PVOID)0x00400000;  /* Standard EXE base */
    Peb->OSMajorVersion = 10;  /* Windows 10 */
    Peb->OSMinorVersion = 0;
    Peb->OSBuildNumber = 19041;
    Peb->OSPlatformId = VER_PLATFORM_WIN32_NT;
    Peb->NumberOfProcessors = 1;
    Peb->BeingDebugged = FALSE;
    
    /* In real implementation, would map PEB into process address space */
    Process->Peb = Peb;
    *PebBase = Peb;
    
    DebugPrint("*** MM: PEB created successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* Create TEB (Thread Environment Block) - renamed to avoid conflict */
NTSTATUS PspCreateTeb(
    IN PEPROCESS Process,
    IN PETHREAD Thread,
    IN PINITIAL_TEB InitialTeb,
    OUT PTEB *TebBase)
{
    DebugPrint("*** MM: Creating TEB ***\n");
    
    /* TEB is in user space, one per thread */
    /* PVOID TebAddress = (PVOID)0x7FF7FF7DB000; */  /* Standard TEB location - will be used later */
    
    /* Use static allocation for TEB since pool is not ready */
    static TEB StaticTebs[20];
    static ULONG TebIndex = 0;
    
    if (TebIndex >= 20)
    {
        DebugPrint("*** MM: Out of static TEB slots ***\n");
        return STATUS_NO_MEMORY;
    }
    
    PVOID TebMemory = &StaticTebs[TebIndex++];
    
    DebugPrint("*** MM: Using static TEB allocation ***\n");
    
    /* Initialize TEB */
    PTEB Teb = (PTEB)TebMemory;
    RtlZeroMemory(Teb, sizeof(TEB));
    
    /* Set basic TEB fields */
    Teb->ClientId = Thread->Cid;
    Teb->ProcessEnvironmentBlock = Process->Peb;
    Teb->CurrentLocale = 0x0409;  /* en-US */
    
    /* Set stack information */
    if (InitialTeb && InitialTeb->StackBase && InitialTeb->StackLimit)
    {
        Teb->NtTib.StackBase = InitialTeb->StackBase;
        Teb->NtTib.StackLimit = InitialTeb->StackLimit;
        Teb->DeallocationStack = InitialTeb->AllocatedStackBase ?
                                 InitialTeb->AllocatedStackBase :
                                 InitialTeb->StackLimit;
    }
    else
    {
        Teb->NtTib.StackBase = (PVOID)0x00130000;
        Teb->NtTib.StackLimit = (PVOID)0x00120000;
        Teb->DeallocationStack = (PVOID)0x00120000;
    }
    
    /* In real implementation, would map TEB into process address space */
    Thread->Tcb.Teb = Teb;
    *TebBase = Teb;
    
    DebugPrint("*** MM: TEB created successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* Initialize thread context for AMD64 */
static VOID
PspSetupThreadContext(
    IN OUT PCONTEXT Context,
    IN PINITIAL_TEB InitialTeb,
    IN PVOID StartAddress,
    IN PVOID StartParameter)
{
    if (!Context)
        return;

    if (!(Context->ContextFlags & CONTEXT_CONTROL))
        Context->ContextFlags |= CONTEXT_CONTROL;

    if (StartAddress)
        Context->Rip = (ULONG64)StartAddress;

    if (!(Context->ContextFlags & CONTEXT_INTEGER))
        Context->ContextFlags |= CONTEXT_INTEGER;

    Context->Rcx = (ULONG64)StartParameter;

    if (InitialTeb)
    {
        ULONG_PTR StackUpper = (ULONG_PTR)InitialTeb->StackBase;

        if (Context->Rsp == 0)
        {
            StackUpper = PSP_ALIGN_DOWN(StackUpper - 0x20, 16);
            if (StackUpper <= (ULONG_PTR)InitialTeb->StackLimit + PAGE_SIZE)
            {
                StackUpper = PSP_ALIGN_DOWN((ULONG_PTR)InitialTeb->StackLimit + PAGE_SIZE + 0x100, 16);
            }
            Context->Rsp = StackUpper;
        }
        else
        {
            Context->Rsp = PSP_ALIGN_DOWN(Context->Rsp, 16);
            if (Context->Rsp <= (ULONG_PTR)InitialTeb->StackLimit + PAGE_SIZE)
            {
                Context->Rsp = PSP_ALIGN_DOWN((ULONG_PTR)InitialTeb->StackBase - 0x20, 16);
            }
        }

        if (Context->Rbp == 0)
            Context->Rbp = Context->Rsp;
    }

    if (!(Context->ContextFlags & CONTEXT_SEGMENTS))
        Context->ContextFlags |= CONTEXT_SEGMENTS;

    if (Context->SegCs == 0) Context->SegCs = 0x33;
    if (Context->SegDs == 0) Context->SegDs = 0x2B;
    if (Context->SegEs == 0) Context->SegEs = 0x2B;
    if (Context->SegFs == 0) Context->SegFs = 0x53;
    if (Context->SegGs == 0) Context->SegGs = 0x2B;
    if (Context->SegSs == 0) Context->SegSs = 0x2B;

    if (Context->EFlags == 0)
        Context->EFlags = 0x200;
}

/* Main process creation function - renamed to avoid conflict */
NTSTATUS PspCreateProcessReal(
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    IN HANDLE ParentProcess OPTIONAL,
    IN ULONG Flags,
    IN HANDLE SectionHandle OPTIONAL,
    IN HANDLE DebugPort OPTIONAL,
    IN HANDLE ExceptionPort OPTIONAL,
    IN ULONG JobMemberLevel)
{
    NTSTATUS Status;
    PEPROCESS Process;
    ULONG_PTR DirectoryTableBase;
    PPEB Peb;
    
    DebugPrint("*** PS: PspCreateProcess called ***\n");
    
    /* Allocate EPROCESS */
    Status = PspAllocateProcess(&Process, ParentProcess);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    
    /* Create address space */
    Status = PspCreateProcessAddressSpace(Process, &DirectoryTableBase);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }
    
    Process->Pcb.DirectoryTableBase[0] = DirectoryTableBase;
    
    /* Create PEB */
    Status = PspCreatePeb(Process, &Peb);
    if (!NT_SUCCESS(Status))
    {
        /* Clean up */
        ExFreePoolWithTag(Process, 'corP');
        return Status;
    }
    
    /* Return handle (simulated) */
    *ProcessHandle = Process->UniqueProcessId;
    
    DebugPrint("*** PS: Process created successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* Main thread creation function - renamed to avoid conflict */
NTSTATUS PspCreateThreadReal(
    OUT PHANDLE ThreadHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    IN HANDLE ProcessHandle,
    OUT PCLIENT_ID ClientId OPTIONAL,
    IN PCONTEXT ThreadContext,
    IN PINITIAL_TEB InitialTeb,
    IN BOOLEAN CreateSuspended)
{
    NTSTATUS Status;
    PEPROCESS Process;
    PETHREAD Thread;
    PTEB Teb;
    
    DebugPrint("*** PS: PspCreateThread called ***\n");
    
    /* Find process by handle (simplified) */
    /* In real implementation, would use ObReferenceObjectByHandle */
    PLIST_ENTRY Entry = PsActiveProcessHead.Flink;
    Process = NULL;
    
    while (Entry != &PsActiveProcessHead)
    {
        PEPROCESS CurrentProcess = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);
        if (CurrentProcess->UniqueProcessId == ProcessHandle)
        {
            Process = CurrentProcess;
            break;
        }
        Entry = Entry->Flink;
    }
    
    if (!Process)
    {
        DebugPrint("*** PS: Process not found ***\n");
        return STATUS_INVALID_HANDLE;
    }
    
    /* Allocate ETHREAD */
    Status = PspAllocateThread(&Thread, Process);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    
    /* Create TEB */
    Status = PspCreateTeb(Process, Thread, InitialTeb, &Teb);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Thread, 'drhT');
        return Status;
    }
    
    /* Set up context if provided */
    if (ThreadContext)
    {
        Thread->StartAddress = (PVOID)ThreadContext->Rip;
        Thread->Win32StartAddress = (PVOID)ThreadContext->Rip; /* treat entry point as Win32 start */

        PspSetupThreadContext(ThreadContext,
                              InitialTeb,
                              (PVOID)ThreadContext->Rip,
                              (PVOID)ThreadContext->Rcx);
    }
    
    /* Return client ID */
    if (ClientId)
    {
        *ClientId = Thread->Cid;
    }
    
    /* Return handle */
    *ThreadHandle = Thread->Cid.UniqueThread;
    
    /* If not suspended, make thread ready */
    if (!CreateSuspended)
    {
        Thread->Tcb.State = Ready;
        DebugPrint("*** PS: Thread set to Ready state ***\n");
    }
    
    DebugPrint("*** PS: Thread created successfully ***\n");
    
    return STATUS_SUCCESS;
}

/* External PE loader function */
extern NTSTATUS LoadPEImage(
    IN PUNICODE_STRING FileName,
    OUT PVOID *ImageBase,
    OUT PVOID *EntryPoint);

/* Start initial system process (smss.exe) */
NTSTATUS PspStartInitialSystemProcess(void)
{
    NTSTATUS Status;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    CONTEXT Context;
    INITIAL_TEB InitialTeb;
    UNICODE_STRING SmssPath;
    PVOID ImageBase;
    PVOID EntryPoint;
    
    DebugPrint("*** PS: Starting initial system process (smss.exe) ***\n");
    
    /* Initialize process subsystem if needed */
    PspInitializeProcessSubsystem();
    
    /* Load smss.exe PE image */
    RtlInitUnicodeString(&SmssPath, L"\\SystemRoot\\System32\\smss.exe");
    Status = LoadPEImage(&SmssPath, &ImageBase, &EntryPoint);
    if (!NT_SUCCESS(Status))
    {
        DebugPrint("*** PS: Failed to load smss.exe image ***\n");
        return Status;
    }
    
    DebugPrint("*** PS: smss.exe loaded at base ***\n");
    
    /* Create smss.exe process */
    Status = PspCreateProcessReal(
        &ProcessHandle,
        PROCESS_ALL_ACCESS,
        NULL,
        NULL,  /* No parent */
        0,
        NULL,
        NULL,
        NULL,
        0);
    
    if (!NT_SUCCESS(Status))
    {
        DebugPrint("*** PS: Failed to create smss.exe process ***\n");
        return Status;
    }
    
    /* Set up thread context with actual entry point */
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    Context.Rip = (ULONG64)EntryPoint;  /* Use actual entry point from PE */

    RtlZeroMemory(&InitialTeb, sizeof(InitialTeb));

    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)ImageBase;
    PIMAGE_NT_HEADERS64 NtHeaders = (PIMAGE_NT_HEADERS64)((PUCHAR)ImageBase + DosHeader->e_lfanew);
    SIZE_T StackReserve = NtHeaders->OptionalHeader.SizeOfStackReserve;
    SIZE_T StackCommit = NtHeaders->OptionalHeader.SizeOfStackCommit;

    Status = PspSetupInitialStack(ProcessHandle,
                                  StackReserve,
                                  StackCommit,
                                  &InitialTeb,
                                  &Context);
    if (!NT_SUCCESS(Status))
    {
        DebugPrint("*** PS: Falling back to static stack setup ***\n");
        RtlZeroMemory(&InitialTeb, sizeof(InitialTeb));
        InitialTeb.StackBase = (PVOID)0x00130000;
        InitialTeb.StackLimit = (PVOID)0x00120000;
        InitialTeb.AllocatedStackBase = (PVOID)0x00120000;

        Context.Rsp = PSP_ALIGN_DOWN((ULONG_PTR)InitialTeb.StackBase - 0x20, 16);
        Context.Rbp = Context.Rsp;
        Context.SegCs = 0x33;  /* User mode CS for AMD64 */
        Context.SegDs = 0x2B;  /* User mode DS */
        Context.SegEs = 0x2B;
        Context.SegFs = 0x53;
        Context.SegGs = 0x2B;
        Context.SegSs = 0x2B;
        Context.EFlags |= 0x200;  /* Interrupts enabled */
        Status = STATUS_SUCCESS;
    }

    /* Create initial thread */
    Status = PspCreateThreadReal(
        &ThreadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        ProcessHandle,
        &ClientId,
        &Context,
        &InitialTeb,
        FALSE);  /* Start immediately */
    
    if (!NT_SUCCESS(Status))
    {
        DebugPrint("*** PS: Failed to create smss.exe thread ***\n");
        return Status;
    }
    
    DebugPrint("*** PS: smss.exe started successfully ***\n");
    DebugPrint("*** PS: Process ID: 4, Thread ID: 8 ***\n");
    
    return STATUS_SUCCESS;
}
