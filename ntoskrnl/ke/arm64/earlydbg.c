/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Early Boot Debug Printing Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <stdarg.h>

/* DEFINITIONS ***************************************************************/

/* PL011 UART Base Address for QEMU ARM64 Virtual Machine */
#define ARM64_UART_BASE     0x09000000U
#define PL011_DR           (ARM64_UART_BASE + 0x00)  /* Data Register */
#define PL011_FR           (ARM64_UART_BASE + 0x18)  /* Flag Register */
#define PL011_FR_TXFF      0x20                       /* Transmit FIFO Full */
#define PL011_FR_RXFE      0x10                       /* Receive FIFO Empty */
#define PL011_FR_BUSY      0x08                       /* UART Busy */

/* Simple delay loops for early boot when timing services aren't available */
#define UART_DELAY_LOOPS   1000

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Simple delay function for UART operations
 * @param Loops Number of delay loops
 */
static VOID
KiEarlyDelay(ULONG Loops)
{
    volatile ULONG i;
    for (i = 0; i < Loops; i++)
    {
        /* Memory barrier to prevent optimization */
        __asm__ __volatile__("" ::: "memory");
    }
}

/**
 * @brief Write a single character to the UART
 * @param Ch Character to write
 */
static VOID
KiEarlyPutChar(CHAR Ch)
{
    volatile ULONG *DataReg = (volatile ULONG *)PL011_DR;
    volatile ULONG *FlagReg = (volatile ULONG *)PL011_FR;

    /* Wait until transmit FIFO is not full */
    while ((*FlagReg & PL011_FR_TXFF) != 0)
    {
        KiEarlyDelay(10);
    }

    /* Write character to data register */
    *DataReg = (ULONG)Ch;

    /* Small delay for transmission */
    KiEarlyDelay(UART_DELAY_LOOPS);
}

/**
 * @brief Write a string to the UART
 * @param String Null-terminated string to write
 */
static VOID
KiEarlyPutString(const CHAR *String)
{
    if (!String)
        return;

    while (*String)
    {
        /* Handle line feed - send CR+LF for proper terminal display */
        if (*String == '\n')
        {
            KiEarlyPutChar('\r');
        }
        KiEarlyPutChar(*String);
        String++;
    }
}

/**
 * @brief Write a hex digit to UART
 * @param Digit Hex digit value (0-15)
 */
static VOID
KiEarlyPutHexDigit(ULONG Digit)
{
    if (Digit < 10)
    {
        KiEarlyPutChar('0' + Digit);
    }
    else
    {
        KiEarlyPutChar('A' + (Digit - 10));
    }
}

/**
 * @brief Write a hex number to UART
 * @param Value Value to write
 * @param Digits Number of hex digits to display
 */
static VOID
KiEarlyPutHex(ULONG64 Value, ULONG Digits)
{
    LONG i;

    KiEarlyPutString("0x");

    /* Print hex digits from most significant to least */
    for (i = (Digits - 1) * 4; i >= 0; i -= 4)
    {
        KiEarlyPutHexDigit((Value >> i) & 0xF);
    }
}

/**
 * @brief Write a decimal number to UART
 * @param Value Value to write
 */
static VOID
KiEarlyPutDecimal(ULONG64 Value)
{
    CHAR Buffer[21];  /* Max 20 digits for 64-bit + null */
    CHAR *p = &Buffer[20];

    *p = '\0';

    /* Handle zero specially */
    if (Value == 0)
    {
        KiEarlyPutChar('0');
        return;
    }

    /* Build string from least significant digit */
    while (Value > 0)
    {
        p--;
        *p = '0' + (Value % 10);
        Value /= 10;
    }

    KiEarlyPutString(p);
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Early boot debug print function with format string support
 *
 * This function provides printf-like formatting for early boot debugging
 * before the full kernel debug infrastructure is available. It writes
 * directly to the ARM64 PL011 UART.
 *
 * Supported format specifiers:
 *   %s  - String
 *   %c  - Character
 *   %d  - Signed decimal
 *   %u  - Unsigned decimal
 *   %x  - Hexadecimal (lowercase)
 *   %X  - Hexadecimal (uppercase)
 *   %p  - Pointer
 *   %ld - Long decimal
 *   %lu - Unsigned long decimal
 *   %lx - Long hexadecimal
 *   %lld - Long long decimal
 *   %llu - Unsigned long long decimal
 *   %llx - Long long hexadecimal
 *   %%  - Literal %
 *
 * @param Format Printf-style format string
 * @param Args Variable argument list
 */
VOID
NTAPI
KiEarlyDebugPrintV(
    IN const CHAR *Format,
    IN va_list Args)
{
    const CHAR *p;
    CHAR Ch;

    if (!Format)
        return;

    for (p = Format; *p; p++)
    {
        if (*p != '%')
        {
            /* Regular character */
            if (*p == '\n')
            {
                KiEarlyPutChar('\r');
            }
            KiEarlyPutChar(*p);
            continue;
        }

        /* Format specifier */
        p++;
        if (*p == '\0')
            break;

        /* Handle format specifiers */
        switch (*p)
        {
            case '%':
                KiEarlyPutChar('%');
                break;

            case 's':
            {
                const CHAR *str = va_arg(Args, const CHAR *);
                if (str)
                {
                    KiEarlyPutString(str);
                }
                else
                {
                    KiEarlyPutString("(null)");
                }
                break;
            }

            case 'c':
                Ch = (CHAR)va_arg(Args, int);
                KiEarlyPutChar(Ch);
                break;

            case 'd':
            {
                INT Value = va_arg(Args, INT);
                if (Value < 0)
                {
                    KiEarlyPutChar('-');
                    Value = -Value;
                }
                KiEarlyPutDecimal((ULONG64)Value);
                break;
            }

            case 'u':
            {
                UINT Value = va_arg(Args, UINT);
                KiEarlyPutDecimal((ULONG64)Value);
                break;
            }

            case 'x':
            case 'X':
            {
                UINT Value = va_arg(Args, UINT);
                KiEarlyPutHex((ULONG64)Value, 8);
                break;
            }

            case 'p':
            {
                PVOID Ptr = va_arg(Args, PVOID);
                KiEarlyPutHex((ULONG64)Ptr, 16);
                break;
            }

            case 'l':
            {
                /* Long format specifiers */
                p++;
                if (*p == '\0')
                    break;

                if (*p == 'l')
                {
                    /* Long long format */
                    p++;
                    if (*p == '\0')
                        break;

                    switch (*p)
                    {
                        case 'd':
                        {
                            LONGLONG Value = va_arg(Args, LONGLONG);
                            if (Value < 0)
                            {
                                KiEarlyPutChar('-');
                                Value = -Value;
                            }
                            KiEarlyPutDecimal((ULONG64)Value);
                            break;
                        }

                        case 'u':
                        {
                            ULONGLONG Value = va_arg(Args, ULONGLONG);
                            KiEarlyPutDecimal(Value);
                            break;
                        }

                        case 'x':
                        case 'X':
                        {
                            ULONGLONG Value = va_arg(Args, ULONGLONG);
                            KiEarlyPutHex(Value, 16);
                            break;
                        }

                        default:
                            KiEarlyPutChar('%');
                            KiEarlyPutChar('l');
                            KiEarlyPutChar('l');
                            KiEarlyPutChar(*p);
                            break;
                    }
                }
                else
                {
                    /* Long format */
                    switch (*p)
                    {
                        case 'd':
                        {
                            LONG Value = va_arg(Args, LONG);
                            if (Value < 0)
                            {
                                KiEarlyPutChar('-');
                                Value = -Value;
                            }
                            KiEarlyPutDecimal((ULONG64)Value);
                            break;
                        }

                        case 'u':
                        {
                            ULONG Value = va_arg(Args, ULONG);
                            KiEarlyPutDecimal((ULONG64)Value);
                            break;
                        }

                        case 'x':
                        case 'X':
                        {
                            ULONG Value = va_arg(Args, ULONG);
                            KiEarlyPutHex((ULONG64)Value, 8);
                            break;
                        }

                        default:
                            KiEarlyPutChar('%');
                            KiEarlyPutChar('l');
                            KiEarlyPutChar(*p);
                            break;
                    }
                }
                break;
            }

            default:
                /* Unknown format specifier - just print it */
                KiEarlyPutChar('%');
                KiEarlyPutChar(*p);
                break;
        }
    }
}

/**
 * @brief Early boot debug print function (varargs wrapper)
 *
 * This is the main function to call for early boot debugging.
 * Usage: KiEarlyKernelDebugPrint("Value: %d, Ptr: %p\n", value, ptr);
 *
 * Note: Renamed from KiEarlyDebugPrint to avoid conflict with boot.S
 *
 * @param Format Printf-style format string
 * @param ... Variable arguments
 */
VOID
NTAPI
KiEarlyKernelDebugPrint(
    IN const CHAR *Format,
    IN ...)
{
    va_list Args;

    va_start(Args, Format);
    KiEarlyDebugPrintV(Format, Args);
    va_end(Args);
}

/**
 * @brief Write a simple string to early debug output
 *
 * Convenience function for writing plain strings without formatting.
 *
 * @param String String to write
 */
VOID
NTAPI
KiEarlyDebugString(
    IN const CHAR *String)
{
    KiEarlyPutString(String);
}

/**
 * @brief Write a hex value to early debug output
 *
 * Convenience function for dumping hex values.
 *
 * @param Label Descriptive label
 * @param Value Value to display
 */
VOID
NTAPI
KiEarlyDebugHex(
    IN const CHAR *Label,
    IN ULONG64 Value)
{
    if (Label)
    {
        KiEarlyPutString(Label);
        KiEarlyPutString(": ");
    }
    KiEarlyPutHex(Value, 16);
    KiEarlyPutString("\n");
}

/* EOF */