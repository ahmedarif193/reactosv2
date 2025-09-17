/*
 * UEFI Serial I/O Protocol Debug Support for ARM64
 * Provides enhanced debug output via UEFI Serial I/O Protocol
 */

#include <freeldr.h>

//#if defined(_M_ARM64)

#include <uefildr.h>
#include <SerialIo.h>
#include <debug.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* Serial I/O Protocol instance */
static EFI_SERIAL_IO_PROTOCOL* SerialIoProtocol = NULL;
static BOOLEAN SerialInitialized = FALSE;

/* Serial I/O Protocol GUID */
EFI_GUID gEfiSerialIoProtocolGuid = EFI_SERIAL_IO_PROTOCOL_GUID;


/* Initialize UEFI Serial I/O */
static BOOLEAN UefiSerialInitialize(ULONG ComPort, ULONG BaudRate)
{
    EFI_STATUS Status;
    EFI_HANDLE* HandleBuffer = NULL;
    UINTN HandleCount = 0;
    UINTN Index;
    BOOLEAN Found = FALSE;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices) {
        return FALSE;
    }

    /* Locate all Serial I/O Protocol instances */
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &gEfiSerialIoProtocolGuid,
        NULL,
        &HandleCount,
        &HandleBuffer);

    if (EFI_ERROR(Status) || HandleCount == 0) {
        return FALSE;
    }

    /* Select the appropriate serial port (0 means auto-detect) */
    if (ComPort == 0 || ComPort > HandleCount) {
        ComPort = 1; /* Use first available port */
    }

    Index = ComPort - 1; /* Convert to 0-based index */

    if (Index < HandleCount) {
        /* Get the Serial I/O Protocol for the selected port */
        Status = GlobalSystemTable->BootServices->HandleProtocol(
            HandleBuffer[Index],
            &gEfiSerialIoProtocolGuid,
            (VOID**)&SerialIoProtocol);

        if (!EFI_ERROR(Status) && SerialIoProtocol) {
            /* Configure serial port attributes */
            Status = SerialIoProtocol->SetAttributes(
                SerialIoProtocol,
                BaudRate,
                0,  /* Use default FIFO depth */
                0,  /* No timeout */
                NoParity,
                8,  /* 8 data bits */
                OneStopBit);

            if (!EFI_ERROR(Status)) {
                /* Reset the serial device */
                SerialIoProtocol->Reset(SerialIoProtocol);
                Found = TRUE;
            }
        }
    }

    /* Free the handle buffer */
    if (HandleBuffer) {
        GlobalSystemTable->BootServices->FreePool(HandleBuffer);
    }

    return Found;
}

/* Send byte via Serial I/O Protocol */
static VOID UefiSerialPutByte(UCHAR ByteToSend)
{
    UINTN BufferSize = 1;

    if (SerialIoProtocol) {
        SerialIoProtocol->Write(SerialIoProtocol, &BufferSize, &ByteToSend);
    }
}

/* Enhanced RS232 compatibility functions with Serial I/O support */

BOOLEAN Rs232PortInitialize(IN ULONG ComPort, IN ULONG BaudRate)
{
    /* Check if already initialized */
    if (SerialInitialized) {
        return TRUE;
    }

    /* Set defaults if not specified */
    if (ComPort == 0) {
        ComPort = 1;  /* Default to first available serial port */
    }
    if (BaudRate == 0) {
        BaudRate = 115200;  /* Standard baud rate */
    }

    /* Try to initialize UEFI Serial I/O Protocol */
    if (UefiSerialInitialize(ComPort, BaudRate)) {
        SerialInitialized = TRUE;
        return TRUE;
    }

    /* Mark as initialized even if no serial available */
    /* This allows graceful degradation on systems without serial */
    SerialInitialized = TRUE;
    return TRUE;
}

BOOLEAN Rs232PortGetByte(PUCHAR ByteReceived)
{
    UINTN BufferSize = 1;
    EFI_STATUS Status;

    if (!SerialIoProtocol) {
        return FALSE;
    }

    Status = SerialIoProtocol->Read(SerialIoProtocol, &BufferSize, ByteReceived);
    return (!EFI_ERROR(Status) && BufferSize == 1);
}

BOOLEAN Rs232PortPollByte(PUCHAR ByteReceived)
{
    /* Same as Rs232PortGetByte for UEFI */
    return Rs232PortGetByte(ByteReceived);
}

VOID Rs232PortPutByte(UCHAR ByteToSend)
{
    /* If Serial I/O Protocol is available, use it */
    if (SerialIoProtocol) {
        UefiSerialPutByte(ByteToSend);
        return;
    }

    /* No output if serial protocol not available */
    /* This keeps debug output clean and production-ready */
    /* Serial output requires proper UEFI Serial I/O Protocol support */
}

BOOLEAN Rs232PortInUse(PUCHAR Base)
{
    /* Not applicable for UEFI Serial I/O */
    (void)Base;
    return (SerialIoProtocol != NULL);
}


//#endif /* _M_ARM64 */