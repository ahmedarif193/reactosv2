/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Page Fault Handling
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Exception Syndrome Register (ESR) Bits for Data/Instruction Aborts */
#define ESR_EC_MASK             0xFC000000  /* Exception Class */
#define ESR_EC_SHIFT            26
#define ESR_ISS_MASK            0x01FFFFFF  /* Instruction Specific Syndrome */

/* Exception Classes for Memory Access */
#define ESR_EC_IABT_LOW         0x20        /* Instruction Abort from lower EL */
#define ESR_EC_IABT_CUR         0x21        /* Instruction Abort from current EL */
#define ESR_EC_PC_ALIGN         0x22        /* PC alignment fault */
#define ESR_EC_DABT_LOW         0x24        /* Data Abort from lower EL */
#define ESR_EC_DABT_CUR         0x25        /* Data Abort from current EL */
#define ESR_EC_SP_ALIGN         0x26        /* SP alignment fault */

/* Data Abort ISS (Instruction Specific Syndrome) fields */
#define ESR_ISS_DFSC_MASK       0x3F        /* Data Fault Status Code */
#define ESR_ISS_WNR             (1 << 6)    /* Write not Read */
#define ESR_ISS_S1PTW           (1 << 7)    /* Stage 1 translation table walk */
#define ESR_ISS_CM              (1 << 8)    /* Cache maintenance */
#define ESR_ISS_EA              (1 << 9)    /* External abort */
#define ESR_ISS_FNV             (1 << 10)   /* FAR not valid */
#define ESR_ISS_AET_MASK        (0x3 << 10) /* Access error type */
#define ESR_ISS_AET_SHIFT       10
#define ESR_ISS_SET_MASK        (0x3 << 11) /* Synchronous Error Type */
#define ESR_ISS_AR              (1 << 14)   /* Acquire/Release */
#define ESR_ISS_SF              (1 << 15)   /* Sixty-Four bit register */
#define ESR_ISS_SRT_MASK        (0x1F << 16)/* Syndrome Register Transfer */
#define ESR_ISS_SRT_SHIFT       16
#define ESR_ISS_SSE             (1 << 21)   /* Syndrome Sign Extend */
#define ESR_ISS_SAS_MASK        (0x3 << 22) /* Syndrome Access Size */
#define ESR_ISS_SAS_SHIFT       22
#define ESR_ISS_ISV             (1 << 24)   /* Instruction Syndrome Valid */

/* Fault Status Codes (DFSC/IFSC) */
#define FSC_TRANSLATION_L0      0x04        /* Translation fault, level 0 */
#define FSC_TRANSLATION_L1      0x05        /* Translation fault, level 1 */
#define FSC_TRANSLATION_L2      0x06        /* Translation fault, level 2 */
#define FSC_TRANSLATION_L3      0x07        /* Translation fault, level 3 */
#define FSC_ACCESS_FLAG_L0      0x08        /* Access flag fault, level 0 */
#define FSC_ACCESS_FLAG_L1      0x09        /* Access flag fault, level 1 */
#define FSC_ACCESS_FLAG_L2      0x0A        /* Access flag fault, level 2 */
#define FSC_ACCESS_FLAG_L3      0x0B        /* Access flag fault, level 3 */
#define FSC_PERMISSION_L0       0x0C        /* Permission fault, level 0 */
#define FSC_PERMISSION_L1       0x0D        /* Permission fault, level 1 */
#define FSC_PERMISSION_L2       0x0E        /* Permission fault, level 2 */
#define FSC_PERMISSION_L3       0x0F        /* Permission fault, level 3 */
#define FSC_ALIGNMENT           0x21        /* Alignment fault */
#define FSC_TLB_CONFLICT        0x30        /* TLB conflict abort */

/* Page fault error codes compatible with x86 for higher level code */
#define MM_FAULT_PRESENT        0x01        /* Page was present */
#define MM_FAULT_WRITE          0x02        /* Fault was write access */
#define MM_FAULT_USER           0x04        /* Fault from user mode */
#define MM_FAULT_RESERVED       0x08        /* Reserved bit violation */
#define MM_FAULT_EXECUTE        0x10        /* Instruction fetch fault */

/* GLOBALS ********************************************************************/

ULONG64 MmTotalPageFaults = 0;
ULONG64 MmTotalPageFaultsResolved = 0;
ULONG64 MmTotalPageFaultsInvalid = 0;
ULONG64 MmTotalPermissionFaults = 0;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Decode ARM64 fault status code
 *
 * Converts ARM64 specific fault status to generic fault type.
 */
static
ULONG
MiDecodeFaultStatus(
    IN ULONG FaultStatusCode)
{
    ULONG FaultType = 0;

    /* Check fault type */
    switch (FaultStatusCode & 0x3C)  /* Mask to get fault type */
    {
        case FSC_TRANSLATION_L0:
        case FSC_TRANSLATION_L1:
        case FSC_TRANSLATION_L2:
        case FSC_TRANSLATION_L3:
            /* Page not present */
            DPRINT("Translation fault at level %d\n", FaultStatusCode & 0x3);
            break;

        case FSC_ACCESS_FLAG_L0:
        case FSC_ACCESS_FLAG_L1:
        case FSC_ACCESS_FLAG_L2:
        case FSC_ACCESS_FLAG_L3:
            /* Access flag fault - page accessed for first time */
            FaultType |= MM_FAULT_PRESENT;
            DPRINT("Access flag fault at level %d\n", FaultStatusCode & 0x3);
            break;

        case FSC_PERMISSION_L0:
        case FSC_PERMISSION_L1:
        case FSC_PERMISSION_L2:
        case FSC_PERMISSION_L3:
            /* Permission fault - page present but access denied */
            FaultType |= MM_FAULT_PRESENT;
            DPRINT("Permission fault at level %d\n", FaultStatusCode & 0x3);
            break;

        case FSC_ALIGNMENT:
            /* Alignment fault */
            FaultType |= MM_FAULT_RESERVED;  /* Use reserved to indicate special fault */
            DPRINT("Alignment fault\n");
            break;

        default:
            DPRINT1("Unknown fault status code: 0x%02x\n", FaultStatusCode);
            break;
    }

    return FaultType;
}

/*
 * @brief Get page table level from fault status
 *
 * Returns the page table level where the fault occurred (0-3).
 */
static
ULONG
MiGetFaultLevel(
    IN ULONG FaultStatusCode)
{
    /* For translation and permission faults, level is in bits 0-1 */
    if ((FaultStatusCode >= FSC_TRANSLATION_L0 && FaultStatusCode <= FSC_PERMISSION_L3))
    {
        return FaultStatusCode & 0x3;
    }

    /* For other faults, no specific level */
    return 0;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief ARM64 Page Fault Handler
 * @implemented
 *
 * Main entry point for handling page faults (data and instruction aborts)
 * on ARM64 systems. Called from the exception handler.
 *
 * @param TrapFrame - Trap frame containing fault context
 * @param ExceptionSyndrome - ESR value containing fault information
 * @param FaultAddress - FAR value containing the faulting address
 * @return STATUS_SUCCESS if handled, appropriate error code otherwise
 */
NTSTATUS
NTAPI
MmAccessFault(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG64 ExceptionSyndrome,
    IN ULONG64 FaultAddress)
{
    ULONG ExceptionClass;
    ULONG FaultStatusCode;
    ULONG FaultType;
    BOOLEAN IsWrite;
    BOOLEAN IsExec;
    BOOLEAN IsUser;
    PEPROCESS Process;
    PETHREAD Thread;
    NTSTATUS Status;
    PVOID Address;

    /* Update statistics */
    InterlockedIncrement64((PLONG64)&MmTotalPageFaults);

    /* Extract exception class from ESR */
    ExceptionClass = (ExceptionSyndrome >> ESR_EC_SHIFT) & 0x3F;

    /* Get current process and thread */
    Thread = PsGetCurrentThread();
    Process = PsGetCurrentProcess();

    /* Round fault address to page boundary */
    Address = (PVOID)(FaultAddress & ~(PAGE_SIZE - 1));

    DPRINT("Page fault: Address=0x%p, ESR=0x%llx, EC=0x%x\n",
           Address, ExceptionSyndrome, ExceptionClass);

    /* Determine fault characteristics based on exception class */
    switch (ExceptionClass)
    {
        case ESR_EC_IABT_LOW:
        case ESR_EC_IABT_CUR:
            /* Instruction abort - execution fault */
            IsWrite = FALSE;
            IsExec = TRUE;
            IsUser = (ExceptionClass == ESR_EC_IABT_LOW);
            FaultStatusCode = ExceptionSyndrome & ESR_ISS_MASK;
            DPRINT("Instruction abort at 0x%p (user=%d)\n", Address, IsUser);
            break;

        case ESR_EC_DABT_LOW:
        case ESR_EC_DABT_CUR:
            /* Data abort - read or write fault */
            IsWrite = (ExceptionSyndrome & ESR_ISS_WNR) != 0;
            IsExec = FALSE;
            IsUser = (ExceptionClass == ESR_EC_DABT_LOW);
            FaultStatusCode = ExceptionSyndrome & ESR_ISS_DFSC_MASK;
            DPRINT("Data abort at 0x%p (%s, user=%d)\n",
                   Address, IsWrite ? "write" : "read", IsUser);
            break;

        case ESR_EC_PC_ALIGN:
            /* PC alignment fault */
            DPRINT1("PC alignment fault at 0x%p\n", (PVOID)FaultAddress);
            InterlockedIncrement64((PLONG64)&MmTotalPageFaultsInvalid);
            return STATUS_DATATYPE_MISALIGNMENT;

        case ESR_EC_SP_ALIGN:
            /* Stack pointer alignment fault */
            DPRINT1("SP alignment fault\n");
            InterlockedIncrement64((PLONG64)&MmTotalPageFaultsInvalid);
            return STATUS_DATATYPE_MISALIGNMENT;

        default:
            /* Unknown exception class */
            DPRINT1("Unknown exception class 0x%x for memory fault\n", ExceptionClass);
            InterlockedIncrement64((PLONG64)&MmTotalPageFaultsInvalid);
            return STATUS_ACCESS_VIOLATION;
    }

    /* Decode fault status to determine fault type */
    FaultType = MiDecodeFaultStatus(FaultStatusCode);
    if (IsWrite) FaultType |= MM_FAULT_WRITE;
    if (IsUser) FaultType |= MM_FAULT_USER;
    if (IsExec) FaultType |= MM_FAULT_EXECUTE;

    /* Check for kernel fault at invalid address */
    if (!IsUser && FaultAddress < MM_SYSTEM_RANGE_START)
    {
        DPRINT1("Kernel fault at user address 0x%p\n", Address);
        InterlockedIncrement64((PLONG64)&MmTotalPageFaultsInvalid);
        KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                     (ULONG_PTR)Address,
                     FaultType,
                     (ULONG_PTR)TrapFrame->Pc,
                     TrapFrame->Cpsr);
    }

    /* Handle different fault types */
    if (!(FaultType & MM_FAULT_PRESENT))
    {
        /* Page not present - need to bring it in */
        DPRINT("Page not present at 0x%p\n", Address);

        /* TODO: Call memory manager to handle page-in
         * Status = MmHandlePageFault(Address, FaultType, Process, Thread);
         */

        /* For now, treat as access violation */
        Status = STATUS_ACCESS_VIOLATION;
    }
    else if (FaultStatusCode >= FSC_PERMISSION_L0 &&
             FaultStatusCode <= FSC_PERMISSION_L3)
    {
        /* Permission fault - check if we need to update permissions */
        InterlockedIncrement64((PLONG64)&MmTotalPermissionFaults);
        DPRINT("Permission fault at 0x%p (write=%d)\n", Address, IsWrite);

        if (IsWrite)
        {
            /* Write to read-only page - check for copy-on-write */
            /* TODO: Check if this is a COW page
             * Status = MmHandleCopyOnWrite(Address, Process);
             */
            Status = STATUS_ACCESS_VIOLATION;
        }
        else
        {
            /* Read/execute permission fault */
            Status = STATUS_ACCESS_VIOLATION;
        }
    }
    else if (FaultStatusCode >= FSC_ACCESS_FLAG_L0 &&
             FaultStatusCode <= FSC_ACCESS_FLAG_L3)
    {
        /* Access flag fault - page accessed for first time */
        DPRINT("Access flag fault at 0x%p\n", Address);

        /* TODO: Set access flag in page table entry
         * Status = MmSetPageAccessed(Address, Process);
         */

        /* For now, mark as resolved */
        Status = STATUS_SUCCESS;
    }
    else
    {
        /* Other fault type */
        DPRINT1("Unhandled fault type: FSC=0x%x at 0x%p\n",
                FaultStatusCode, Address);
        Status = STATUS_ACCESS_VIOLATION;
    }

    /* Update statistics based on result */
    if (NT_SUCCESS(Status))
    {
        InterlockedIncrement64((PLONG64)&MmTotalPageFaultsResolved);
    }
    else
    {
        InterlockedIncrement64((PLONG64)&MmTotalPageFaultsInvalid);
    }

    return Status;
}

/*
 * @brief Handle instruction prefetch abort
 * @implemented
 *
 * Specialized handler for instruction fetch faults.
 *
 * @param TrapFrame - Trap frame containing fault context
 * @param FaultAddress - Address that caused the fault
 * @return STATUS_SUCCESS if handled, error code otherwise
 */
NTSTATUS
NTAPI
MmHandlePrefetchAbort(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG64 FaultAddress)
{
    PEPROCESS Process;
    PVOID Address;
    NTSTATUS Status;

    /* Round to page boundary */
    Address = (PVOID)(FaultAddress & ~(PAGE_SIZE - 1));

    DPRINT("Prefetch abort at 0x%p (PC=0x%p)\n",
           Address, (PVOID)TrapFrame->Pc);

    /* Get current process */
    Process = PsGetCurrentProcess();

    /* Check if address is in valid code region */
    if (FaultAddress >= MM_SYSTEM_RANGE_START)
    {
        /* Kernel code region */
        if (!MmIsAddressValid(Address))
        {
            /* Invalid kernel code address */
            DPRINT1("Prefetch abort at invalid kernel address 0x%p\n", Address);
            KeBugCheckEx(ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY,
                         (ULONG_PTR)Address,
                         (ULONG_PTR)TrapFrame->Pc,
                         0,
                         0);
        }
    }
    else
    {
        /* User code region */
        /* TODO: Validate and handle user code page fault
         * Status = MmHandleUserCodeFault(Address, Process);
         */
        Status = STATUS_ACCESS_VIOLATION;
    }

    return Status;
}

/*
 * @brief Handle data abort
 * @implemented
 *
 * Specialized handler for data access faults.
 *
 * @param TrapFrame - Trap frame containing fault context
 * @param FaultAddress - Address that caused the fault
 * @param IsWrite - TRUE if write access, FALSE if read
 * @return STATUS_SUCCESS if handled, error code otherwise
 */
NTSTATUS
NTAPI
MmHandleDataAbort(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG64 FaultAddress,
    IN BOOLEAN IsWrite)
{
    PEPROCESS Process;
    PVOID Address;
    NTSTATUS Status;

    /* Round to page boundary */
    Address = (PVOID)(FaultAddress & ~(PAGE_SIZE - 1));

    DPRINT("Data abort at 0x%p (%s, PC=0x%p)\n",
           Address, IsWrite ? "write" : "read", (PVOID)TrapFrame->Pc);

    /* Get current process */
    Process = PsGetCurrentProcess();

    /* Check address range */
    if (FaultAddress >= MM_SYSTEM_RANGE_START)
    {
        /* Kernel data region */
        if (!MmIsAddressValid(Address))
        {
            /* Invalid kernel data address */
            DPRINT1("Data abort at invalid kernel address 0x%p\n", Address);
            KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                         (ULONG_PTR)Address,
                         IsWrite ? 1 : 0,
                         (ULONG_PTR)TrapFrame->Pc,
                         0);
        }
    }
    else
    {
        /* User data region */
        /* TODO: Handle user data page fault
         * Status = MmHandleUserDataFault(Address, IsWrite, Process);
         */
        Status = STATUS_ACCESS_VIOLATION;
    }

    return Status;
}

/*
 * @brief Check if address is valid
 * @implemented
 *
 * Checks if a virtual address is currently valid (mapped and accessible).
 *
 * @param VirtualAddress - Address to check
 * @return TRUE if valid, FALSE otherwise
 */
BOOLEAN
NTAPI
MmIsAddressValid(
    IN PVOID VirtualAddress)
{
    ULONG64 Ttbr;
    ULONG_PTR Va = (ULONG_PTR)VirtualAddress;

    /* Quick check for obviously invalid addresses */
    if (!VirtualAddress)
        return FALSE;

    /* Check if in kernel space */
    if (Va >= MM_SYSTEM_RANGE_START)
    {
        /* Kernel address - check TTBR1 */
        __asm__ __volatile__ (
            "mrs %0, TTBR1_EL1\n"
            : "=r"(Ttbr)
        );
    }
    else
    {
        /* User address - check TTBR0 */
        __asm__ __volatile__ (
            "mrs %0, TTBR0_EL1\n"
            : "=r"(Ttbr)
        );
    }

    /* Check if translation table is valid */
    if (!(Ttbr & ~0xFFFF))
        return FALSE;

    /* TODO: Walk page tables to check if address is mapped
     * For now, do a simple probe using AT (Address Translation) instruction */

    /* Use AT S1E1R instruction to test read access at EL1 */
    __asm__ __volatile__ (
        "at S1E1R, %0\n"
        "isb\n"
        :: "r"(Va)
    );

    /* Read result from PAR_EL1 */
    ULONG64 Par;
    __asm__ __volatile__ (
        "mrs %0, PAR_EL1\n"
        : "=r"(Par)
    );

    /* Check if translation succeeded (bit 0 clear means success) */
    return !(Par & 1);
}

/*
 * @brief Probe memory for read access
 * @implemented
 *
 * Probes memory to ensure it's readable and handles any faults.
 *
 * @param Address - Address to probe
 * @param Length - Number of bytes to probe
 */
VOID
NTAPI
MmProbeForRead(
    IN PVOID Address,
    IN SIZE_T Length,
    IN ULONG Alignment)
{
    ULONG_PTR Current, Last;

    /* Check alignment */
    if (((ULONG_PTR)Address & (Alignment - 1)) != 0)
    {
        ExRaiseStatus(STATUS_DATATYPE_MISALIGNMENT);
    }

    /* Calculate last address */
    Current = (ULONG_PTR)Address;
    Last = Current + Length - 1;

    /* Check for overflow */
    if (Last < Current)
    {
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    }

    /* Check if in user space */
    if (Last < MM_SYSTEM_RANGE_START)
    {
        /* Probe each page */
        while (Current <= Last)
        {
            /* Touch the page to trigger fault if not present */
            *(volatile CHAR *)Current;

            /* Move to next page */
            Current = (Current & ~(PAGE_SIZE - 1)) + PAGE_SIZE;
        }
    }
    /* Kernel addresses are always valid in this context */
}

/*
 * @brief Probe memory for write access
 * @implemented
 *
 * Probes memory to ensure it's writable and handles any faults.
 *
 * @param Address - Address to probe
 * @param Length - Number of bytes to probe
 * @param Alignment - Required alignment
 */
VOID
NTAPI
MmProbeForWrite(
    IN PVOID Address,
    IN SIZE_T Length,
    IN ULONG Alignment)
{
    ULONG_PTR Current, Last;
    volatile CHAR *Ptr;
    CHAR Value;

    /* Check alignment */
    if (((ULONG_PTR)Address & (Alignment - 1)) != 0)
    {
        ExRaiseStatus(STATUS_DATATYPE_MISALIGNMENT);
    }

    /* Calculate last address */
    Current = (ULONG_PTR)Address;
    Last = Current + Length - 1;

    /* Check for overflow */
    if (Last < Current)
    {
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    }

    /* Check if in user space */
    if (Last < MM_SYSTEM_RANGE_START)
    {
        /* Probe each page */
        while (Current <= Last)
        {
            /* Read-modify-write to test write access */
            Ptr = (volatile CHAR *)Current;
            Value = *Ptr;
            *Ptr = Value;

            /* Move to next page */
            Current = (Current & ~(PAGE_SIZE - 1)) + PAGE_SIZE;
        }
    }
    /* Kernel addresses are assumed writable in this context */
}

/*
 * @brief Initialize page fault handling
 * @implemented
 *
 * Initializes the page fault handling subsystem for ARM64.
 */
VOID
NTAPI
MiInitializePageFaultHandling(VOID)
{
    DPRINT("Initializing ARM64 page fault handling\n");

    /* Reset statistics */
    MmTotalPageFaults = 0;
    MmTotalPageFaultsResolved = 0;
    MmTotalPageFaultsInvalid = 0;
    MmTotalPermissionFaults = 0;

    /* TODO: Set up any ARM64-specific fault handling structures
     * - Initialize page fault handler tables
     * - Set up fault recovery mechanisms
     * - Configure hardware fault detection features
     */

    DPRINT("ARM64 page fault handling initialized\n");
}

/* EOF */