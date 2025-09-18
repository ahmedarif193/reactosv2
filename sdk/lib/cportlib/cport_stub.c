/*
 * ARM64 cportlib stub implementation
 * Provides no-op serial port helpers so kernel and boot code can link on ARM64.
 */

#include <ntdef.h>
#include <ntstatus.h>
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#include <reactos/libs/cportlib/cportlib.h>

VOID NTAPI
CpEnableFifo(IN PUCHAR Address, IN BOOLEAN Enable)
{
    (void)Address; (void)Enable;
}

VOID NTAPI
CpSetBaud(IN PCPPORT Port, IN ULONG BaudRate)
{
    if (Port) Port->BaudRate = BaudRate;
}

NTSTATUS NTAPI
CpInitialize(IN PCPPORT Port, IN PUCHAR Address, IN ULONG BaudRate)
{
    if (!Port) return STATUS_INVALID_PARAMETER;
    Port->Address = Address;
    Port->BaudRate = BaudRate;
    Port->Flags = 0;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI
CpDoesPortExist(IN PUCHAR Address)
{
    (void)Address; return FALSE;
}

UCHAR NTAPI
CpReadLsr(IN PCPPORT Port, IN UCHAR ExpectedValue)
{
    (void)Port; (void)ExpectedValue; return 0;
}

USHORT NTAPI
CpGetByte(IN  PCPPORT Port, OUT PUCHAR Byte, IN  BOOLEAN Wait, IN  BOOLEAN Poll)
{
    (void)Port; (void)Byte; (void)Wait; (void)Poll; return CP_GET_NODATA;
}

VOID NTAPI
CpPutByte(IN PCPPORT Port, IN UCHAR Byte)
{
    (void)Port; (void)Byte;
}
