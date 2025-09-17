/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Console output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#include <comm.h>

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern REACTOS_INTERNAL_BGCONTEXT framebufferData;
static unsigned CurrentCursorX = 0;
static unsigned CurrentCursorY = 0;
static unsigned CurrentAttr = 0x0f;
static EFI_INPUT_KEY Key;
static BOOLEAN ExtendedKey = FALSE;
static char ExtendedScanCode = 0;
static BOOLEAN KeyAvailable = FALSE;
/* Simple flag to stop using any UEFI services after ExitBootServices */
BOOLEAN UefiBootServicesActive = TRUE;

/* AGENT-MODIFIED: Add GOP console function declarations */
extern VOID UefiGopConsolePutChar(CHAR Ch);
extern VOID UefiGopConsolePutString(PCSTR String);
extern VOID UefiGopConsoleClear(VOID);
extern VOID UefiGopConsoleSetCursor(UINT32 X, UINT32 Y);
extern BOOLEAN UefiGopConsoleIsInitialized(VOID);

/* FUNCTIONS ******************************************************************/

#ifdef _M_ARM64
/* Initialize serial port for dual console output */
static BOOLEAN SerialInitializedForConsole = FALSE;

static VOID
EnsureSerialInitialized(VOID)
{
    if (!SerialInitializedForConsole)
    {
        Rs232PortInitialize(0, 115200);
        SerialInitializedForConsole = TRUE;
    }
}

/* Output wide string to both console and serial */
VOID
UefiConsPutString(PCWSTR String)
{
    if (!GlobalSystemTable || !GlobalSystemTable->ConOut)
        return;

    /* Ensure serial is initialized */
    EnsureSerialInitialized();

    /* Output to console */
    GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, (PWSTR)String);

    /* Also output to serial */
    while (*String)
    {
        CHAR16 Ch = *String++;
        if (Ch == L'\r')
        {
            Rs232PortPutByte('\r');
        }
        else if (Ch == L'\n')
        {
            Rs232PortPutByte('\n');
        }
        else if (Ch < 0x80)
        {
            /* ASCII character */
            Rs232PortPutByte((UCHAR)Ch);
        }
        /* Skip non-ASCII for now */
    }
}
#endif

VOID
UefiConsPutChar(int c)
{
    /* Do not print anything after ExitBootServices */
    if (!UefiBootServicesActive)
        return;

#ifdef _M_ARM64
    /* Output to both console and serial */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
    {
        CHAR16 WideChar[2];
        WideChar[1] = 0;

        /* Ensure serial is initialized */
        EnsureSerialInitialized();

        /* Send to serial */
        if (c == '\n')
        {
            Rs232PortPutByte('\r');
            Rs232PortPutByte('\n');
        }
        else
        {
            Rs232PortPutByte((UCHAR)c);
        }

        /* Output to console */
        if (c == '\n')
        {
            WideChar[0] = L'\r';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
            WideChar[0] = L'\n';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
        }
        else
        {
            WideChar[0] = (CHAR16)c;
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
        }
        return;
    }
#else
    /* Early fallback to firmware text console until GOP framebuffer is ready */
    if (framebufferData.BaseAddress == 0 && GlobalSystemTable && GlobalSystemTable->ConOut)
    {
        CHAR16 WideChar[2];
        WideChar[1] = 0;
        if (c == '\n')
        {
            WideChar[0] = L'\r';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
            WideChar[0] = L'\n';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
        }
        else
        {
            WideChar[0] = (CHAR16)c;
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
        }
        return;
    }
#endif
    
    ULONG Width, Height, Unused;
    BOOLEAN NeedScroll;

    UefiVideoGetDisplaySize(&Width, &Height, &Unused);

    NeedScroll = (CurrentCursorY >= Height);
    if (NeedScroll)
    {
        UefiVideoScrollUp();
        --CurrentCursorY;
    }
    if (c == '\r')
    {
        CurrentCursorX = 0;
    }
    else if (c == '\n')
    {
        CurrentCursorX = 0;

        if (!NeedScroll)
            ++CurrentCursorY;
    }
    else if (c == '\t')
    {
        CurrentCursorX = (CurrentCursorX + 8) & ~7;
    }
    else
    {
        UefiVideoPutChar(c, CurrentAttr, CurrentCursorX, CurrentCursorY);
        CurrentCursorX++;
    }
    if (CurrentCursorX >= Width)
    {
        CurrentCursorX = 0;
        CurrentCursorY++;
    }
}

static
UCHAR
ConvertToBiosExtValue(UCHAR KeyIn)
{
    switch (KeyIn)
    {
        case SCAN_UP:
            return KEY_UP;
        case SCAN_DOWN:
            return KEY_DOWN;
        case SCAN_RIGHT:
            return KEY_RIGHT;
        case SCAN_LEFT:
            return KEY_LEFT;
        case SCAN_HOME:
            return KEY_HOME;
        case SCAN_END:
            return KEY_END;

        // case SCAN_INSERT:
        //     break;

        case SCAN_DELETE:
            return KEY_DELETE;

        // case SCAN_PAGE_UP:
        // case SCAN_PAGE_DOWN:
        //     break;

        case SCAN_F1:
            return KEY_F1;
        case SCAN_F2:
            return KEY_F2;
        case SCAN_F3:
            return KEY_F3;
        case SCAN_F4:
            return KEY_F4;
        case SCAN_F5:
            return KEY_F5;
        case SCAN_F6:
            return KEY_F6;
        case SCAN_F7:
            return KEY_F7;
        case SCAN_F8:
            return KEY_F8;
        case SCAN_F9:
            return KEY_F9;
        case SCAN_F10:
            return KEY_F10;
        case SCAN_ESC:
            return KEY_ESC;
    }
    return 0;
}

BOOLEAN
UefiConsKbHit(VOID)
{
    EFI_STATUS Status;
    
    /* Only read a new key if we don't have one buffered */
    if (!KeyAvailable && !ExtendedKey)
    {
        Status = GlobalSystemTable->ConIn->ReadKeyStroke(GlobalSystemTable->ConIn, &Key);
        if (Status == EFI_SUCCESS)
        {
            KeyAvailable = TRUE;
        }
        else
        {
            KeyAvailable = FALSE;
        }
    }
    
    return (KeyAvailable || ExtendedKey);
}

int
UefiConsGetCh(VOID)
{
    UCHAR KeyOutput = 0;
    EFI_STATUS Status;

    /* If an extended key press was detected the last time we were called
     * then return the scan code of that key. */
    if (ExtendedKey)
    {
        ExtendedKey = FALSE;
        return ExtendedScanCode;
    }

    /* Ensure we have a key available */
    if (!KeyAvailable)
    {
        /* Wait for a key if none is buffered */
        do
        {
            Status = GlobalSystemTable->ConIn->ReadKeyStroke(GlobalSystemTable->ConIn, &Key);
        } while (Status != EFI_SUCCESS);
        KeyAvailable = TRUE;
    }

    if (Key.UnicodeChar != 0)
    {
        KeyOutput = Key.UnicodeChar;
    }
    else if (Key.ScanCode != 0)
    {
        ExtendedKey = TRUE;
        ExtendedScanCode = ConvertToBiosExtValue(Key.ScanCode);
        KeyOutput = KEY_EXTENDED;
    }

    /* Clear the key buffer after consuming it */
    Key.UnicodeChar = 0;
    Key.ScanCode = 0;
    KeyAvailable = FALSE;
    
    return KeyOutput;
}

/* Function to mark boot services as exited: silence console */
VOID
UefiConsMarkBootServicesExited(VOID)
{
    UefiBootServicesActive = FALSE;
}
