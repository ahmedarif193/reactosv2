/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Debugger Support Functions
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/**
 * @brief Read MSR register (ARM64 system register)
 */
NTSTATUS
NTAPI
KdpSysReadMsr(
    IN ULONG Msr,
    OUT PULONG64 Data)
{
    /* ARM64 doesn't have MSRs in the x86 sense */
    /* System registers are accessed differently */
    DPRINT1("KdpSysReadMsr: Not implemented for ARM64\n");
    *Data = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Write MSR register (ARM64 system register)
 */
NTSTATUS
NTAPI
KdpSysWriteMsr(
    IN ULONG Msr,
    IN PULONG64 Data)
{
    /* ARM64 doesn't have MSRs in the x86 sense */
    DPRINT1("KdpSysWriteMsr: Not implemented for ARM64\n");
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Read bus data
 */
NTSTATUS
NTAPI
KdpSysReadBusData(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN ULONG Offset,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength)
{
    DPRINT1("KdpSysReadBusData: Not implemented for ARM64\n");
    *ActualLength = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Write bus data
 */
NTSTATUS
NTAPI
KdpSysWriteBusData(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN ULONG Offset,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength)
{
    DPRINT1("KdpSysWriteBusData: Not implemented for ARM64\n");
    *ActualLength = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Read I/O space
 */
NTSTATUS
NTAPI
KdpSysReadIoSpace(
    IN INTERFACE_TYPE InterfaceType,
    IN ULONG BusNumber,
    IN ULONG AddressSpace,
    IN ULONG64 IoAddress,
    IN PVOID DataValue,
    IN ULONG DataSize,
    OUT PULONG ActualDataSize)
{
    /* ARM64 uses memory-mapped I/O */
    DPRINT1("KdpSysReadIoSpace: Not implemented for ARM64\n");
    *ActualDataSize = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Write I/O space
 */
NTSTATUS
NTAPI
KdpSysWriteIoSpace(
    IN INTERFACE_TYPE InterfaceType,
    IN ULONG BusNumber,
    IN ULONG AddressSpace,
    IN ULONG64 IoAddress,
    IN PVOID DataValue,
    IN ULONG DataSize,
    OUT PULONG ActualDataSize)
{
    /* ARM64 uses memory-mapped I/O */
    DPRINT1("KdpSysWriteIoSpace: Not implemented for ARM64\n");
    *ActualDataSize = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Check low memory
 */
NTSTATUS
NTAPI
KdpSysCheckLowMemory(
    IN ULONG Flags)
{
    DPRINT1("KdpSysCheckLowMemory: Not implemented for ARM64\n");
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

/**
 * @brief Read control space
 */
NTSTATUS
NTAPI
KdpSysReadControlSpace(
    IN ULONG Processor,
    IN ULONG64 BaseAddress,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength)
{
    DPRINT1("KdpSysReadControlSpace: Not implemented for ARM64\n");
    *ActualLength = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Write control space
 */
NTSTATUS
NTAPI
KdpSysWriteControlSpace(
    IN ULONG Processor,
    IN ULONG64 BaseAddress,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG ActualLength)
{
    DPRINT1("KdpSysWriteControlSpace: Not implemented for ARM64\n");
    *ActualLength = 0;
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Set context state
 */
VOID
NTAPI
KdpSetContextState(
    IN PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    IN PCONTEXT Context)
{
    /* Set basic context state */
    DPRINT("KdpSetContextState: ARM64 stub\n");

    /* TODO: Properly set the context state in WaitStateChange */
}

/**
 * @brief Allow debugger disable
 */
NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* Allow disabling on ARM64 for now */
    return STATUS_SUCCESS;
}

/**
 * @brief Save processor control state
 */
VOID
NTAPI
KiSaveProcessorControlState(
    IN PKPROCESSOR_STATE ProcessorState)
{
    DPRINT("KiSaveProcessorControlState: ARM64 stub\n");

    /* TODO: Save ARM64-specific control registers */
    /* This includes system registers, debug registers, etc. */
}

/**
 * @brief Restore processor control state
 */
VOID
NTAPI
KiRestoreProcessorControlState(
    IN PKPROCESSOR_STATE ProcessorState)
{
    DPRINT("KiRestoreProcessorControlState: ARM64 stub\n");

    /* TODO: Restore ARM64-specific control registers */
}