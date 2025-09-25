/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Debugger Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FORWARD DECLARATIONS ******************************************************/

VOID
NTAPI
PspGetContext(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN OUT PCONTEXT Context
);

VOID
NTAPI
PspSetContext(
    OUT PKTRAP_FRAME TrapFrame,
    OUT PKEXCEPTION_FRAME ExceptionFrame,
    IN PCONTEXT Context,
    IN KPROCESSOR_MODE Mode
);

/* FUNCTIONS *****************************************************************/

/**
 * @brief Get ARM64 processor state for debugger
 */
VOID
NTAPI
KdpGetStateChange(
    IN PDBGKD_MANIPULATE_STATE64 State,
    IN PCONTEXT Context
)
{
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Context);

    /* ARM64 debugger support not yet implemented */
    DPRINT1("KdpGetStateChange: ARM64 stub\n");
}

/**
 * @brief Set ARM64 processor state from debugger
 */
NTSTATUS
NTAPI
KdpSetStateChange(
    IN PDBGKD_MANIPULATE_STATE64 State,
    IN PCONTEXT Context
)
{
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Context);
    
    /* ARM64 debugger support not yet implemented */
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Read ARM64 control registers
 */
NTSTATUS
NTAPI
KdpReadControlSpace(
    IN USHORT Processor,
    IN ULONG64 BaseAddress,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength
)
{
    UNREFERENCED_PARAMETER(Processor);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    
    *ActualLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Write ARM64 control registers
 */
NTSTATUS
NTAPI
KdpWriteControlSpace(
    IN USHORT Processor,
    IN ULONG64 BaseAddress,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength
)
{
    UNREFERENCED_PARAMETER(Processor);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    
    *ActualLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Get ARM64 processor context for debugging
 */
VOID
NTAPI
KdpGetContext(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN OUT PCONTEXT Context
)
{
    /* Use the process context functions */
    PspGetContext(TrapFrame, ExceptionFrame, Context);
}

/**
 * @brief Set ARM64 processor context from debugger
 */
VOID
NTAPI
KdpSetContext(
    IN OUT PKTRAP_FRAME TrapFrame,
    IN OUT PKEXCEPTION_FRAME ExceptionFrame,
    IN PCONTEXT Context
)
{
    /* Use the process context functions */
    PspSetContext(TrapFrame, ExceptionFrame, Context, KernelMode);
}

/**
 * @brief Handle ARM64 breakpoint for debugger
 */
BOOLEAN
NTAPI
KdpCheckBreakpoint(
    IN PKTRAP_FRAME TrapFrame
)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* ARM64 breakpoint handling not yet implemented */
    return FALSE;
}

/**
 * @brief Set ARM64 hardware breakpoint
 */
NTSTATUS
NTAPI
KdpSetBreakpoint(
    IN ULONG64 Address,
    IN ULONG Flags,
    OUT PULONG Handle
)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Flags);
    
    *Handle = 0;
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Clear ARM64 hardware breakpoint
 */
NTSTATUS
NTAPI
KdpClearBreakpoint(
    IN ULONG Handle
)
{
    UNREFERENCED_PARAMETER(Handle);
    
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Initialize ARM64 kernel debugger support
 */
VOID
NTAPI
KdpInitializeProcessor(VOID)
{
    DPRINT("ARM64: Kernel debugger processor support initialized\n");
}

/**
 * @brief ARM64 specific debugger entry point
 */
BOOLEAN
NTAPI
KdpTrap(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT Context,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChanceException
)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(SecondChanceException);

    DPRINT("ARM64: Kernel debugger trap - not yet implemented\n");

    /* For now, don't handle the trap */
    return FALSE;
}

BOOLEAN
NTAPI
KdpStub(IN PKTRAP_FRAME TrapFrame,
        IN PKEXCEPTION_FRAME ExceptionFrame,
        IN PEXCEPTION_RECORD ExceptionRecord,
        IN PCONTEXT ContextRecord,
        IN KPROCESSOR_MODE PreviousMode,
        IN BOOLEAN SecondChanceException)
{
    ULONG_PTR ExceptionCommand;

    /* Check if this was a breakpoint due to DbgPrint or Load/UnloadSymbols */
    ExceptionCommand = ExceptionRecord->ExceptionInformation[0];
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->NumberParameters > 0) &&
        ((ExceptionCommand == BREAKPOINT_LOAD_SYMBOLS) ||
         (ExceptionCommand == BREAKPOINT_UNLOAD_SYMBOLS) ||
         (ExceptionCommand == BREAKPOINT_COMMAND_STRING) ||
         (ExceptionCommand == BREAKPOINT_PRINT)))
    {
        /* This we can handle: simply bump the Program Counter */
        KeSetContextPc(ContextRecord,
                       KeGetContextPc(ContextRecord) + KD_BREAKPOINT_SIZE);
        return TRUE;
    }
    else if (KdPitchDebugger)
    {
        /* There's no debugger, fail. */
        return FALSE;
    }
    else if ((KdAutoEnableOnEvent) &&
             (KdPreviouslyEnabled) &&
             !(KdDebuggerEnabled) &&
             (NT_SUCCESS(KdEnableDebugger())) &&
             (KdDebuggerEnabled))
    {
        /* Debugging was Auto-Enabled. We can now send this to KD. */
        return KdpTrap(TrapFrame,
                       ExceptionFrame,
                       ExceptionRecord,
                       ContextRecord,
                       PreviousMode,
                       SecondChanceException);
    }
    else
    {
        /* FIXME: All we can do in this case is trace this exception */
        return FALSE;
    }
}