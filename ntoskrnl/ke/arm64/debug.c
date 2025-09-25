/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 early debug output support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * @brief Output a string to ARM64 serial port for early debugging
 *
 * This function provides early boot debug output before the full
 * kernel debugger is initialized.
 */
VOID
Arm64SerialPutString(
    _In_ PCHAR String)
{
    volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

    if (!String) return;

    while (*String)
    {
        /* Write character to PL011 UART data register */
        *Pl011Dr = (UCHAR)(*String);

        /* Simple delay for UART transmission */
        for (volatile ULONG Delay = 0; Delay < 1000; Delay++)
        {
            __asm__ __volatile__("nop");
        }

        String++;
    }
}

/*
 * @brief Output a single character to ARM64 serial port
 */
VOID
Arm64SerialPutChar(
    _In_ CHAR Character)
{
    volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

    /* Write character to PL011 UART data register */
    *Pl011Dr = (UCHAR)Character;

    /* Simple delay for UART transmission */
    for (volatile ULONG Delay = 0; Delay < 1000; Delay++)
    {
        __asm__ __volatile__("nop");
    }
}

/*
 * @brief Output a hex value to ARM64 serial port
 */
VOID
Arm64SerialPutHex64(
    _In_ ULONGLONG Value)
{
    CHAR HexBuffer[20];
    INT Pos = 0;
    INT Shift;

    /* Build hex string */
    HexBuffer[Pos++] = '0';
    HexBuffer[Pos++] = 'x';

    for (Shift = 60; Shift >= 0; Shift -= 4)
    {
        UCHAR Nibble = (UCHAR)((Value >> Shift) & 0xF);
        HexBuffer[Pos++] = (Nibble < 10) ? ('0' + Nibble) : ('A' + Nibble - 10);
    }

    HexBuffer[Pos] = '\0';

    /* Output the hex string */
    Arm64SerialPutString(HexBuffer);
}

/*
 * @brief Output a carriage return and line feed
 */
VOID
Arm64SerialCrLf(VOID)
{
    Arm64SerialPutChar('\r');
    Arm64SerialPutChar('\n');
}