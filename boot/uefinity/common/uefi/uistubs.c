/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UI Function Stubs for UEFI-only bootloader
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <ui.h>
#include <machine.h>
#include <uefildr.h>
#include <uefi/machuefi.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(UI);

VOID StallExecutionProcessor(ULONG Microseconds);

/* Global UI Variables */
ULONG UiScreenWidth = 80;
ULONG UiScreenHeight = 25;
UCHAR UiStatusBarFgColor = COLOR_BLACK;
UCHAR UiStatusBarBgColor = COLOR_CYAN;
UCHAR UiBackdropFgColor = COLOR_WHITE;
UCHAR UiBackdropBgColor = COLOR_BLUE;
UCHAR UiBackdropFillStyle = MEDIUM_FILL;
UCHAR UiTitleBoxFgColor = COLOR_WHITE;
UI_PROGRESS_BAR UiProgressBar = {0};

/* Console helpers ***********************************************************/

static VOID
UiConsolePutChar(
    _In_ BOOLEAN UseGop,
    _In_ CHAR Ch)
{
    if (UseGop)
    {
        UefiGopConsolePutChar(Ch);
    }
    else
    {
        MachConsPutChar(Ch);
    }
}

static VOID
UiConsolePutString(
    _In_ BOOLEAN UseGop,
    _In_opt_ PCSTR Text)
{
    if (!Text)
        return;

    while (*Text)
    {
        UiConsolePutChar(UseGop, *Text++);
    }
}

static VOID
UiConsoleNewLine(
    _In_ BOOLEAN UseGop)
{
    if (UseGop)
    {
        UiConsolePutChar(TRUE, '\n');
    }
    else
    {
        UiConsolePutChar(FALSE, '\r');
        UiConsolePutChar(FALSE, '\n');
    }
}

static VOID
UiConsoleWriteLine(
    _In_ BOOLEAN UseGop,
    _Inout_opt_ PUINT32 CurrentLine,
    _In_opt_ PCSTR Text)
{
    UiConsolePutString(UseGop, Text ? Text : "");
    UiConsoleNewLine(UseGop);
    if (CurrentLine)
        (*CurrentLine)++;
}

static VOID
UiConsoleWritePadded(
    _In_ BOOLEAN UseGop,
    _In_opt_ PCSTR Text,
    _In_ ULONG PadWidth)
{
    ULONG Printed = 0;

    if (Text)
    {
        UiConsolePutString(UseGop, Text);
        Printed = (ULONG)strlen(Text);
    }

    while (Printed < PadWidth)
    {
        UiConsolePutChar(UseGop, ' ');
        ++Printed;
    }
}

static VOID
UiConsoleClear(
    _In_ BOOLEAN UseGop)
{
    if (UseGop)
    {
        UefiGopConsoleClear();
    }
    else
    {
        MachVideoClearScreen((UiBackdropBgColor << 4) | UiBackdropFgColor);
    }
}

/* Basic UI Functions */
BOOLEAN
UiInitialize(BOOLEAN ShowUi)
{
    TRACE("UiInitialize(ShowUi=%s)\n", ShowUi ? "TRUE" : "FALSE");
    if (ShowUi && UefiGopConsoleIsInitialized())
    {
        UefiGopConsoleClear();
    }
    return TRUE;
}

VOID
UiUnInitialize(PCSTR BootText)
{
    TRACE("UiUnInitialize(\"%s\")\n", BootText ? BootText : "(null)");
    /* TODO: Cleanup UEFI console */
}

ULONG
UiGetScreenHeight(VOID)
{
    return UiScreenHeight;
}

UCHAR
UiGetMenuBgColor(VOID)
{
    return UiBackdropBgColor;
}

/* Drawing Functions */
VOID
UiDrawBackdrop(ULONG DrawHeight)
{
    TRACE("UiDrawBackdrop(DrawHeight=%lu)\n", DrawHeight);
    /* TODO: Draw background using UEFI console */
}

VOID
UiDrawText(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ PCSTR Text,
    _In_ UCHAR Attr)
{
    TRACE("UiDrawText(X=%lu, Y=%lu, Text=\"%s\", Attr=0x%02x)\n", X, Y, Text ? Text : "(null)", Attr);
    /* TODO: Draw text using UEFI console */
}

VOID
UiDrawProgressBarCenter(
    _In_ PCSTR ProgressText)
{
    TRACE("UiDrawProgressBarCenter(\"%s\")\n", ProgressText ? ProgressText : "(null)");
    /* TODO: Draw progress bar using UEFI console */
}

VOID
UiDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText)
{
    TRACE("UiDrawProgressBar(L=%lu,T=%lu,R=%lu,B=%lu,Text=\"%s\")\n",
          Left, Top, Right, Bottom, ProgressText ? ProgressText : "(null)");
}

VOID
UiSetProgressBarText(
    _In_ PCSTR ProgressText)
{
    TRACE("UiSetProgressBarText(\"%s\")\n", ProgressText ? ProgressText : "(null)");
}

VOID
UiTickProgressBar(
    _In_ ULONG SubPercentTimes100)
{
    TRACE("UiTickProgressBar(%lu)\n", SubPercentTimes100);
}

VOID
UiUpdateProgressBar(
    _In_ ULONG Percentage,
    _In_opt_ PCSTR ProgressText)
{
    TRACE("UiUpdateProgressBar(Percentage=%lu, ProgressText=\"%s\")\n",
          Percentage, ProgressText ? ProgressText : "(null)");
    /* TODO: Update progress bar using UEFI console */
}

VOID
UiIndicateProgress(VOID)
{
    TRACE("UiIndicateProgress()\n");
}

VOID
UiSetProgressBarSubset(
    _In_ ULONG StartPercentage,
    _In_ ULONG EndPercentage)
{
    TRACE("UiSetProgressBarSubset(Start=%lu, End=%lu)\n", StartPercentage, EndPercentage);
}

VOID
UiDrawStatusText(
    _In_ PCSTR StatusText)
{
    TRACE("UiDrawStatusText(\"%s\")\n", StatusText ? StatusText : "(null)");
}

VOID
UiResetForSOS(VOID)
{
    TRACE("UiResetForSOS()\n");
}

/* Message Functions */
VOID
UiMessageBox(
    _In_ PCSTR Format, ...)
{
    va_list ap;
    CHAR Buffer[512];

    va_start(ap, Format);
    RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    TRACE("UiMessageBox: %s\n", Buffer);
    /* TODO: Display message box using UEFI console */
}

VOID
UiMessageBoxCritical(
    _In_ PCSTR MessageText)
{
    TRACE("UiMessageBoxCritical: %s\n", MessageText ? MessageText : "(null)");
    /* TODO: Display critical message using UEFI console */
}

VOID
UiShowMessageBoxesInSection(
    IN ULONG_PTR SectionId)
{
    TRACE("UiShowMessageBoxesInSection(SectionId=0x%lx)\n", SectionId);
    /* TODO: Show message boxes for section */
}

/* Menu Functions */
BOOLEAN
UiDisplayMenu(
    IN PCSTR MenuHeader,
    IN PCSTR MenuFooter OPTIONAL,
    IN PCSTR MenuItemList[],
    IN ULONG MenuItemCount,
    IN ULONG DefaultMenuItem,
    IN LONG MenuTimeOut,
    OUT PULONG SelectedMenuItem,
    IN BOOLEAN CanEscape,
    IN UiMenuKeyPressFilterCallback KeyPressFilter OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    TRACE("UiDisplayMenu: Header=\"%s\", ItemCount=%lu, Default=%lu\n",
          MenuHeader ? MenuHeader : "(null)", MenuItemCount, DefaultMenuItem);

    UNREFERENCED_PARAMETER(MenuFooter);
    UNREFERENCED_PARAMETER(CanEscape);
    UNREFERENCED_PARAMETER(KeyPressFilter);
    UNREFERENCED_PARAMETER(Context);

    ULONG DefaultIndex = (DefaultMenuItem < MenuItemCount) ? DefaultMenuItem : 0;
    if (SelectedMenuItem)
        *SelectedMenuItem = DefaultIndex;

    const PCSTR DefaultLabel = (MenuItemList && DefaultIndex < MenuItemCount &&
                                MenuItemList[DefaultIndex])
                                   ? MenuItemList[DefaultIndex]
                                   : "default entry";

    LONG TimeoutSeconds = MenuTimeOut;
    if (TimeoutSeconds <= 0)
        TimeoutSeconds = 5;

    BOOLEAN UseGop = UefiGopConsoleIsInitialized();
    UiConsoleClear(UseGop);

    UINT32 LineCursor = 0;
    if (MenuHeader && *MenuHeader)
        UiConsoleWriteLine(UseGop, &LineCursor, MenuHeader);

    if (MenuItemCount > 1)
    {
        UiConsoleWriteLine(UseGop, &LineCursor, "Multiple boot entries detected.");
        UiConsoleWriteLine(UseGop, &LineCursor,
                           "UI placeholder active; proceeding with default selection:");
        CHAR Summary[160];
        RtlStringCbPrintfA(Summary, sizeof(Summary), "  %s", DefaultLabel);
        UiConsoleWriteLine(UseGop, &LineCursor, Summary);
    }
    else
    {
        CHAR Summary[160];
        RtlStringCbPrintfA(Summary, sizeof(Summary), "Boot target: %s", DefaultLabel);
        UiConsoleWriteLine(UseGop, &LineCursor, Summary);
    }

    UiConsoleWriteLine(UseGop, &LineCursor,
                       "Press any key to boot immediately.");

    ULONG ConsoleWidth = 80;
    ULONG Height = 0, Depth = 0;
    UefiVideoGetDisplaySize(&ConsoleWidth, &Height, &Depth);

    UINT32 CountdownLine = LineCursor;
    CHAR CountdownText[160];

    for (LONG Seconds = TimeoutSeconds; Seconds > 0; --Seconds)
    {
        RtlStringCbPrintfA(CountdownText, sizeof(CountdownText),
                           "Booting in %ld second%s...", Seconds, (Seconds == 1) ? "" : "s");

        if (UseGop)
        {
            UefiGopConsoleSetCursor(0, CountdownLine);
            UiConsoleWritePadded(TRUE, CountdownText, ConsoleWidth);
            UefiGopConsoleSetCursor(0, CountdownLine);
        }
        else
        {
            UiConsolePutChar(FALSE, '\r');
            UiConsoleWritePadded(FALSE, CountdownText, ConsoleWidth);
            UiConsolePutChar(FALSE, '\r');
        }

        BOOLEAN SkipCountdown = FALSE;
        for (ULONG Tick = 0; Tick < 100; ++Tick)
        {
            if (MachConsKbHit())
            {
                MachConsGetCh();
                SkipCountdown = TRUE;
                break;
            }
            StallExecutionProcessor(10000);
        }

        if (SkipCountdown)
            break;
    }

    if (UseGop)
    {
        UefiGopConsoleSetCursor(0, CountdownLine);
        UiConsoleWritePadded(TRUE, "Booting now...", ConsoleWidth);
        UiConsoleNewLine(TRUE);
    }
    else
    {
        UiConsolePutChar(FALSE, '\r');
        UiConsoleWritePadded(FALSE, "Booting now...", ConsoleWidth);
        UiConsoleNewLine(FALSE);
    }

    return TRUE;
}

BOOLEAN
UiEditBox(PCSTR MessageText, PCHAR EditTextBuffer, ULONG Length)
{
    TRACE("UiEditBox: Message=\"%s\", Length=%lu\n",
          MessageText ? MessageText : "(null)", Length);

    /* For now, just return empty string */
    if (EditTextBuffer && Length > 0)
    {
        EditTextBuffer[0] = '\0';
    }

    /* TODO: Implement edit box using UEFI console */
    return TRUE;
}
