/*
 * UEFI Serial I/O Protocol Support
 * Provides serial output via UEFI Serial I/O Protocol
 * With PL011 UART fallback for ARM64 systems
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

#ifdef _M_ARM64
/* PL011 UART registers and addresses for ARM64 platforms */
#define PL011_UART_BASE    0x09000000  /* QEMU ARM64 virt machine UART0 address */
#define PL011_DR           0x000       /* Data Register */
#define PL011_FR           0x018       /* Flag Register */
#define PL011_FR_TXFF      (1 << 5)    /* Transmit FIFO Full */
#define PL011_FR_RXFE      (1 << 4)    /* Receive FIFO Empty */

/* PL011 UART access macros */
#define PL011_READ(offset) \
    (*(volatile UINT32*)((UINTN)PL011_UART_BASE + (offset)))
#define PL011_WRITE(offset, value) \
    (*(volatile UINT32*)((UINTN)PL011_UART_BASE + (offset)) = (value))

static BOOLEAN UsePL011Fallback = FALSE;

/* Check if PL011 UART is present and accessible */
static BOOLEAN PL011IsPresent(VOID)
{
    volatile UINT32 *uart_fr = (volatile UINT32*)((UINTN)PL011_UART_BASE + PL011_FR);
    UINT32 fr_value;

    /* Read and validate the flag register */
    fr_value = *uart_fr;

    /* Check if value looks like valid PL011 FR (bits 0-7 used, upper bits reserved) */
    if ((fr_value & 0xFFFFFF00) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

/* Send a byte via PL011 UART */
static VOID PL011PutByte(UCHAR ByteToSend)
{
    volatile UINT32 *uart_dr = (volatile UINT32*)((UINTN)PL011_UART_BASE + PL011_DR);
    volatile UINT32 *uart_fr = (volatile UINT32*)((UINTN)PL011_UART_BASE + PL011_FR);

    /* Wait until transmit FIFO is not full */
    while ((*uart_fr & PL011_FR_TXFF) != 0)
    {
    }

    /* Write the byte to data register */
    *uart_dr = (UINT32)ByteToSend;
}
#endif /* _M_ARM64 */


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
                /* Some firmwares misbehave on Reset(); avoid it here */
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
        EFI_TPL OldTpl = TPL_APPLICATION;
        if (GlobalSystemTable && GlobalSystemTable->BootServices)
            OldTpl = GlobalSystemTable->BootServices->RaiseTPL(TPL_NOTIFY);
        SerialIoProtocol->Write(SerialIoProtocol, &BufferSize, &ByteToSend);
        if (GlobalSystemTable && GlobalSystemTable->BootServices)
            GlobalSystemTable->BootServices->RestoreTPL(OldTpl);
    }
}

/* Exported helper: write a whole buffer in one go when possible. */
VOID
UefiSerialWriteBuffer(const CHAR* Buffer, size_t Length)
{
    if (!SerialIoProtocol || !Buffer || Length == 0)
        return;

    UINTN Size = (UINTN)Length;
    EFI_TPL OldTpl = TPL_APPLICATION;
    if (GlobalSystemTable && GlobalSystemTable->BootServices)
        OldTpl = GlobalSystemTable->BootServices->RaiseTPL(TPL_NOTIFY);
    SerialIoProtocol->Write(SerialIoProtocol, &Size, (VOID*)Buffer);
    if (GlobalSystemTable && GlobalSystemTable->BootServices)
        GlobalSystemTable->BootServices->RestoreTPL(OldTpl);
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

#ifdef _M_ARM64
    /* Try PL011 UART as fallback on ARM64 */
    if (PL011IsPresent()) {
        UsePL011Fallback = TRUE;
        SerialInitialized = TRUE;
        return TRUE;
    }
#endif

    /* Mark as initialized for graceful degradation */
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

#ifdef _M_ARM64
    /* Use PL011 UART fallback if available */
    if (UsePL011Fallback) {
        PL011PutByte(ByteToSend);
        return;
    }
#endif
}

BOOLEAN Rs232PortInUse(PUCHAR Base)
{
    /* Not applicable for UEFI Serial I/O */
    (void)Base;
#ifdef _M_ARM64
    return (SerialIoProtocol != NULL || UsePL011Fallback);
#else
    return (SerialIoProtocol != NULL);
#endif
}


//#endif /* _M_ARM64 */
