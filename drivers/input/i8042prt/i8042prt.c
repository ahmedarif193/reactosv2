/*
 * PROJECT:     ReactOS i8042 (ps/2 keyboard-mouse controller) driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/input/i8042prt/i8042prt.c
 * PURPOSE:     Driver entry function
 * PROGRAMMERS: Copyright Victor Kirhenshtein (sauros@iname.com)
                Copyright Jason Filby (jasonfilby@yahoo.com)
                Copyright Martijn Vernooij (o112w8r02@sneakemail.com)
                Copyright 2006-2007 Hervé Poussineau (hpoussin@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "i8042prt.h"

#include <debug.h>

/* FUNCTIONS *****************************************************************/

static DRIVER_STARTIO i8042StartIo;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
static DRIVER_DISPATCH i8042DeviceControl;
_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
static DRIVER_DISPATCH i8042InternalDeviceControl;
_Dispatch_type_(IRP_MJ_SYSTEM_CONTROL)
static DRIVER_DISPATCH i8042SystemControl;
_Dispatch_type_(IRP_MJ_POWER)
static DRIVER_DISPATCH i8042Power;
DRIVER_INITIALIZE DriverEntry;

NTSTATUS NTAPI
i8042AddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT Pdo)
{
	PI8042_DRIVER_EXTENSION DriverExtension;
	PFDO_DEVICE_EXTENSION DeviceExtension = NULL;
	PDEVICE_OBJECT Fdo = NULL;
	ULONG DeviceExtensionSize;
	NTSTATUS Status;

	TRACE_(I8042PRT, "i8042AddDevice(%p %p)\n", DriverObject, Pdo);

	DriverExtension = (PI8042_DRIVER_EXTENSION)IoGetDriverObjectExtension(DriverObject, DriverObject);

	if (Pdo == NULL)
	{
		/* We're getting a NULL Pdo at the first call as
		 * we are a legacy driver. Ignore it */
		return STATUS_SUCCESS;
	}

	/* Create new device object. As we don't know if the device would be a keyboard
	 * or a mouse, we have to allocate the biggest device extension. */
	DeviceExtensionSize = MAX(sizeof(I8042_KEYBOARD_EXTENSION), sizeof(I8042_MOUSE_EXTENSION));
	Status = IoCreateDevice(
		DriverObject,
		DeviceExtensionSize,
		NULL,
		Pdo->DeviceType,
		FILE_DEVICE_SECURE_OPEN,
		TRUE,
		&Fdo);
	if (!NT_SUCCESS(Status))
	{
		WARN_(I8042PRT, "IoCreateDevice() failed with status 0x%08lx\n", Status);
		goto cleanup;
	}


	/* Always use buffered I/O for port devices */
	Fdo->Flags |= DO_BUFFERED_IO;
	Fdo->Flags &= ~DO_DIRECT_IO;

	DeviceExtension = (PFDO_DEVICE_EXTENSION)Fdo->DeviceExtension;
	RtlZeroMemory(DeviceExtension, DeviceExtensionSize);
	DeviceExtension->Type = Unknown;
	DeviceExtension->Fdo = Fdo;
	DeviceExtension->Pdo = Pdo;
	DeviceExtension->PortDeviceExtension = &DriverExtension->Port;
	Status = IoAttachDeviceToDeviceStackSafe(Fdo, Pdo, &DeviceExtension->LowerDevice);
	if (!NT_SUCCESS(Status))
	{
		WARN_(I8042PRT, "IoAttachDeviceToDeviceStackSafe() failed with status 0x%08lx\n", Status);
		goto cleanup;
	}

	ExInterlockedInsertTailList(
		&DriverExtension->DeviceListHead,
		&DeviceExtension->ListEntry,
		&DriverExtension->DeviceListLock);

	Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;

cleanup:
	if (DeviceExtension && DeviceExtension->LowerDevice)
		IoDetachDevice(DeviceExtension->LowerDevice);
	if (Fdo)
		IoDeleteDevice(Fdo);
	return Status;
}

NTSTATUS NTAPI
i8042PerformHookRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    PDEVICE_OBJECT TopOfStack;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    KIRQL CurrentIrql;

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

    CurrentIrql = KeGetCurrentIrql();
    DPRINT1("i8042: Hook IOCTL 0x%lx at IRQL %lu\n", IoControlCode, CurrentIrql);
    Status = IoCallDriver(TopOfStack, Irp);
    if (Status == STATUS_PENDING)
    {
        if (CurrentIrql <= APC_LEVEL)
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
            ULONG SpinCount = 0;

            while (!KeReadStateEvent(&Event))
            {
                KeStallExecutionProcessor(50);

                if (++SpinCount >= 20000)
                {
                    IoCancelIrp(Irp);

                    while (!KeReadStateEvent(&Event))
                    {
                        KeStallExecutionProcessor(50);
                    }
                    break;
                }
            }

            Status = IoStatus.Status;
        }
    }
    else
    {
        Status = IoStatus.Status;
    }

    ObDereferenceObject(TopOfStack);
    return Status;
}


static VOID NTAPI
i8042StartIo(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PFDO_DEVICE_EXTENSION DeviceExtension;

	DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	switch (DeviceExtension->Type)
	{
		case Keyboard:
			i8042KbdStartIo(DeviceObject, Irp);
			break;
		default:
			ERR_(I8042PRT, "Unknown FDO type %u\n", DeviceExtension->Type);
			ASSERT(FALSE);
			break;
	}
}

/* Write the current byte of the packet. Returns FALSE in case
 * of problems.
 */
static BOOLEAN
i8042PacketWrite(
	IN PPORT_DEVICE_EXTENSION DeviceExtension)
{
	UCHAR Port = DeviceExtension->PacketPort;

	if (Port)
	{
		if (!i8042Write(DeviceExtension,
		                DeviceExtension->ControlPort,
		                Port))
		{
			/* something is really wrong! */
			WARN_(I8042PRT, "Failed to send packet byte!\n");
			return FALSE;
		}
	}

	return i8042Write(DeviceExtension,
	                  DeviceExtension->DataPort,
	                  DeviceExtension->Packet.Bytes[DeviceExtension->Packet.CurrentByte]);
}

BOOLEAN
i8042PacketIsr(
	IN PPORT_DEVICE_EXTENSION DeviceExtension,
	IN UCHAR Output)
{
	if (DeviceExtension->Packet.State == Idle)
		return FALSE;

	switch (Output)
	{
		case KBD_RESEND:
			DeviceExtension->PacketResends++;
			if (DeviceExtension->PacketResends > DeviceExtension->Settings.ResendIterations)
			{
				DeviceExtension->Packet.State = Idle;
				DeviceExtension->PacketComplete = TRUE;
				DeviceExtension->PacketResult = STATUS_IO_TIMEOUT;
				DeviceExtension->PacketResends = 0;
				return TRUE;
			}
			DeviceExtension->Packet.CurrentByte--;
			break;

		case KBD_NACK:
			DeviceExtension->Packet.State = Idle;
			DeviceExtension->PacketComplete = TRUE;
			DeviceExtension->PacketResult = STATUS_UNEXPECTED_IO_ERROR;
			DeviceExtension->PacketResends = 0;
			return TRUE;

		default:
			DeviceExtension->PacketResends = 0;
	}

	if (DeviceExtension->Packet.CurrentByte >= DeviceExtension->Packet.ByteCount)
	{
		DeviceExtension->Packet.State = Idle;
		DeviceExtension->PacketComplete = TRUE;
		DeviceExtension->PacketResult = STATUS_SUCCESS;
		return TRUE;
	}

	if (!i8042PacketWrite(DeviceExtension))
	{
		DeviceExtension->Packet.State = Idle;
		DeviceExtension->PacketComplete = TRUE;
		DeviceExtension->PacketResult = STATUS_IO_TIMEOUT;
		return TRUE;
	}
	DeviceExtension->Packet.CurrentByte++;

	return TRUE;
}

/*
 * This function starts a packet. It must be called with the
 * correct DIRQL.
 */
NTSTATUS
i8042StartPacket(
	IN PPORT_DEVICE_EXTENSION DeviceExtension,
	IN PFDO_DEVICE_EXTENSION FdoDeviceExtension,
	IN PUCHAR Bytes,
	IN ULONG ByteCount,
	IN PIRP Irp)
{
	PKINTERRUPT InterruptObject;
	KIRQL Irql;
	NTSTATUS Status;

	InterruptObject = I8042pGetInterruptObject(DeviceExtension);
	if (!InterruptObject)
		return STATUS_DEVICE_BUSY;

	Irql = KeAcquireInterruptSpinLock(InterruptObject);

	if (DeviceExtension->Packet.State != Idle)
	{
		Status = STATUS_DEVICE_BUSY;
		goto done;
	}

	switch (FdoDeviceExtension->Type)
	{
		case Keyboard: DeviceExtension->PacketPort = 0; break;
		case Mouse: DeviceExtension->PacketPort = CTRL_WRITE_MOUSE; break;
		default:
			ERR_(I8042PRT, "Unknown FDO type %u\n", FdoDeviceExtension->Type);
			ASSERT(FALSE);
			Status = STATUS_INTERNAL_ERROR;
			goto done;
	}

	DeviceExtension->Packet.Bytes = Bytes;
	DeviceExtension->Packet.CurrentByte = 0;
	DeviceExtension->Packet.ByteCount = ByteCount;
	DeviceExtension->Packet.State = SendingBytes;
	DeviceExtension->PacketResult = Status = STATUS_PENDING;
	DeviceExtension->CurrentIrp = Irp;
	DeviceExtension->CurrentIrpDevice = FdoDeviceExtension->Fdo;

	if (!i8042PacketWrite(DeviceExtension))
	{
		Status = STATUS_IO_TIMEOUT;
		DeviceExtension->Packet.State = Idle;
		DeviceExtension->PacketResult = STATUS_ABANDONED;
		goto done;
	}

	DeviceExtension->Packet.CurrentByte++;

done:
	KeReleaseInterruptSpinLock(InterruptObject, Irql);

	if (Status != STATUS_PENDING)
	{
		DeviceExtension->CurrentIrp = NULL;
		DeviceExtension->CurrentIrpDevice = NULL;
		Irp->IoStatus.Status = Status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}
	return Status;
}

static NTSTATUS NTAPI
i8042DeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PFDO_DEVICE_EXTENSION DeviceExtension;

	TRACE_(I8042PRT, "i8042DeviceControl(%p %p)\n", DeviceObject, Irp);
	DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	switch (DeviceExtension->Type)
	{
		case Keyboard:
			return i8042KbdDeviceControl(DeviceObject, Irp);
		default:
			Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return STATUS_INVALID_DEVICE_REQUEST;
	}
}

static NTSTATUS NTAPI
i8042InternalDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PFDO_DEVICE_EXTENSION DeviceExtension;
	ULONG ControlCode;
	NTSTATUS Status;

	TRACE_(I8042PRT, "i8042InternalDeviceControl(%p %p)\n", DeviceObject, Irp);
	DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	switch (DeviceExtension->Type)
	{
		case Unknown:
		{
			ControlCode = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.IoControlCode;
			switch (ControlCode)
			{
				case IOCTL_INTERNAL_KEYBOARD_CONNECT:
					Status = i8042KbdInternalDeviceControl(DeviceObject, Irp);
					break;
				case IOCTL_INTERNAL_MOUSE_CONNECT:
					Status = i8042MouInternalDeviceControl(DeviceObject, Irp);
					break;
				default:
					ERR_(I8042PRT, "Unknown IO control code 0x%lx\n", ControlCode);
					ASSERT(FALSE);
					Status = STATUS_INVALID_DEVICE_REQUEST;
					break;
			}
			break;
		}
		case Keyboard:
			Status = i8042KbdInternalDeviceControl(DeviceObject, Irp);
			break;
		case Mouse:
			Status = i8042MouInternalDeviceControl(DeviceObject, Irp);
			break;
		default:
			ERR_(I8042PRT, "Unknown FDO type %u\n", DeviceExtension->Type);
			ASSERT(FALSE);
			Status = STATUS_INTERNAL_ERROR;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			break;
	}

	return Status;
}

static NTSTATUS NTAPI
i8042Power(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PFDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
	PDEVICE_OBJECT LowerDevice = DeviceExtension->LowerDevice;

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	return PoCallDriver(LowerDevice, Irp);
}

static NTSTATUS NTAPI
i8042SystemControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	return ForwardIrpAndForget(DeviceObject, Irp);
}

NTSTATUS NTAPI
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath)
{
	PI8042_DRIVER_EXTENSION DriverExtension;
	NTSTATUS Status;

	Status = IoAllocateDriverObjectExtension(
		DriverObject,
		DriverObject,
		sizeof(I8042_DRIVER_EXTENSION),
		(PVOID*)&DriverExtension);
	if (!NT_SUCCESS(Status))
	{
		WARN_(I8042PRT, "IoAllocateDriverObjectExtension() failed with status 0x%08lx\n", Status);
		return Status;
	}
	RtlZeroMemory(DriverExtension, sizeof(I8042_DRIVER_EXTENSION));
	KeInitializeSpinLock(&DriverExtension->Port.SpinLock);
	InitializeListHead(&DriverExtension->DeviceListHead);
	KeInitializeSpinLock(&DriverExtension->DeviceListLock);

	Status = DuplicateUnicodeString(
		RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
		RegistryPath,
		&DriverExtension->RegistryPath);
	if (!NT_SUCCESS(Status))
	{
		WARN_(I8042PRT, "DuplicateUnicodeString() failed with status 0x%08lx\n", Status);
		return Status;
	}

	Status = ReadRegistryEntries(&DriverExtension->RegistryPath, &DriverExtension->Port.Settings);
	if (!NT_SUCCESS(Status))
	{
		WARN_(I8042PRT, "ReadRegistryEntries() failed with status 0x%08lx\n", Status);
		return Status;
	}

	DriverObject->DriverExtension->AddDevice = i8042AddDevice;
	DriverObject->DriverStartIo = i8042StartIo;

	DriverObject->MajorFunction[IRP_MJ_CREATE]  = i8042Create;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = i8042Cleanup;
	DriverObject->MajorFunction[IRP_MJ_CLOSE]   = i8042Close;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = i8042DeviceControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = i8042InternalDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_POWER]   = i8042Power;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = i8042SystemControl;
	DriverObject->MajorFunction[IRP_MJ_PNP]     = i8042Pnp;

    i8042InitializeHwHacks();

	return STATUS_SUCCESS;
}
