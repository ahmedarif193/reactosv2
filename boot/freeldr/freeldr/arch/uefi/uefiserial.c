/*
 * UEFI Serial I/O Protocol Debug Support for ARM64
 * Provides enhanced debug output via UEFI Serial I/O Protocol
 */

#include <freeldr.h>

#if defined(_M_ARM64) && defined(UEFIBOOT)

#include <uefildr.h>
#include <SerialIo.h>
#include <debug.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* Serial I/O Protocol instance */
static EFI_SERIAL_IO_PROTOCOL* SerialIoProtocol = NULL;
static BOOLEAN SerialInitialized = FALSE;
static ULONG DebugComPort = 0;
static BOOLEAN UseSerialForDebug = FALSE;

/* Serial I/O Protocol GUID */
EFI_GUID gEfiSerialIoProtocolGuid = EFI_SERIAL_IO_PROTOCOL_GUID;

/* Forward declarations */
PCSTR GetCommandLineOptions(VOID);

/* Parse debug port from command line */
static ULONG ParseDebugPort(PCSTR Options)
{
    PCSTR debugPort;
    ULONG port = 0;

    if (!Options) return 0;

    /* Look for /DEBUGPORT=COMx or /DEBUGPORT=SERIAL */
    debugPort = strstr(Options, "/DEBUGPORT=");
    if (!debugPort) {
        debugPort = strstr(Options, "/debugport=");
    }

    if (debugPort) {
        debugPort += 11; /* Skip "/DEBUGPORT=" */

        if (_strnicmp(debugPort, "COM", 3) == 0) {
            port = atoi(debugPort + 3);
        } else if (_strnicmp(debugPort, "SERIAL", 6) == 0) {
            port = 1; /* Default to first serial port */
        }
    }

    return port;
}

/* Parse baud rate from command line */
static ULONG ParseBaudRate(PCSTR Options)
{
    PCSTR baudStr;
    ULONG baudRate = 115200; /* Default */

    if (!Options) return baudRate;

    /* Look for /BAUDRATE=xxxxx */
    baudStr = strstr(Options, "/BAUDRATE=");
    if (!baudStr) {
        baudStr = strstr(Options, "/baudrate=");
    }

    if (baudStr) {
        baudStr += 10; /* Skip "/BAUDRATE=" */
        baudRate = atoi(baudStr);

        /* Validate baud rate */
        if (baudRate != 9600 && baudRate != 19200 &&
            baudRate != 38400 && baudRate != 57600 &&
            baudRate != 115200) {
            baudRate = 115200; /* Default if invalid */
        }
    }

    return baudRate;
}

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
    PCSTR Options;

    /* Check if already initialized */
    if (SerialInitialized) {
        return TRUE;
    }

    /* Get command line options */
    Options = GetCommandLineOptions();

    /* Parse debug port from command line if not specified */
    if (ComPort == 0) {
        ComPort = ParseDebugPort(Options);
    }

    /* Parse baud rate from command line if not specified */
    if (BaudRate == 0) {
        BaudRate = ParseBaudRate(Options);
    }

    /* Try to initialize UEFI Serial I/O if requested */
    if (ComPort > 0) {
        if (UefiSerialInitialize(ComPort, BaudRate)) {
            UseSerialForDebug = TRUE;
            DebugComPort = ComPort;
            SerialInitialized = TRUE;

            /* Send initialization message */
            PCSTR initMsg = "\r\n[FreeLDR] UEFI Serial I/O Debug Initialized\r\n";
            while (*initMsg) {
                UefiSerialPutByte(*initMsg++);
            }

            return TRUE;
        }
    }

    /* Fallback to console output */
    UseSerialForDebug = FALSE;
    SerialInitialized = TRUE;
    return TRUE; /* Always succeed with console fallback */
}

BOOLEAN Rs232PortGetByte(PUCHAR ByteReceived)
{
    UINTN BufferSize = 1;
    EFI_STATUS Status;

    if (!UseSerialForDebug || !SerialIoProtocol) {
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
    CHAR16 WideChar[2];

    /* If using Serial I/O, send via serial */
    if (UseSerialForDebug && SerialIoProtocol) {
        UefiSerialPutByte(ByteToSend);
    }

    /* Also output to console if available (dual output) */
    if (GlobalSystemTable && GlobalSystemTable->ConOut) {
        /* Convert byte to wide character */
        WideChar[0] = (CHAR16)ByteToSend;
        WideChar[1] = 0;

        /* Special handling for newline */
        if (ByteToSend == '\n') {
            WideChar[0] = L'\r';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
            WideChar[0] = L'\n';
        }

        /* Output the character */
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
    }
}

BOOLEAN Rs232PortInUse(PUCHAR Base)
{
    /* Not applicable for UEFI Serial I/O */
    (void)Base;
    return UseSerialForDebug;
}

/* Get command line options helper */
PCSTR GetCommandLineOptions(VOID)
{
    /* This would normally parse the LoadOptions from EFI_LOADED_IMAGE_PROTOCOL */
    /* For now, return NULL - can be enhanced to parse actual options */
    return NULL;
}

#endif /* _M_ARM64 && UEFIBOOT */