/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Debugger Communication
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* Debug packet types */
#define PACKET_TYPE_UNUSED          0
#define PACKET_TYPE_KD_STATE_CHANGE 1
#define PACKET_TYPE_KD_STATE_MANIP  2
#define PACKET_TYPE_KD_DEBUG_IO     3
#define PACKET_TYPE_KD_TRACE_IO     4
#define PACKET_TYPE_KD_CONTROL      5
#define PACKET_TYPE_KD_FILE_IO      6
#define PACKET_TYPE_MAX             7

/* Packet leader bytes */
#define PACKET_LEADER_BYTE          0x30
#define PACKET_LEADER_BYTE_CONTROL  0x69
#define CONTROL_PACKET_LEADER       0x69696969
#define DATA_PACKET_LEADER          0x30303030
#define BREAKIN_PACKET_BYTE         0x62

/* Packet trailer */
#define PACKET_TRAILER_BYTE         0xAA

/* Maximum packet sizes */
#define KD_MAX_PACKET_SIZE          4096
#define KD_MAX_STATE_CHANGE_SIZE    1024
#define KD_MAX_MANIP_SIZE           4096

/* Communication ports */
#define DEBUG_COM_PORT_1            0x3F8
#define DEBUG_COM_PORT_2            0x2F8
#define DEBUG_COM_PORT_3            0x3E8
#define DEBUG_COM_PORT_4            0x2E8

/* Retry parameters */
#define KD_RETRY_COUNT              5
#define KD_TIMEOUT_MS               1000

/* ARM64 UART registers (PL011) */
#define UART_DR                     0x000  /* Data Register */
#define UART_RSR                    0x004  /* Receive Status Register */
#define UART_FR                     0x018  /* Flag Register */
#define UART_ILPR                   0x020  /* IrDA Low-Power Counter */
#define UART_IBRD                   0x024  /* Integer Baud Rate */
#define UART_FBRD                   0x028  /* Fractional Baud Rate */
#define UART_LCR_H                  0x02C  /* Line Control Register */
#define UART_CR                     0x030  /* Control Register */
#define UART_IFLS                   0x034  /* Interrupt FIFO Level Select */
#define UART_IMSC                   0x038  /* Interrupt Mask Set/Clear */
#define UART_RIS                    0x03C  /* Raw Interrupt Status */
#define UART_MIS                    0x040  /* Masked Interrupt Status */
#define UART_ICR                    0x044  /* Interrupt Clear */
#define UART_DMACR                  0x048  /* DMA Control */

/* UART Flag Register bits */
#define UART_FR_CTS                 (1 << 0)
#define UART_FR_DSR                 (1 << 1)
#define UART_FR_DCD                 (1 << 2)
#define UART_FR_BUSY                (1 << 3)
#define UART_FR_RXFE                (1 << 4)  /* RX FIFO Empty */
#define UART_FR_TXFF                (1 << 5)  /* TX FIFO Full */
#define UART_FR_RXFF                (1 << 6)  /* RX FIFO Full */
#define UART_FR_TXFE                (1 << 7)  /* TX FIFO Empty */

/* UART Control Register bits */
#define UART_CR_UARTEN              (1 << 0)  /* UART Enable */
#define UART_CR_TXE                 (1 << 8)  /* Transmit Enable */
#define UART_CR_RXE                 (1 << 9)  /* Receive Enable */

/* DATA STRUCTURES ************************************************************/

/* Debug packet header */
typedef struct _KD_PACKET_HEADER
{
    ULONG PacketLeader;
    USHORT PacketType;
    USHORT ByteCount;
    ULONG PacketId;
    ULONG Checksum;
} KD_PACKET_HEADER, *PKD_PACKET_HEADER;

/* Control packet */
typedef struct _KD_CONTROL_PACKET
{
    ULONG PacketLeader;
    ULONG PacketType;
    ULONG PacketId;
} KD_CONTROL_PACKET, *PKD_CONTROL_PACKET;

/* Debug I/O structure */
typedef struct _KD_DEBUG_IO
{
    ULONG ApiNumber;
    USHORT ProcessorLevel;
    USHORT Processor;
    union
    {
        struct
        {
            ULONG LengthOfString;
            ULONG LengthOfPromptString;
        } PrintString;
        struct
        {
            ULONG LengthOfPromptString;
            ULONG LengthOfStringRead;
        } GetString;
    } u;
} KD_DEBUG_IO, *PKD_DEBUG_IO;

/* Communication port information */
typedef struct _KD_COM_PORT
{
    PVOID BaseAddress;
    ULONG BaudRate;
    UCHAR DataBits;
    UCHAR StopBits;
    UCHAR Parity;
    BOOLEAN Initialized;
    KSPIN_LOCK Lock;
} KD_COM_PORT, *PKD_COM_PORT;

/* GLOBALS ********************************************************************/

static KD_COM_PORT KdComPort = {0};
static ULONG KdPacketId = 0;
static ULONG KdNextPacketId = 1;
static BOOLEAN KdComPortInUse = FALSE;
static UCHAR KdReceiveBuffer[KD_MAX_PACKET_SIZE];
static UCHAR KdSendBuffer[KD_MAX_PACKET_SIZE];

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Read from UART register
 */
static
ULONG
KdpReadUartRegister(
    IN ULONG Offset)
{
    if (!KdComPort.BaseAddress)
        return 0;

    return READ_REGISTER_ULONG((PULONG)((PUCHAR)KdComPort.BaseAddress + Offset));
}

/*
 * @brief Write to UART register
 */
static
VOID
KdpWriteUartRegister(
    IN ULONG Offset,
    IN ULONG Value)
{
    if (!KdComPort.BaseAddress)
        return;

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)KdComPort.BaseAddress + Offset), Value);
}

/*
 * @brief Initialize PL011 UART for ARM64
 */
static
BOOLEAN
KdpInitializeUart(
    IN PVOID BaseAddress,
    IN ULONG BaudRate)
{
    ULONG Divider;
    ULONG Remainder;
    ULONG Fraction;
    ULONG Control;

    DPRINT("Initializing PL011 UART at %p, baud rate %lu\n", BaseAddress, BaudRate);

    KdComPort.BaseAddress = BaseAddress;
    KdComPort.BaudRate = BaudRate;

    /* Disable UART during configuration */
    KdpWriteUartRegister(UART_CR, 0);

    /* Clear pending interrupts */
    KdpWriteUartRegister(UART_ICR, 0x7FF);

    /* Calculate baud rate divisors
     * BaudRate = UARTCLK / (16 * Divisor)
     * Assuming UARTCLK = 48MHz for now (platform specific)
     */
    ULONG UartClock = 48000000;  /* TODO: Get from platform */
    Divider = UartClock / (16 * BaudRate);
    Remainder = UartClock % (16 * BaudRate);
    Fraction = ((Remainder * 64) + (8 * BaudRate)) / (16 * BaudRate);

    /* Set baud rate */
    KdpWriteUartRegister(UART_IBRD, Divider);
    KdpWriteUartRegister(UART_FBRD, Fraction);

    /* Set line control: 8 data bits, no parity, 1 stop bit, FIFOs enabled */
    KdpWriteUartRegister(UART_LCR_H, (3 << 5) | (1 << 4));  /* 8N1 + FIFO */

    /* Enable UART, TX and RX */
    Control = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
    KdpWriteUartRegister(UART_CR, Control);

    KdComPort.DataBits = 8;
    KdComPort.StopBits = 1;
    KdComPort.Parity = 0;
    KdComPort.Initialized = TRUE;

    DPRINT("PL011 UART initialized successfully\n");
    return TRUE;
}

/*
 * @brief Send byte via UART
 */
static
VOID
KdpSendByte(
    IN UCHAR Byte)
{
    ULONG Flags;
    ULONG Timeout = 100000;

    if (!KdComPort.Initialized)
        return;

    /* Wait for TX FIFO to have space */
    do
    {
        Flags = KdpReadUartRegister(UART_FR);
        if (--Timeout == 0)
        {
            DPRINT1("UART TX timeout\n");
            return;
        }
    } while (Flags & UART_FR_TXFF);

    /* Send byte */
    KdpWriteUartRegister(UART_DR, Byte);
}

/*
 * @brief Receive byte via UART
 */
static
BOOLEAN
KdpReceiveByte(
    OUT PUCHAR Byte)
{
    ULONG Flags;
    ULONG Data;

    if (!KdComPort.Initialized)
        return FALSE;

    /* Check if data available */
    Flags = KdpReadUartRegister(UART_FR);
    if (Flags & UART_FR_RXFE)
    {
        /* RX FIFO empty */
        return FALSE;
    }

    /* Read data */
    Data = KdpReadUartRegister(UART_DR);
    *Byte = (UCHAR)(Data & 0xFF);

    return TRUE;
}

/*
 * @brief Wait for and receive byte with timeout
 */
static
BOOLEAN
KdpReceiveByteWithTimeout(
    OUT PUCHAR Byte,
    IN ULONG TimeoutMs)
{
    LARGE_INTEGER StartTime, CurrentTime, Timeout;

    /* Get start time */
    StartTime = KeQueryPerformanceCounter(NULL);

    /* Calculate timeout in performance counter units */
    Timeout.QuadPart = TimeoutMs * 10000;  /* Convert to 100ns units */

    while (TRUE)
    {
        /* Try to receive byte */
        if (KdpReceiveByte(Byte))
        {
            return TRUE;
        }

        /* Check timeout */
        CurrentTime = KeQueryPerformanceCounter(NULL);
        if ((CurrentTime.QuadPart - StartTime.QuadPart) > Timeout.QuadPart)
        {
            return FALSE;
        }

        /* Yield to other threads */
        KeStallExecutionProcessor(1);
    }
}

/*
 * @brief Calculate packet checksum
 */
static
ULONG
KdpCalculateChecksum(
    IN PVOID Buffer,
    IN ULONG Length)
{
    PUCHAR Data = (PUCHAR)Buffer;
    ULONG Checksum = 0;
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        Checksum += Data[i];
    }

    return Checksum;
}

/*
 * @brief Send packet
 */
static
BOOLEAN
KdpSendPacket(
    IN ULONG PacketType,
    IN PVOID Header,
    IN ULONG HeaderSize,
    IN PVOID Data,
    IN ULONG DataSize)
{
    KD_PACKET_HEADER PacketHeader;
    ULONG i;
    PUCHAR HeaderBytes;
    PUCHAR DataBytes;

    if (!KdComPort.Initialized)
        return FALSE;

    DPRINT("Sending packet: Type=%lu, HeaderSize=%lu, DataSize=%lu\n",
           PacketType, HeaderSize, DataSize);

    /* Build packet header */
    PacketHeader.PacketLeader = DATA_PACKET_LEADER;
    PacketHeader.PacketType = (USHORT)PacketType;
    PacketHeader.ByteCount = (USHORT)DataSize;
    PacketHeader.PacketId = KdNextPacketId++;
    PacketHeader.Checksum = KdpCalculateChecksum(Data, DataSize);

    /* Send packet leader */
    for (i = 0; i < 4; i++)
    {
        KdpSendByte((UCHAR)(PacketHeader.PacketLeader >> (i * 8)));
    }

    /* Send packet header */
    HeaderBytes = (PUCHAR)&PacketHeader.PacketType;
    for (i = 0; i < sizeof(PacketHeader) - 4; i++)
    {
        KdpSendByte(HeaderBytes[i]);
    }

    /* Send header data if present */
    if (Header && HeaderSize > 0)
    {
        HeaderBytes = (PUCHAR)Header;
        for (i = 0; i < HeaderSize; i++)
        {
            KdpSendByte(HeaderBytes[i]);
        }
    }

    /* Send packet data */
    if (Data && DataSize > 0)
    {
        DataBytes = (PUCHAR)Data;
        for (i = 0; i < DataSize; i++)
        {
            KdpSendByte(DataBytes[i]);
        }
    }

    /* Send trailer */
    KdpSendByte(PACKET_TRAILER_BYTE);

    return TRUE;
}

/*
 * @brief Receive packet
 */
static
BOOLEAN
KdpReceivePacket(
    OUT PULONG PacketType,
    OUT PVOID Header,
    IN ULONG HeaderSize,
    OUT PVOID Data,
    IN OUT PULONG DataSize)
{
    KD_PACKET_HEADER PacketHeader;
    UCHAR Byte;
    ULONG i;
    ULONG ReceivedChecksum;
    ULONG CalculatedChecksum;
    PUCHAR HeaderBytes;
    PUCHAR DataBytes;

    if (!KdComPort.Initialized)
        return FALSE;

    /* Wait for packet leader */
    for (i = 0; i < 4; i++)
    {
        if (!KdpReceiveByteWithTimeout(&Byte, KD_TIMEOUT_MS))
        {
            return FALSE;
        }

        if (Byte == BREAKIN_PACKET_BYTE)
        {
            /* Break-in packet */
            *PacketType = PACKET_TYPE_KD_CONTROL;
            return TRUE;
        }

        ((PUCHAR)&PacketHeader.PacketLeader)[i] = Byte;
    }

    /* Verify packet leader */
    if (PacketHeader.PacketLeader != DATA_PACKET_LEADER &&
        PacketHeader.PacketLeader != CONTROL_PACKET_LEADER)
    {
        DPRINT1("Invalid packet leader: 0x%08lx\n", PacketHeader.PacketLeader);
        return FALSE;
    }

    /* Receive rest of packet header */
    HeaderBytes = (PUCHAR)&PacketHeader.PacketType;
    for (i = 0; i < sizeof(PacketHeader) - 4; i++)
    {
        if (!KdpReceiveByteWithTimeout(&HeaderBytes[i], KD_TIMEOUT_MS))
        {
            return FALSE;
        }
    }

    /* Validate packet size */
    if (PacketHeader.ByteCount > KD_MAX_PACKET_SIZE)
    {
        DPRINT1("Packet too large: %u bytes\n", PacketHeader.ByteCount);
        return FALSE;
    }

    /* Receive header data if expected */
    if (Header && HeaderSize > 0)
    {
        HeaderBytes = (PUCHAR)Header;
        for (i = 0; i < HeaderSize; i++)
        {
            if (!KdpReceiveByteWithTimeout(&HeaderBytes[i], KD_TIMEOUT_MS))
            {
                return FALSE;
            }
        }
    }

    /* Receive packet data */
    if (Data && PacketHeader.ByteCount > 0)
    {
        DataBytes = (PUCHAR)Data;
        for (i = 0; i < PacketHeader.ByteCount && i < *DataSize; i++)
        {
            if (!KdpReceiveByteWithTimeout(&DataBytes[i], KD_TIMEOUT_MS))
            {
                return FALSE;
            }
        }
    }

    /* Receive trailer */
    if (!KdpReceiveByteWithTimeout(&Byte, KD_TIMEOUT_MS) || Byte != PACKET_TRAILER_BYTE)
    {
        DPRINT1("Invalid packet trailer\n");
        return FALSE;
    }

    /* Verify checksum */
    ReceivedChecksum = PacketHeader.Checksum;
    CalculatedChecksum = KdpCalculateChecksum(Data, PacketHeader.ByteCount);
    if (ReceivedChecksum != CalculatedChecksum)
    {
        DPRINT1("Checksum mismatch: received=0x%08lx, calculated=0x%08lx\n",
               ReceivedChecksum, CalculatedChecksum);
        return FALSE;
    }

    /* Return packet information */
    *PacketType = PacketHeader.PacketType;
    *DataSize = PacketHeader.ByteCount;
    KdPacketId = PacketHeader.PacketId;

    DPRINT("Received packet: Type=%lu, Size=%lu, Id=%lu\n",
           *PacketType, *DataSize, KdPacketId);

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize kernel debugger communication
 * @implemented
 */
NTSTATUS
NTAPI
KdDebuggerInitialize0(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    PVOID UartBase = NULL;
    ULONG BaudRate = 115200;

    DPRINT("Initializing ARM64 kernel debugger communication\n");

    /* Initialize spinlock */
    KeInitializeSpinLock(&KdComPort.Lock);

    /* Get UART configuration from loader block */
    if (LoaderBlock && LoaderBlock->Extension)
    {
        /* TODO: Extract UART base address from loader block
         * This should come from firmware/UEFI or device tree
         */
        /* For now, use a common ARM64 UART address */
        UartBase = (PVOID)0x09000000;  /* Example: QEMU virt machine PL011 */
    }

    if (!UartBase)
    {
        DPRINT1("No UART base address provided\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Map UART registers */
    PHYSICAL_ADDRESS PhysicalAddress;
    PhysicalAddress.QuadPart = (ULONG_PTR)UartBase;
    UartBase = MmMapIoSpace(PhysicalAddress, PAGE_SIZE, MmNonCached);
    if (!UartBase)
    {
        DPRINT1("Failed to map UART registers\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize UART */
    if (!KdpInitializeUart(UartBase, BaudRate))
    {
        MmUnmapIoSpace(UartBase, PAGE_SIZE);
        return STATUS_DEVICE_NOT_READY;
    }

    DPRINT("Kernel debugger communication initialized\n");
    return STATUS_SUCCESS;
}

/*
 * @brief Secondary initialization
 * @implemented
 */
NTSTATUS
NTAPI
KdDebuggerInitialize1(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* Perform any secondary initialization */
    DPRINT("Kernel debugger communication phase 1 initialized\n");

    /* Send initial connection packet */
    KdpSendConnectionPacket();

    return STATUS_SUCCESS;
}

/*
 * @brief Send connection packet
 * @implemented
 */
VOID
NTAPI
KdpSendConnectionPacket(VOID)
{
    UCHAR ConnectionString[] = "ReactOS ARM64 Kernel Debugger";

    DPRINT("Sending connection packet\n");

    /* Send connection string */
    KdpSendPacket(PACKET_TYPE_KD_DEBUG_IO,
                  NULL, 0,
                  ConnectionString, sizeof(ConnectionString));
}

/*
 * @brief Send string to debugger
 * @implemented
 */
VOID
NTAPI
KdpSendString(
    IN PSTRING String)
{
    KD_DEBUG_IO DebugIo;
    KIRQL OldIrql;

    if (!KdComPort.Initialized || !String || String->Length == 0)
        return;

    /* Acquire lock */
    KeAcquireSpinLock(&KdComPort.Lock, &OldIrql);

    /* Build debug I/O structure */
    DebugIo.ApiNumber = DbgKdPrintStringApi;
    DebugIo.ProcessorLevel = KeProcessorLevel;
    DebugIo.Processor = KeGetCurrentProcessorNumber();
    DebugIo.u.PrintString.LengthOfString = String->Length;
    DebugIo.u.PrintString.LengthOfPromptString = 0;

    /* Send packet */
    KdpSendPacket(PACKET_TYPE_KD_DEBUG_IO,
                  &DebugIo, sizeof(DebugIo),
                  String->Buffer, String->Length);

    /* Release lock */
    KeReleaseSpinLock(&KdComPort.Lock, OldIrql);
}

/*
 * @brief Prompt for string from debugger
 * @implemented
 */
ULONG
NTAPI
KdpPromptString(
    IN PSTRING PromptString,
    OUT PSTRING ResponseString)
{
    KD_DEBUG_IO DebugIo;
    ULONG PacketType;
    ULONG ResponseLength;
    KIRQL OldIrql;

    if (!KdComPort.Initialized)
        return 0;

    /* Acquire lock */
    KeAcquireSpinLock(&KdComPort.Lock, &OldIrql);

    /* Build debug I/O structure */
    DebugIo.ApiNumber = DbgKdGetStringApi;
    DebugIo.ProcessorLevel = KeProcessorLevel;
    DebugIo.Processor = KeGetCurrentProcessorNumber();
    DebugIo.u.GetString.LengthOfPromptString = PromptString->Length;
    DebugIo.u.GetString.LengthOfStringRead = ResponseString->MaximumLength;

    /* Send prompt packet */
    KdpSendPacket(PACKET_TYPE_KD_DEBUG_IO,
                  &DebugIo, sizeof(DebugIo),
                  PromptString->Buffer, PromptString->Length);

    /* Wait for response */
    ResponseLength = ResponseString->MaximumLength;
    if (KdpReceivePacket(&PacketType,
                         &DebugIo, sizeof(DebugIo),
                         ResponseString->Buffer, &ResponseLength))
    {
        ResponseString->Length = (USHORT)ResponseLength;
    }
    else
    {
        ResponseString->Length = 0;
        ResponseLength = 0;
    }

    /* Release lock */
    KeReleaseSpinLock(&KdComPort.Lock, OldIrql);

    return ResponseLength;
}

/*
 * @brief Poll for break-in packet
 * @implemented
 */
BOOLEAN
NTAPI
KdPollBreakIn(VOID)
{
    UCHAR Byte;

    if (!KdComPort.Initialized)
        return FALSE;

    /* Check for break-in byte without blocking */
    if (KdpReceiveByte(&Byte))
    {
        if (Byte == BREAKIN_PACKET_BYTE)
        {
            DPRINT("Break-in detected\n");
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * @brief Send control packet
 * @implemented
 */
VOID
NTAPI
KdSendPacket(
    IN ULONG PacketType,
    IN PSTRING MessageHeader,
    IN PSTRING MessageData OPTIONAL,
    IN OUT PKD_CONTEXT Context)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Context);

    if (!KdComPort.Initialized)
        return;

    /* Acquire lock */
    KeAcquireSpinLock(&KdComPort.Lock, &OldIrql);

    /* Send packet */
    KdpSendPacket(PacketType,
                  MessageHeader->Buffer, MessageHeader->Length,
                  MessageData ? MessageData->Buffer : NULL,
                  MessageData ? MessageData->Length : 0);

    /* Release lock */
    KeReleaseSpinLock(&KdComPort.Lock, OldIrql);
}

/*
 * @brief Receive packet
 * @implemented
 */
ULONG
NTAPI
KdReceivePacket(
    IN ULONG PacketType,
    OUT PSTRING MessageHeader,
    OUT PSTRING MessageData,
    OUT PULONG DataLength,
    IN OUT PKD_CONTEXT Context)
{
    ULONG ReceivedType;
    ULONG ReceivedLength;
    KIRQL OldIrql;
    BOOLEAN Success;

    UNREFERENCED_PARAMETER(PacketType);
    UNREFERENCED_PARAMETER(Context);

    if (!KdComPort.Initialized)
        return KDP_PACKET_TIMEOUT;

    /* Acquire lock */
    KeAcquireSpinLock(&KdComPort.Lock, &OldIrql);

    /* Receive packet */
    ReceivedLength = MessageData->MaximumLength;
    Success = KdpReceivePacket(&ReceivedType,
                               MessageHeader->Buffer, MessageHeader->MaximumLength,
                               MessageData->Buffer, &ReceivedLength);

    /* Release lock */
    KeReleaseSpinLock(&KdComPort.Lock, OldIrql);

    if (!Success)
    {
        return KDP_PACKET_TIMEOUT;
    }

    /* Update lengths */
    MessageData->Length = (USHORT)ReceivedLength;
    if (DataLength)
    {
        *DataLength = ReceivedLength;
    }

    /* Check packet type */
    if (ReceivedType == PACKET_TYPE_KD_CONTROL)
    {
        return KDP_PACKET_RESEND;
    }

    return KDP_PACKET_RECEIVED;
}

/* EOF */