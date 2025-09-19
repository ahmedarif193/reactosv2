/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * PURPOSE:         ARM64 System Call Interface
 * FILE:            ntoskrnl/ke/arm64/syscall.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

/* System call table - defined in ke/procobj.c and ke/krnlinit.c */
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTable[SSDT_MAX_ENTRIES];
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTableShadow[SSDT_MAX_ENTRIES];

/* Maximum system call number - defined in ke/krnlinit.c */
extern ULONG KiServiceLimit;

/* ARM64 System Call Service Table */
PVOID KiServiceTable[] = {
    /* TODO: This should be auto-generated from ntoskrnl.spec */
    /* For now, provide minimal stubs to unblock the build */
    NULL, /* Entry 0 - placeholder */
    /* Add actual service functions as they are implemented */
};

/* FUNCTIONS *****************************************************************/

/*
 * @brief Initialize ARM64 system call interface
 *
 * ARM64 uses the SVC (Supervisor Call) instruction for system calls.
 * System call number is passed in X8, parameters in X0-X7.
 *
 * @return VOID
 */
VOID
NTAPI
KiInitializeSystemCall(VOID)
{
    DPRINT1("KiInitializeSystemCall: ARM64 syscall init stub\n");

    /* TODO: Set up exception vector table */
    /* VBAR_EL1: Vector Base Address Register
     * Points to 16-entry exception vector table:
     * - Synchronous exceptions (including SVC)
     * - IRQ
     * - FIQ
     * - SError
     * Each for: Current EL with SP0, Current EL with SPx,
     *           Lower EL using AArch64, Lower EL using AArch32
     */

    /* TODO: Install SVC handler in vector table */
    /* Synchronous exception from lower EL (EL0) at offset 0x400 */

    /* TODO: Set VBAR_EL1 to point to vector table */
    /* MSR VBAR_EL1, X0 */

    /* TODO: Initialize system call table */
    /* KeServiceDescriptorTable points to:
     * - Native system calls
     * - GUI system calls (win32k)
     * - IIS system calls
     * - Reserved
     */

    /* TODO: Set up fast system call mechanism if available */
    /* ARM64 doesn't have SYSCALL/SYSRET like x86-64,
     * but SVC is relatively fast with minimal state save
     */
}

/*
 * @brief System call dispatcher
 *
 * Called from SVC exception handler with user context saved.
 * System call number in X8, parameters in X0-X7.
 *
 * @param TrapFrame - Saved user context
 * @return VOID
 */
VOID
NTAPI
KiSystemService(
    IN PKTRAP_FRAME TrapFrame)
{
    ULONG SystemCallNumber;
    PULONG_PTR SystemCallTable;
    PVOID SystemCallFunction;
    ULONG NumberOfParameters;
    DPRINT1("KiSystemService: ARM64 syscall dispatcher stub\n");

    /* TODO: Get system call number from trap frame */
    /* System call number is in X8 register */
    SystemCallNumber = 0; /* TrapFrame->X8 */

    /* TODO: Validate system call number */
    if (SystemCallNumber >= KiServiceLimit)
    {
        DPRINT1("Invalid system call number: %u\n", SystemCallNumber);
        /* TODO: TrapFrame->X0 = STATUS_INVALID_SYSTEM_SERVICE; */
        return;
    }

    /* TODO: Get system call table */
    /* Bits 31:12 = table index
     * Bits 11:0 = service number
     */
    SystemCallTable = KeServiceDescriptorTable[0].Base;

    /* TODO: Get function pointer from table */
    SystemCallFunction = (PVOID)SystemCallTable[SystemCallNumber];

    /* TODO: Get parameter count (encoded in table) */
    UNREFERENCED_PARAMETER(NumberOfParameters); /* TODO: Get from service table */
    UNREFERENCED_PARAMETER(SystemCallFunction); /* TODO: Call the function */

    /* TODO: Copy parameters from registers/stack if needed */
    /* First 8 parameters in X0-X7
     * Additional parameters on user stack
     */

    /* TODO: Check if parameters need probing */
    /* User-mode pointers must be validated */

    /* TODO: Call the system service */
    /* ULONG64 ReturnValue = SystemCallFunction(...); */
    /* ReturnValue = STATUS_NOT_IMPLEMENTED; */

    /* TODO: Set return value in trap frame */
    /* TrapFrame->X0 = ReturnValue; */

    /* TODO: Check for pending APC delivery */
    /* If user APC pending, dispatch it before return */

    /* TODO: Check for thread termination */
    /* If thread is terminating, handle cleanup */
}

/*
 * @brief Handle SVC instruction from user mode
 *
 * SVC #0 is used for system calls in ARM64 Windows.
 *
 * @param TrapFrame - Saved context at SVC
 * @param SvcNumber - Immediate value from SVC instruction
 * @return VOID
 */
VOID
NTAPI
KiSvcHandler(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG SvcNumber)
{
    DPRINT1("KiSvcHandler: SVC #%u - ARM64 stub\n", SvcNumber);

    /* Check SVC immediate value */
    if (SvcNumber == 0)
    {
        /* System call */
        KiSystemService(TrapFrame);
    }
    else
    {
        /* Other SVC numbers may be used for:
         * - Debugging (breakpoints)
         * - Hypervisor calls
         * - Special kernel services
         */
        DPRINT1("Unhandled SVC number: %u\n", SvcNumber);
    }
}

/*
 * @brief Register a system call table
 *
 * @param TableIndex - Index in service descriptor table (0-3)
 * @param ServiceTable - Pointer to service function table
 * @param CounterTable - Pointer to call counter table (optional)
 * @param ServiceLimit - Number of services in table
 * @param ArgumentTable - Pointer to argument count table
 * @return BOOLEAN - Success or failure
 *
 * NOTE: The real implementation is in ke/procobj.c
 * This is disabled to avoid duplicate symbols
 */
#if 0
BOOLEAN
NTAPI
KeAddSystemServiceTable(
    IN PULONG_PTR Base,
    IN PULONG Count,
    IN ULONG Limit,
    IN PUCHAR Number,
    IN ULONG Index)
{
    DPRINT1("KeAddSystemServiceTable: Index %u, Limit %u - ARM64 stub\n",
            Index, Limit);

    /* Validate table index */
    if (Index >= 4)
    {
        return FALSE;
    }

    /* TODO: Store service table information */
    KeServiceDescriptorTable[Index].Base = Base;
    KeServiceDescriptorTable[Index].Count = Count;
    KeServiceDescriptorTable[Index].Limit = Limit;
    KeServiceDescriptorTable[Index].Number = Number;
    /* TODO: Store counter table if provided */
    /* TODO: Store argument table */

    /* Update global service limit */
    if (Index == 0 && Limit > KiServiceLimit)
    {
        KiServiceLimit = Limit;
    }

    return TRUE;
}
#endif /* Duplicate KeAddSystemServiceTable */

/*
 * @brief Fast user-mode callback mechanism
 *
 * ARM64 doesn't have a direct equivalent to x86 SYSENTER/SYSEXIT,
 * but we can optimize the return path.
 *
 * @param TrapFrame - Current trap frame
 * @param CallbackStack - User-mode callback stack
 * @param CallbackFunction - User-mode callback function
 * @param CallbackParameter - Parameter for callback
 * @param CallbackParameterSize - Size of parameter
 * @return NTSTATUS - Result from callback
 */
NTSTATUS
NTAPI
KiUserModeCallback(
    IN PKTRAP_FRAME TrapFrame,
    IN PVOID CallbackStack,
    IN PVOID CallbackFunction,
    IN PVOID CallbackParameter,
    IN ULONG CallbackParameterSize)
{
    DPRINT1("KiUserModeCallback: Function %p - ARM64 stub\n",
            CallbackFunction);

    /* TODO: Save current kernel state */
    /* Need to save kernel stack, return address, etc. */

    /* TODO: Set up user-mode context for callback */
    /* - Set PC to CallbackFunction
     * - Set SP to CallbackStack
     * - Set X0 to CallbackParameter
     * - Set SPSR to user mode (EL0)
     */

    /* TODO: Return to user mode via ERET */
    /* Exception return will restore user context */

    /* Callback will return via another system call */

    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @brief Return from user-mode callback
 *
 * @param Result - Result from callback
 * @param Status - Status code from callback
 * @return VOID
 */
VOID
NTAPI
KiCallbackReturn(
    IN PVOID Result,
    IN NTSTATUS Status)
{
    DPRINT1("KiCallbackReturn: Result %p, Status %lx - ARM64 stub\n",
            Result, Status);

    /* TODO: Restore saved kernel state */
    /* Restore kernel stack, registers, etc. */

    /* TODO: Copy result to kernel buffer if needed */

    /* TODO: Resume kernel execution after callback */
    /* Continue from where callback was initiated */

    /* This function should never return - halt if we somehow get here */
    for (;;) {
        /* Spin forever */
    }
}