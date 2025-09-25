/*
 * PROJECT:     ReactOS Boot Video Driver for ARM64 devices
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file for ARM64 boot video support
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/* Import framebuffer data from kernel for UEFI GOP systems */
__declspec(dllimport) PHYSICAL_ADDRESS VidpFrameBufferBase;
__declspec(dllimport) ULONG VidpFrameBufferSize;
__declspec(dllimport) ULONG VidpScreenWidth;
__declspec(dllimport) ULONG VidpScreenHeight;
__declspec(dllimport) ULONG VidpPixelsPerScanLine;

/* Local framebuffer variables */
PUSHORT VgaArmBase;
PHYSICAL_ADDRESS VgaPhysical;
static ULONG ScreenWidth = 0;
static ULONG ScreenHeight = 0;
static ULONG PixelsPerScanLine = 0;

/* PRIVATE FUNCTIONS *********************************************************/

FORCEINLINE
USHORT
VidpBuildColor(
    _In_ UCHAR Color)
{
    UCHAR Red, Green, Blue;

    /* Extract color components */
    Red   = GetRValue(VidpDefaultPalette[Color]) >> 3;
    Green = GetGValue(VidpDefaultPalette[Color]) >> 3;
    Blue  = GetBValue(VidpDefaultPalette[Color]) >> 3;

    /* Build the 16-bit color mask */
    return ((Red & 0x1F) << 11) | ((Green & 0x1F) << 6) | ((Blue & 0x1F));
}

VOID
PrepareForSetPixel(VOID)
{
    /* Nothing to prepare for ARM64 */
    NOTHING;
}

VOID
SetPixel(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ UCHAR Color)
{
    PUSHORT PixelPosition;

    /* Validate coordinates */
    if (Left >= ScreenWidth || Top >= ScreenHeight)
        return;

    /* Calculate the pixel position using actual pitch */
    PixelPosition = &VgaArmBase[Left + (Top * PixelsPerScanLine)];

    /* Set our color */
    WRITE_REGISTER_USHORT(PixelPosition, VidpBuildColor(Color));
}

VOID
DisplayCharacter(
    _In_ CHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    PUCHAR FontChar;
    ULONG i, j, XOffset;

    /* Get the font line for this character */
    FontChar = &VidpFontData[Character * BOOTCHAR_HEIGHT - Top];

    /* Loop each pixel height */
    for (i = BOOTCHAR_HEIGHT; i > 0; --i)
    {
        /* Loop each pixel width */
        XOffset = Left;
        for (j = (1 << 7); j > 0; j >>= 1)
        {
            /* Check if we should draw this pixel */
            if (FontChar[Top] & (UCHAR)j)
            {
                /* We do, use the given Text Color */
                SetPixel(XOffset, Top, (UCHAR)TextColor);
            }
            else if (BackColor < BV_COLOR_NONE)
            {
                /*
                 * This is a background pixel. We're drawing it
                 * unless it's transparent.
                 */
                SetPixel(XOffset, Top, (UCHAR)BackColor);
            }

            /* Increase X Offset */
            XOffset++;
        }

        /* Move to the next Y ordinate */
        Top++;
    }
}

VOID
DoScroll(
    _In_ ULONG Scroll)
{
    ULONG Top, Offset;
    PUSHORT SourceOffset, DestOffset;
    PUSHORT i, j;

    /* Set memory positions of the scroll */
    SourceOffset = &VgaArmBase[(VidpScrollRegion[1] * (PixelsPerScanLine / 8)) + (VidpScrollRegion[0] >> 3)];
    DestOffset = &SourceOffset[Scroll * (PixelsPerScanLine / 8)];

    /* Start loop */
    for (Top = VidpScrollRegion[1]; Top <= VidpScrollRegion[3]; ++Top)
    {
        /* Set number of bytes to loop and start offset */
        Offset = VidpScrollRegion[0] >> 3;
        j = SourceOffset;

        /* Check if this is part of the scroll region */
        if (Offset <= (VidpScrollRegion[2] >> 3))
        {
            /* Update position */
            i = (PUSHORT)(DestOffset - SourceOffset);

            /* Loop the X axis */
            do
            {
                /* Write value in the new position so that we can do the scroll */
                WRITE_REGISTER_USHORT(j, READ_REGISTER_USHORT(j + (ULONG_PTR)i));

                /* Move to the next memory location to write to */
                j++;

                /* Move to the next byte in the region */
                Offset++;

                /* Make sure we don't go past the scroll region */
            } while (Offset <= (VidpScrollRegion[2] >> 3));
        }

        /* Move to the next line */
        SourceOffset += (PixelsPerScanLine / 8);
        DestOffset += (PixelsPerScanLine / 8);
    }
}

VOID
PreserveRow(
    _In_ ULONG CurrentTop,
    _In_ ULONG TopDelta,
    _In_ BOOLEAN Restore)
{
    PUSHORT Position1, Position2;
    ULONG Count;

    /* Calculate the position in memory for the row */
    if (Restore)
    {
        /* Restore the row by copying back the contents saved off-screen */
        Position1 = &VgaArmBase[CurrentTop * (PixelsPerScanLine / 8)];
        Position2 = &VgaArmBase[ScreenHeight * (PixelsPerScanLine / 8)];
    }
    else
    {
        /* Preserve the row by saving its contents off-screen */
        Position1 = &VgaArmBase[ScreenHeight * (PixelsPerScanLine / 8)];
        Position2 = &VgaArmBase[CurrentTop * (PixelsPerScanLine / 8)];
    }

    /* Set the count and loop every pixel */
    Count = TopDelta * (PixelsPerScanLine / 8);
    while (Count--)
    {
        /* Write the data back on the other position */
        WRITE_REGISTER_USHORT(Position1, READ_REGISTER_USHORT(Position2));

        /* Increase both positions */
        Position1++;
        Position2++;
    }
}

VOID
VidpInitializeDisplay(VOID)
{
    /* ARM64 display initialization - platform specific */
    /* For now, we assume the bootloader has already set up the display */
    /* This would be customized based on the specific ARM64 platform */
}

VOID
InitPaletteWithTable(
    _In_ PULONG Table,
    _In_ ULONG Count)
{
    /* Palette initialization for ARM64 */
    /* This is usually not needed for modern ARM64 framebuffer devices */
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Count);
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
VidInitialize(
    _In_ BOOLEAN SetMode)
{
    DPRINT1("bootvid-arm64 v0.2 - UEFI GOP Framebuffer\n");

    /* Check if we have framebuffer information from the loader */
    if (VidpFrameBufferBase.QuadPart != 0 && VidpFrameBufferSize != 0)
    {
        /* Use the real framebuffer from UEFI GOP */
        VgaPhysical = VidpFrameBufferBase;
        ScreenWidth = VidpScreenWidth;
        ScreenHeight = VidpScreenHeight;
        PixelsPerScanLine = VidpPixelsPerScanLine;

        /* Map the framebuffer into virtual memory */
        VgaArmBase = (PUSHORT)MmMapIoSpace(VgaPhysical, VidpFrameBufferSize, MmNonCached);
        if (!VgaArmBase)
        {
            DPRINT1("[BOOTVID-ARM64] Failed to map framebuffer\n");
            return FALSE;
        }

        DPRINT1("[BOOTVID-ARM64] Using UEFI GOP framebuffer @ %p (Physical: 0x%llx)\n",
                VgaArmBase, VgaPhysical.QuadPart);
        DPRINT1("[BOOTVID-ARM64] Resolution: %dx%d, Pitch: %d pixels\n",
                ScreenWidth, ScreenHeight, PixelsPerScanLine);
    }
    else
    {
        /* Fallback: Allocate a dummy framebuffer for testing */
        DPRINT1("[BOOTVID-ARM64] No GOP framebuffer info, using fallback\n");

        /* Set default resolution */
        ScreenWidth = 640;
        ScreenHeight = 480;
        PixelsPerScanLine = 640;

        /* Allocate framebuffer - 600KB for 640x480@16bpp */
        VgaPhysical.QuadPart = -1;
        VgaArmBase = MmAllocateContiguousMemory(600 * 1024, VgaPhysical);
        if (!VgaArmBase) return FALSE;

        /* Get physical address */
        VgaPhysical = MmGetPhysicalAddress(VgaArmBase);
        if (!VgaPhysical.QuadPart) return FALSE;

        DPRINT1("[BOOTVID-ARM64] Fallback framebuffer @ %p (Physical: 0x%llx)\n",
                VgaArmBase, VgaPhysical.QuadPart);
    }

    /* Setup the display */
    VidpInitializeDisplay();

    /* Success */
    return TRUE;
}

VOID
NTAPI
VidResetDisplay(
    _In_ BOOLEAN HalReset)
{
    /* Clear the current position */
    VidpCurrentX = 0;
    VidpCurrentY = 0;

    /* Re-initialize the display */
    VidpInitializeDisplay();

    /* Re-initialize the palette and fill the screen black */
    InitializePalette();
    VidSolidColorFill(0, 0, ScreenWidth - 1, ScreenHeight - 1, BV_COLOR_BLACK);
}

VOID
NTAPI
VidCleanUp(VOID)
{
    /* Clean up video resources */
    if (VgaArmBase)
    {
        /* Check if we mapped the real framebuffer or allocated our own */
        if (VidpFrameBufferBase.QuadPart != 0 && VidpFrameBufferSize != 0)
        {
            /* Unmap the framebuffer */
            MmUnmapIoSpace(VgaArmBase, VidpFrameBufferSize);
        }
        /* Otherwise it was allocated memory, which will be freed on shutdown */
        VgaArmBase = NULL;
    }
}

VOID
NTAPI
VidScreenToBufferBlt(
    _Out_writes_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    ULONG X, Y;
    PUSHORT Source;
    PUSHORT Destination;

    /* Calculate starting positions */
    Source = &VgaArmBase[Top * PixelsPerScanLine + Left];
    Destination = (PUSHORT)Buffer;

    /* Copy each line */
    for (Y = 0; Y < Height; Y++)
    {
        /* Copy pixels for this line */
        for (X = 0; X < Width; X++)
        {
            Destination[X] = READ_REGISTER_USHORT(&Source[X]);
        }

        /* Move to next line */
        Source += PixelsPerScanLine;
        Destination = (PUSHORT)((PUCHAR)Destination + Delta);
    }
}

VOID
NTAPI
VidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    ULONG X, Y;

    /* Loop along the Y-axis */
    for (Y = Top; Y <= Bottom; Y++)
    {
        /* Loop along the X-axis */
        for (X = Left; X <= Right; X++)
        {
            /* Draw the pixel */
            SetPixel(X, Y, Color);
        }
    }
}