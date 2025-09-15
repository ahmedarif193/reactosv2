/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/kd64/amd64/kdx64_full.c
 * PURPOSE:         Complete KD support routines for AMD64
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 * @brief Reads memory from a specific process
 * @param ProcessHandle - Process to read from (NULL for current)
 * @param BaseAddress - Address to read from
 * @param Buffer - Buffer to receive data
 * @param Size - Size to read
 * @param BytesRead - Actual bytes read
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KdpSysReadMemory(
    _In_opt_ HANDLE ProcessHandle,
    _In_ ULONG64 BaseAddress,
    _Out_ PVOID Buffer,
    _In_ SIZE_T Size,
    _Out_ PSIZE_T BytesRead)
{
    PEPROCESS Process = NULL;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    /* Validate parameters */
    if (!Buffer || !BytesRead)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesRead = 0;

    /* Get process if specified */
    if (ProcessHandle)
    {
        NTSTATUS Status = ObReferenceObjectByHandle(
            ProcessHandle,
            PROCESS_VM_READ,
            PsProcessType,
            KernelMode,
            (PVOID*)&Process,
            NULL);

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        /* Attach to process */
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Use SEH to protect the read */
    _SEH2_TRY
    {
        /* Check if this is kernel memory */
        if (BaseAddress >= (ULONG64)MmSystemRangeStart)
        {
            /* Direct kernel read */
            RtlCopyMemory(Buffer, (PVOID)BaseAddress, Size);
        }
        else
        {
            /* User memory read */
            ProbeForRead((PVOID)BaseAddress, Size, 1);
            RtlCopyMemory(Buffer, (PVOID)BaseAddress, Size);
        }

        *BytesRead = Size;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Read failed */
        *BytesRead = 0;
    }
    _SEH2_END;

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
        ObDereferenceObject(Process);
    }

    return (*BytesRead > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/*
 * @implemented
 * @brief Writes memory to a specific process
 * @param ProcessHandle - Process to write to (NULL for current)
 * @param BaseAddress - Address to write to
 * @param Buffer - Buffer containing data
 * @param Size - Size to write
 * @param BytesWritten - Actual bytes written
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KdpSysWriteMemory(
    _In_opt_ HANDLE ProcessHandle,
    _In_ ULONG64 BaseAddress,
    _In_ PVOID Buffer,
    _In_ SIZE_T Size,
    _Out_ PSIZE_T BytesWritten)
{
    PEPROCESS Process = NULL;
    KAPC_STATE ApcState;
    BOOLEAN ProcessAttached = FALSE;

    /* Validate parameters */
    if (!Buffer || !BytesWritten)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    /* Get process if specified */
    if (ProcessHandle)
    {
        NTSTATUS Status = ObReferenceObjectByHandle(
            ProcessHandle,
            PROCESS_VM_WRITE,
            PsProcessType,
            KernelMode,
            (PVOID*)&Process,
            NULL);

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        /* Attach to process */
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        ProcessAttached = TRUE;
    }

    /* Use SEH to protect the write */
    _SEH2_TRY
    {
        /* Check if this is kernel memory */
        if (BaseAddress >= (ULONG64)MmSystemRangeStart)
        {
            /* Direct kernel write */
            RtlCopyMemory((PVOID)BaseAddress, Buffer, Size);
        }
        else
        {
            /* User memory write */
            ProbeForWrite((PVOID)BaseAddress, Size, 1);
            RtlCopyMemory((PVOID)BaseAddress, Buffer, Size);
        }

        *BytesWritten = Size;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Write failed */
        *BytesWritten = 0;
    }
    _SEH2_END;

    /* Detach from process */
    if (ProcessAttached)
    {
        KeUnstackDetachProcess(&ApcState);
        ObDereferenceObject(Process);
    }

    return (*BytesWritten > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/*
 * @implemented
 * @brief Reads control space
 * @param Processor - Processor number
 * @param BaseAddress - Control space address
 * @param Buffer - Buffer to receive data
 * @param Size - Size to read
 * @param BytesRead - Actual bytes read
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KdpSysReadControlSpace_Full(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_ PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PULONG BytesRead)
{
    PKIPCR Pcr;
    PKPRCB Prcb;
    PVOID ControlStart = NULL;

    /* Validate processor number */
    if (Processor >= KeNumberProcessors)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Get processor control blocks */
    Prcb = KiProcessorBlock[Processor];
    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);

    /* Determine control space type from base address */
    switch (BaseAddress & 0xFFFF)
    {
        case 0: /* KPCR */
            ControlStart = Pcr;
            break;

        case 1: /* KPRCB */
            ControlStart = Prcb;
            break;

        case 2: /* Current thread */
            ControlStart = Prcb->CurrentThread;
            break;

        case 3: /* TEB */
            if (Prcb->CurrentThread)
                ControlStart = Prcb->CurrentThread->Teb;
            break;

        case 4: /* PEB */
            if (Prcb->CurrentThread && Prcb->CurrentThread->ApcState.Process)
                ControlStart = ((PEPROCESS)Prcb->CurrentThread->ApcState.Process)->Peb;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (!ControlStart)
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Copy the data */
    _SEH2_TRY
    {
        RtlCopyMemory(Buffer, ControlStart, Size);
        *BytesRead = Size;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *BytesRead = 0;
        _SEH2_YIELD(return STATUS_UNSUCCESSFUL);
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Reads I/O space
 * @param InterfaceType - Bus interface type
 * @param BusNumber - Bus number
 * @param AddressSpace - 1 for I/O, 0 for memory
 * @param IoAddress - I/O address
 * @param Buffer - Buffer to receive data
 * @param Size - Size to read (1, 2, or 4)
 * @param BytesRead - Actual bytes read
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KdpSysReadIoSpace_Full(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _Out_ PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PULONG BytesRead)
{
    /* Validate size */
    if (Size != 1 && Size != 2 && Size != 4)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesRead = 0;

    /* Check address space */
    if (AddressSpace == 1)
    {
        /* I/O space */
        switch (Size)
        {
            case 1:
                *(PUCHAR)Buffer = __inbyte((USHORT)IoAddress);
                break;
            case 2:
                *(PUSHORT)Buffer = __inword((USHORT)IoAddress);
                break;
            case 4:
                *(PULONG)Buffer = __indword((USHORT)IoAddress);
                break;
        }
    }
    else
    {
        /* Memory space */
        _SEH2_TRY
        {
            switch (Size)
            {
                case 1:
                    *(PUCHAR)Buffer = *(volatile UCHAR*)IoAddress;
                    break;
                case 2:
                    *(PUSHORT)Buffer = *(volatile USHORT*)IoAddress;
                    break;
                case 4:
                    *(PULONG)Buffer = *(volatile ULONG*)IoAddress;
                    break;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return STATUS_UNSUCCESSFUL);
        }
        _SEH2_END;
    }

    *BytesRead = Size;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Writes I/O space
 * @param InterfaceType - Bus interface type
 * @param BusNumber - Bus number
 * @param AddressSpace - 1 for I/O, 0 for memory
 * @param IoAddress - I/O address
 * @param Buffer - Buffer containing data
 * @param Size - Size to write (1, 2, or 4)
 * @param BytesWritten - Actual bytes written
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KdpSysWriteIoSpace_Full(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 IoAddress,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PULONG BytesWritten)
{
    /* Validate size */
    if (Size != 1 && Size != 2 && Size != 4)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    /* Check address space */
    if (AddressSpace == 1)
    {
        /* I/O space */
        switch (Size)
        {
            case 1:
                __outbyte((USHORT)IoAddress, *(PUCHAR)Buffer);
                break;
            case 2:
                __outword((USHORT)IoAddress, *(PUSHORT)Buffer);
                break;
            case 4:
                __outdword((USHORT)IoAddress, *(PULONG)Buffer);
                break;
        }
    }
    else
    {
        /* Memory space */
        _SEH2_TRY
        {
            switch (Size)
            {
                case 1:
                    *(volatile UCHAR*)IoAddress = *(PUCHAR)Buffer;
                    break;
                case 2:
                    *(volatile USHORT*)IoAddress = *(PUSHORT)Buffer;
                    break;
                case 4:
                    *(volatile ULONG*)IoAddress = *(PULONG)Buffer;
                    break;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return STATUS_UNSUCCESSFUL);
        }
        _SEH2_END;
    }

    *BytesWritten = Size;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Gets bus data
 * @param BusDataType - Type of bus data
 * @param BusNumber - Bus number
 * @param SlotNumber - Slot number
 * @param Buffer - Buffer to receive data
 * @param Offset - Offset in configuration space
 * @param Length - Length to read
 * @return Number of bytes read
 */
ULONG
NTAPI
KdpSysGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return HalGetBusDataByOffset(BusDataType,
                                  BusNumber,
                                  SlotNumber,
                                  Buffer,
                                  Offset,
                                  Length);
}

/*
 * @implemented
 * @brief Sets bus data
 * @param BusDataType - Type of bus data
 * @param BusNumber - Bus number
 * @param SlotNumber - Slot number
 * @param Buffer - Buffer containing data
 * @param Offset - Offset in configuration space
 * @param Length - Length to write
 * @return Number of bytes written
 */
ULONG
NTAPI
KdpSysSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return HalSetBusDataByOffset(BusDataType,
                                  BusNumber,
                                  SlotNumber,
                                  Buffer,
                                  Offset,
                                  Length);
}

/*
 * @implemented
 * @brief Checks for low memory corruption
 * @return STATUS_SUCCESS or STATUS_MEMORY_CORRUPTION
 */
NTSTATUS
NTAPI
KdpSysCheckLowMemory_Full(_In_ ULONG Flags)
{
    PUCHAR LowMemory = (PUCHAR)0;
    ULONG i;

    /* Check first 4KB for corruption patterns */
    _SEH2_TRY
    {
        for (i = 0; i < 0x1000; i++)
        {
            if (LowMemory[i] == 0xDD || LowMemory[i] == 0xCC)
            {
                /* Possible corruption detected */
                DPRINT1("Low memory corruption detected at %p: %02X\n",
                        &LowMemory[i], LowMemory[i]);
                return STATUS_DATA_CHECKSUM_ERROR;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Can't access low memory */
        return STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/* END OF FILE */