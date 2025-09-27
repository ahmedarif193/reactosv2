/*
 * ReactOS i8042: Safe hook request helper executed only at <= APC_LEVEL
 */

#include "i8042prt.h"
#include <debug.h>

NTSTATUS NTAPI
i8042PerformHookRequest_Safe(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    KIRQL CurrentIrql;
    PDEVICE_OBJECT TopOfStack;

    /* Do not attempt to wait synchronously at high IRQL */
    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql > APC_LEVEL)
    {
        DPRINT1("i8042: Hook IOCTL 0x%lx requested at IRQL %lu; returning STATUS_INVALID_DEVICE_STATE\n",
                IoControlCode, CurrentIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    TopOfStack = IoGetAttachedDeviceReference(DeviceObject);
    if (!TopOfStack)
        return STATUS_INVALID_DEVICE_REQUEST;

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        TopOfStack,
                                        InputBuffer,
                                        InputBufferLength,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        ObDereferenceObject(TopOfStack);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DPRINT1("i8042: Hook IOCTL 0x%lx at IRQL %lu\n", IoControlCode, CurrentIrql);
    Status = IoCallDriver(TopOfStack, Irp);
    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&Event,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }
    else
    {
        Status = IoStatus.Status;
    }

    ObDereferenceObject(TopOfStack);
    return Status;
}

