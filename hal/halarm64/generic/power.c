/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 PSCI Power Management Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* DEFINES ********************************************************************/

/* PSCI Function IDs (ARM DEN0022D) */
#define PSCI_VERSION                    0x84000000UL
#define PSCI_CPU_SUSPEND_AARCH64        0xC4000001UL
#define PSCI_CPU_OFF                    0x84000002UL
#define PSCI_CPU_ON_AARCH64             0xC4000003UL
#define PSCI_AFFINITY_INFO_AARCH64      0xC4000004UL
#define PSCI_MIGRATE_AARCH64            0xC4000005UL
#define PSCI_MIGRATE_INFO_TYPE          0x84000006UL
#define PSCI_MIGRATE_INFO_UP_CPU_AARCH64 0xC4000007UL
#define PSCI_SYSTEM_OFF                 0x84000008UL
#define PSCI_SYSTEM_RESET               0x84000009UL
#define PSCI_PSCI_FEATURES              0x8400000AUL
#define PSCI_CPU_FREEZE                 0x8400000BUL
#define PSCI_CPU_DEFAULT_SUSPEND_AARCH64 0xC400000CUL
#define PSCI_NODE_HW_STATE_AARCH64      0xC400000DUL
#define PSCI_SYSTEM_SUSPEND_AARCH64     0xC400000EUL
#define PSCI_PSCI_SET_SUSPEND_MODE      0x8400000FUL
#define PSCI_PSCI_STAT_RESIDENCY_AARCH64 0xC4000010UL
#define PSCI_PSCI_STAT_COUNT_AARCH64    0xC4000011UL
#define PSCI_SYSTEM_RESET2_AARCH64      0xC4000012UL
#define PSCI_MEM_PROTECT                0x84000013UL
#define PSCI_MEM_CHK_RANGE_AARCH64      0xC4000014UL

/* PSCI Return Codes */
#define PSCI_RET_SUCCESS                0
#define PSCI_RET_NOT_SUPPORTED          -1
#define PSCI_RET_INVALID_PARAMS         -2
#define PSCI_RET_DENIED                 -3
#define PSCI_RET_ALREADY_ON             -4
#define PSCI_RET_ON_PENDING             -5
#define PSCI_RET_INTERNAL_FAILURE       -6
#define PSCI_RET_NOT_PRESENT            -7
#define PSCI_RET_DISABLED               -8
#define PSCI_RET_INVALID_ADDRESS        -9

/* PSCI Power States */
#define PSCI_POWER_STATE_TYPE_STANDBY   0x0
#define PSCI_POWER_STATE_TYPE_POWERDOWN 0x1

/* PSCI Affinity Info States */
#define PSCI_AFFINITY_LEVEL_0           0
#define PSCI_AFFINITY_LEVEL_1           1
#define PSCI_AFFINITY_LEVEL_2           2
#define PSCI_AFFINITY_LEVEL_3           3

#define PSCI_AFFINITY_INFO_ON           0
#define PSCI_AFFINITY_INFO_OFF          1
#define PSCI_AFFINITY_INFO_ON_PENDING   2

/* PSCI Invocation Methods */
#define PSCI_METHOD_UNKNOWN             0
#define PSCI_METHOD_SMC                 1
#define PSCI_METHOD_HVC                 2

/* PSCI Version */
#define PSCI_VERSION_MAJOR(x)           (((x) >> 16) & 0x7FFF)
#define PSCI_VERSION_MINOR(x)           ((x) & 0xFFFF)

/* ARM64 CPU Power States */
#define ARM64_CPU_STATE_ON              0
#define ARM64_CPU_STATE_OFF             1
#define ARM64_CPU_STATE_SUSPENDED       2
#define ARM64_CPU_STATE_STANDBY         3

/* DATA STRUCTURES ************************************************************/

/* PSCI CPU Context */
typedef struct _ARM64_CPU_CONTEXT
{
    ULONG ProcessorNumber;
    ULONG64 Mpidr;              /* Multiprocessor Affinity Register */
    ULONG State;                /* Current power state */
    ULONG64 ContextId;          /* Context ID for suspend/resume */
    PHYSICAL_ADDRESS EntryPoint; /* Resume entry point */
    BOOLEAN Online;             /* CPU is online and active */
    BOOLEAN SuspendSupported;   /* CPU supports suspend */
    LIST_ENTRY ListEntry;
} ARM64_CPU_CONTEXT, *PARM64_CPU_CONTEXT;

/* PSCI System Context */
typedef struct _ARM64_PSCI_CONTEXT
{
    ULONG Method;               /* Invocation method (SMC/HVC) */
    ULONG Version;              /* PSCI version */
    BOOLEAN Available;          /* PSCI is available */
    BOOLEAN CpuOnSupported;     /* CPU_ON is supported */
    BOOLEAN CpuOffSupported;    /* CPU_OFF is supported */
    BOOLEAN CpuSuspendSupported; /* CPU_SUSPEND is supported */
    BOOLEAN SystemOffSupported; /* SYSTEM_OFF is supported */
    BOOLEAN SystemResetSupported; /* SYSTEM_RESET is supported */
    BOOLEAN SystemSuspendSupported; /* SYSTEM_SUSPEND is supported */
    LIST_ENTRY CpuList;         /* List of CPU contexts */
    KSPIN_LOCK Lock;            /* Protection for CPU list */
} ARM64_PSCI_CONTEXT, *PARM64_PSCI_CONTEXT;

/* Power State Request */
typedef struct _ARM64_POWER_STATE
{
    union {
        struct {
            ULONG StateId : 16;     /* State ID */
            ULONG StateType : 1;    /* 0=Standby, 1=Powerdown */
            ULONG PowerLevel : 2;   /* Power level */
            ULONG Reserved : 13;
        } Fields;
        ULONG Raw;
    };
} ARM64_POWER_STATE, *PARM64_POWER_STATE;

/* GLOBALS ********************************************************************/

static ARM64_PSCI_CONTEXT HalPsciContext = {0};
static BOOLEAN HalPowerInitialized = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Invoke PSCI function via SMC
 */
STATIC
LONG64
HalpInvokePsciSmc(
    IN ULONG64 FunctionId,
    IN ULONG64 Arg0,
    IN ULONG64 Arg1,
    IN ULONG64 Arg2)
{
    LONG64 Result;

    __asm__ __volatile__ (
        "mov x0, %1\n"      /* Function ID */
        "mov x1, %2\n"      /* Argument 0 */
        "mov x2, %3\n"      /* Argument 1 */
        "mov x3, %4\n"      /* Argument 2 */
        "smc #0\n"          /* Secure Monitor Call */
        "mov %0, x0\n"      /* Return value */
        : "=r"(Result)
        : "r"(FunctionId), "r"(Arg0), "r"(Arg1), "r"(Arg2)
        : "x0", "x1", "x2", "x3"
    );

    return Result;
}

/*
 * @brief Invoke PSCI function via HVC
 */
STATIC
LONG64
HalpInvokePsciHvc(
    IN ULONG64 FunctionId,
    IN ULONG64 Arg0,
    IN ULONG64 Arg1,
    IN ULONG64 Arg2)
{
    LONG64 Result;

    __asm__ __volatile__ (
        "mov x0, %1\n"      /* Function ID */
        "mov x1, %2\n"      /* Argument 0 */
        "mov x2, %3\n"      /* Argument 1 */
        "mov x3, %4\n"      /* Argument 2 */
        "hvc #0\n"          /* Hypervisor Call */
        "mov %0, x0\n"      /* Return value */
        : "=r"(Result)
        : "r"(FunctionId), "r"(Arg0), "r"(Arg1), "r"(Arg2)
        : "x0", "x1", "x2", "x3"
    );

    return Result;
}

/*
 * @brief Invoke PSCI function
 */
STATIC
LONG64
HalpInvokePsci(
    IN ULONG64 FunctionId,
    IN ULONG64 Arg0,
    IN ULONG64 Arg1,
    IN ULONG64 Arg2)
{
    switch (HalPsciContext.Method)
    {
        case PSCI_METHOD_SMC:
            return HalpInvokePsciSmc(FunctionId, Arg0, Arg1, Arg2);

        case PSCI_METHOD_HVC:
            return HalpInvokePsciHvc(FunctionId, Arg0, Arg1, Arg2);

        default:
            DPRINT1("Unknown PSCI invocation method: %lu\n", HalPsciContext.Method);
            return PSCI_RET_NOT_SUPPORTED;
    }
}

/*
 * @brief Get current CPU's MPIDR
 */
STATIC
ULONG64
HalpGetCurrentMpidr(VOID)
{
    ULONG64 Mpidr;

    __asm__ __volatile__ ("mrs %0, mpidr_el1" : "=r"(Mpidr));

    /* Mask off reserved bits */
    return Mpidr & 0xFF00FFFFFFUL;
}

/*
 * @brief Find CPU context by MPIDR
 */
STATIC
PARM64_CPU_CONTEXT
HalpFindCpuContext(
    IN ULONG64 Mpidr)
{
    PLIST_ENTRY Entry;
    PARM64_CPU_CONTEXT CpuContext;

    Entry = HalPsciContext.CpuList.Flink;
    while (Entry != &HalPsciContext.CpuList)
    {
        CpuContext = CONTAINING_RECORD(Entry, ARM64_CPU_CONTEXT, ListEntry);
        if (CpuContext->Mpidr == Mpidr)
            return CpuContext;
        Entry = Entry->Flink;
    }

    return NULL;
}

/*
 * @brief Register CPU with PSCI subsystem
 */
STATIC
NTSTATUS
HalpRegisterCpu(
    IN ULONG ProcessorNumber,
    IN ULONG64 Mpidr)
{
    PARM64_CPU_CONTEXT CpuContext;
    KIRQL OldIrql;

    /* Check if CPU is already registered */
    KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
    CpuContext = HalpFindCpuContext(Mpidr);
    KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

    if (CpuContext)
    {
        DPRINT("CPU %lu (MPIDR=0x%I64x) already registered\n", ProcessorNumber, Mpidr);
        return STATUS_SUCCESS;
    }

    /* Allocate CPU context */
    CpuContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(ARM64_CPU_CONTEXT), 'UPSC');
    if (!CpuContext)
    {
        DPRINT1("Failed to allocate CPU context for processor %lu\n", ProcessorNumber);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize CPU context */
    RtlZeroMemory(CpuContext, sizeof(ARM64_CPU_CONTEXT));
    CpuContext->ProcessorNumber = ProcessorNumber;
    CpuContext->Mpidr = Mpidr;
    CpuContext->State = ARM64_CPU_STATE_ON;
    CpuContext->Online = TRUE;
    CpuContext->SuspendSupported = HalPsciContext.CpuSuspendSupported;

    /* Add to CPU list */
    KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
    InsertTailList(&HalPsciContext.CpuList, &CpuContext->ListEntry);
    KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

    DPRINT("Registered CPU %lu (MPIDR=0x%I64x) with PSCI\n", ProcessorNumber, Mpidr);
    return STATUS_SUCCESS;
}

/*
 * @brief Detect PSCI method from device tree or ACPI
 */
STATIC
ULONG
HalpDetectPsciMethod(VOID)
{
    /* TODO: Parse device tree or ACPI tables to determine PSCI method
     * For now, assume SMC is the most common method on ARM64 systems
     */
    return PSCI_METHOD_SMC;
}

/*
 * @brief Initialize PSCI subsystem
 */
STATIC
NTSTATUS
HalpInitializePsci(VOID)
{
    LONG64 Result;
    ULONG Version;

    DPRINT("Initializing PSCI subsystem\n");

    /* Initialize PSCI context */
    RtlZeroMemory(&HalPsciContext, sizeof(ARM64_PSCI_CONTEXT));
    InitializeListHead(&HalPsciContext.CpuList);
    KeInitializeSpinLock(&HalPsciContext.Lock);

    /* Detect PSCI invocation method */
    HalPsciContext.Method = HalpDetectPsciMethod();
    DPRINT("Using PSCI method: %s\n",
           (HalPsciContext.Method == PSCI_METHOD_SMC) ? "SMC" : "HVC");

    /* Get PSCI version */
    Result = HalpInvokePsci(PSCI_VERSION, 0, 0, 0);
    if (Result < 0)
    {
        DPRINT1("PSCI not available or version call failed: %I64d\n", Result);
        return STATUS_NOT_SUPPORTED;
    }

    Version = (ULONG)Result;
    HalPsciContext.Version = Version;
    HalPsciContext.Available = TRUE;

    DPRINT("PSCI version %lu.%lu detected\n",
           PSCI_VERSION_MAJOR(Version), PSCI_VERSION_MINOR(Version));

    /* Check for supported functions */
    if (Version >= 0x00010000)  /* PSCI v1.0+ supports PSCI_FEATURES */
    {
        /* Check CPU_ON support */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_CPU_ON_AARCH64, 0, 0);
        HalPsciContext.CpuOnSupported = (Result == PSCI_RET_SUCCESS);

        /* Check CPU_OFF support */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_CPU_OFF, 0, 0);
        HalPsciContext.CpuOffSupported = (Result == PSCI_RET_SUCCESS);

        /* Check CPU_SUSPEND support */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_CPU_SUSPEND_AARCH64, 0, 0);
        HalPsciContext.CpuSuspendSupported = (Result == PSCI_RET_SUCCESS);

        /* Check SYSTEM_OFF support */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_SYSTEM_OFF, 0, 0);
        HalPsciContext.SystemOffSupported = (Result == PSCI_RET_SUCCESS);

        /* Check SYSTEM_RESET support */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_SYSTEM_RESET, 0, 0);
        HalPsciContext.SystemResetSupported = (Result == PSCI_RET_SUCCESS);

        /* Check SYSTEM_SUSPEND support (PSCI v1.0+) */
        Result = HalpInvokePsci(PSCI_PSCI_FEATURES, PSCI_SYSTEM_SUSPEND_AARCH64, 0, 0);
        HalPsciContext.SystemSuspendSupported = (Result == PSCI_RET_SUCCESS);
    }
    else
    {
        /* For PSCI v0.2, assume basic functions are supported */
        HalPsciContext.CpuOnSupported = TRUE;
        HalPsciContext.CpuOffSupported = TRUE;
        HalPsciContext.SystemOffSupported = TRUE;
        HalPsciContext.SystemResetSupported = TRUE;
        HalPsciContext.CpuSuspendSupported = FALSE;  /* Not guaranteed in v0.2 */
        HalPsciContext.SystemSuspendSupported = FALSE;
    }

    DPRINT("PSCI functions: CPU_ON=%d, CPU_OFF=%d, CPU_SUSPEND=%d, SYSTEM_OFF=%d, SYSTEM_RESET=%d, SYSTEM_SUSPEND=%d\n",
           HalPsciContext.CpuOnSupported,
           HalPsciContext.CpuOffSupported,
           HalPsciContext.CpuSuspendSupported,
           HalPsciContext.SystemOffSupported,
           HalPsciContext.SystemResetSupported,
           HalPsciContext.SystemSuspendSupported);

    /* Register current CPU */
    ULONG64 CurrentMpidr = HalpGetCurrentMpidr();
    HalpRegisterCpu(KeGetCurrentProcessorNumber(), CurrentMpidr);

    DPRINT("PSCI subsystem initialized successfully\n");
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize power management subsystem
 */
NTSTATUS
NTAPI
HalInitializePowerManagement(VOID)
{
    NTSTATUS Status;

    if (HalPowerInitialized)
        return STATUS_SUCCESS;

    DPRINT("Initializing ARM64 power management\n");

    /* Initialize PSCI */
    Status = HalpInitializePsci();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize PSCI: 0x%x\n", Status);
        return Status;
    }

    HalPowerInitialized = TRUE;

    DPRINT("ARM64 power management initialized\n");
    return STATUS_SUCCESS;
}

/*
 * @brief Turn on secondary CPU
 */
NTSTATUS
NTAPI
HalStartProcessor(
    IN ULONG ProcessorNumber,
    IN ULONG64 Mpidr,
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId)
{
    LONG64 Result;

    if (!HalPsciContext.Available || !HalPsciContext.CpuOnSupported)
    {
        DPRINT1("PSCI CPU_ON not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    DPRINT("Starting processor %lu (MPIDR=0x%I64x) at entry point 0x%I64x\n",
           ProcessorNumber, Mpidr, EntryPoint.QuadPart);

    /* Register the CPU */
    HalpRegisterCpu(ProcessorNumber, Mpidr);

    /* Invoke PSCI CPU_ON */
    Result = HalpInvokePsci(PSCI_CPU_ON_AARCH64,
                           Mpidr,
                           EntryPoint.QuadPart,
                           ContextId);

    switch (Result)
    {
        case PSCI_RET_SUCCESS:
            DPRINT("CPU %lu started successfully\n", ProcessorNumber);
            return STATUS_SUCCESS;

        case PSCI_RET_ALREADY_ON:
            DPRINT("CPU %lu is already on\n", ProcessorNumber);
            return STATUS_SUCCESS;

        case PSCI_RET_ON_PENDING:
            DPRINT("CPU %lu start is pending\n", ProcessorNumber);
            return STATUS_PENDING;

        case PSCI_RET_INVALID_PARAMS:
            DPRINT1("Invalid parameters for CPU %lu start\n", ProcessorNumber);
            return STATUS_INVALID_PARAMETER;

        case PSCI_RET_INVALID_ADDRESS:
            DPRINT1("Invalid entry point address for CPU %lu\n", ProcessorNumber);
            return STATUS_INVALID_ADDRESS;

        case PSCI_RET_DENIED:
            DPRINT1("CPU %lu start denied\n", ProcessorNumber);
            return STATUS_ACCESS_DENIED;

        default:
            DPRINT1("CPU %lu start failed with error %I64d\n", ProcessorNumber, Result);
            return STATUS_UNSUCCESSFUL;
    }
}

/*
 * @brief Turn off current CPU
 */
VOID
NTAPI
HalStopProcessor(VOID)
{
    ULONG64 CurrentMpidr;
    PARM64_CPU_CONTEXT CpuContext;
    KIRQL OldIrql;

    if (!HalPsciContext.Available || !HalPsciContext.CpuOffSupported)
    {
        DPRINT1("PSCI CPU_OFF not supported\n");
        return;
    }

    CurrentMpidr = HalpGetCurrentMpidr();

    DPRINT("Stopping current CPU (MPIDR=0x%I64x)\n", CurrentMpidr);

    /* Update CPU context */
    KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
    CpuContext = HalpFindCpuContext(CurrentMpidr);
    if (CpuContext)
    {
        CpuContext->State = ARM64_CPU_STATE_OFF;
        CpuContext->Online = FALSE;
    }
    KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

    /* Invoke PSCI CPU_OFF - this call should not return */
    HalpInvokePsci(PSCI_CPU_OFF, 0, 0, 0);

    /* If we reach here, CPU_OFF failed */
    DPRINT1("PSCI CPU_OFF failed - CPU still running\n");
}

/*
 * @brief Suspend current CPU
 */
NTSTATUS
NTAPI
HalSuspendProcessor(
    IN ULONG PowerState,
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId)
{
    LONG64 Result;
    ULONG64 CurrentMpidr;
    PARM64_CPU_CONTEXT CpuContext;
    KIRQL OldIrql;

    if (!HalPsciContext.Available || !HalPsciContext.CpuSuspendSupported)
    {
        DPRINT1("PSCI CPU_SUSPEND not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    CurrentMpidr = HalpGetCurrentMpidr();

    DPRINT("Suspending current CPU (MPIDR=0x%I64x) with power state 0x%x\n",
           CurrentMpidr, PowerState);

    /* Update CPU context */
    KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
    CpuContext = HalpFindCpuContext(CurrentMpidr);
    if (CpuContext)
    {
        CpuContext->State = ARM64_CPU_STATE_SUSPENDED;
        CpuContext->EntryPoint = EntryPoint;
        CpuContext->ContextId = ContextId;
    }
    KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

    /* Invoke PSCI CPU_SUSPEND */
    Result = HalpInvokePsci(PSCI_CPU_SUSPEND_AARCH64,
                           PowerState,
                           EntryPoint.QuadPart,
                           ContextId);

    /* If we return here, CPU_SUSPEND completed or failed */
    switch (Result)
    {
        case PSCI_RET_SUCCESS:
            DPRINT("CPU suspended and resumed successfully\n");

            /* Update CPU context on resume */
            KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
            if (CpuContext)
            {
                CpuContext->State = ARM64_CPU_STATE_ON;
            }
            KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

            return STATUS_SUCCESS;

        case PSCI_RET_INVALID_PARAMS:
            DPRINT1("Invalid parameters for CPU suspend\n");
            return STATUS_INVALID_PARAMETER;

        case PSCI_RET_INVALID_ADDRESS:
            DPRINT1("Invalid entry point address for CPU suspend\n");
            return STATUS_INVALID_ADDRESS;

        case PSCI_RET_DENIED:
            DPRINT1("CPU suspend denied\n");
            return STATUS_ACCESS_DENIED;

        default:
            DPRINT1("CPU suspend failed with error %I64d\n", Result);
            return STATUS_UNSUCCESSFUL;
    }
}

/*
 * @brief Get CPU affinity information
 */
NTSTATUS
NTAPI
HalGetCpuAffinityInfo(
    IN ULONG64 Mpidr,
    IN ULONG AffinityLevel,
    OUT PULONG AffinityState)
{
    LONG64 Result;

    if (!HalPsciContext.Available)
    {
        DPRINT1("PSCI not available\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (!AffinityState)
        return STATUS_INVALID_PARAMETER;

    DPRINT("Getting affinity info for MPIDR=0x%I64x, level=%lu\n", Mpidr, AffinityLevel);

    /* Invoke PSCI AFFINITY_INFO */
    Result = HalpInvokePsci(PSCI_AFFINITY_INFO_AARCH64, Mpidr, AffinityLevel, 0);

    switch (Result)
    {
        case PSCI_AFFINITY_INFO_ON:
        case PSCI_AFFINITY_INFO_OFF:
        case PSCI_AFFINITY_INFO_ON_PENDING:
            *AffinityState = (ULONG)Result;
            return STATUS_SUCCESS;

        case PSCI_RET_INVALID_PARAMS:
            DPRINT1("Invalid parameters for affinity info\n");
            return STATUS_INVALID_PARAMETER;

        default:
            DPRINT1("Affinity info failed with error %I64d\n", Result);
            return STATUS_UNSUCCESSFUL;
    }
}

/*
 * @brief System shutdown via PSCI
 */
VOID
NTAPI
HalSystemShutdown(VOID)
{
    if (!HalPsciContext.Available || !HalPsciContext.SystemOffSupported)
    {
        DPRINT1("PSCI SYSTEM_OFF not supported\n");
        return;
    }

    DPRINT("Shutting down system via PSCI\n");

    /* Invoke PSCI SYSTEM_OFF - this call should not return */
    HalpInvokePsci(PSCI_SYSTEM_OFF, 0, 0, 0);

    /* If we reach here, SYSTEM_OFF failed */
    DPRINT1("PSCI SYSTEM_OFF failed - system still running\n");
}

/*
 * @brief System restart via PSCI
 */
VOID
NTAPI
HalSystemRestart(VOID)
{
    if (!HalPsciContext.Available || !HalPsciContext.SystemResetSupported)
    {
        DPRINT1("PSCI SYSTEM_RESET not supported\n");
        return;
    }

    DPRINT("Restarting system via PSCI\n");

    /* Invoke PSCI SYSTEM_RESET - this call should not return */
    HalpInvokePsci(PSCI_SYSTEM_RESET, 0, 0, 0);

    /* If we reach here, SYSTEM_RESET failed */
    DPRINT1("PSCI SYSTEM_RESET failed - system still running\n");
}

/*
 * @brief System suspend via PSCI (PSCI v1.0+)
 */
NTSTATUS
NTAPI
HalSystemSuspend(
    IN PHYSICAL_ADDRESS EntryPoint,
    IN ULONG64 ContextId)
{
    LONG64 Result;

    if (!HalPsciContext.Available || !HalPsciContext.SystemSuspendSupported)
    {
        DPRINT1("PSCI SYSTEM_SUSPEND not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    DPRINT("Suspending system via PSCI\n");

    /* Invoke PSCI SYSTEM_SUSPEND */
    Result = HalpInvokePsci(PSCI_SYSTEM_SUSPEND_AARCH64,
                           EntryPoint.QuadPart,
                           ContextId,
                           0);

    /* If we return here, system suspend completed or failed */
    switch (Result)
    {
        case PSCI_RET_SUCCESS:
            DPRINT("System suspended and resumed successfully\n");
            return STATUS_SUCCESS;

        case PSCI_RET_INVALID_ADDRESS:
            DPRINT1("Invalid entry point address for system suspend\n");
            return STATUS_INVALID_ADDRESS;

        case PSCI_RET_DENIED:
            DPRINT1("System suspend denied\n");
            return STATUS_ACCESS_DENIED;

        default:
            DPRINT1("System suspend failed with error %I64d\n", Result);
            return STATUS_UNSUCCESSFUL;
    }
}

/*
 * @brief Check if PSCI is available
 */
BOOLEAN
NTAPI
HalIsPsciAvailable(VOID)
{
    return HalPsciContext.Available;
}

/*
 * @brief Get PSCI version
 */
ULONG
NTAPI
HalGetPsciVersion(VOID)
{
    return HalPsciContext.Version;
}

/*
 * @brief Get CPU power state
 */
ULONG
NTAPI
HalGetCpuPowerState(
    IN ULONG64 Mpidr)
{
    PARM64_CPU_CONTEXT CpuContext;
    KIRQL OldIrql;
    ULONG State = ARM64_CPU_STATE_OFF;

    KeAcquireSpinLock(&HalPsciContext.Lock, &OldIrql);
    CpuContext = HalpFindCpuContext(Mpidr);
    if (CpuContext)
    {
        State = CpuContext->State;
    }
    KeReleaseSpinLock(&HalPsciContext.Lock, OldIrql);

    return State;
}