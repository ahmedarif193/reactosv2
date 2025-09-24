/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UI Function Stubs for UEFI-only bootloader
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <ui.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(UI);

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

/* Basic UI Functions */
BOOLEAN
UiInitialize(BOOLEAN ShowUi)
{
    TRACE("UiInitialize(ShowUi=%s)\n", ShowUi ? "TRUE" : "FALSE");
    /* TODO: Initialize UEFI console for actual UI */
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

    /* For now, just return the default menu item */
    if (SelectedMenuItem)
    {
        *SelectedMenuItem = (DefaultMenuItem < MenuItemCount) ? DefaultMenuItem : 0;
    }

    /* TODO: Implement actual menu display using UEFI console */
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
