/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#include <debug.h>
#ifdef _M_ARM64
#include <comm.h>

#endif
DBG_DEFAULT_CHANNEL(WARNING);

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16
#define TOP_BOTTOM_LINES 0
#define LOWEST_SUPPORTED_RES 1

/* AGENT-MODIFIED: Added preferred resolution constants for better mode selection */
#define PREFERRED_WIDTH_MIN  800
#define PREFERRED_WIDTH_MAX  1920
#define PREFERRED_HEIGHT_MIN 600
#define PREFERRED_HEIGHT_MAX 1200

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* Macro for dual console output on ARM64 */
#ifdef _M_ARM64
extern VOID UefiConsPutString(PCWSTR String);
#define UEFI_CONSOLE_OUTPUT(str) UefiConsPutString(str)
#else
#define UEFI_CONSOLE_OUTPUT(str) \
    do { \
        if (GlobalSystemTable && GlobalSystemTable->ConOut) \
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, str); \
    } while (0)
#endif
extern UCHAR BitmapFont8x16[256 * 16];

UCHAR MachDefaultTextColor = COLOR_GRAY;
REACTOS_INTERNAL_BGCONTEXT framebufferData;
EFI_GUID EfiGraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* AGENT-MODIFIED: Added variables for software text rendering */
static UINT32 ConsoleX = 0;
static UINT32 ConsoleY = 0;
static UINT32 MaxConsoleX = 0;
static UINT32 MaxConsoleY = 0;
static BOOLEAN GopConsoleInitialized = FALSE;
static BOOLEAN GopBltOnly = FALSE;
static EFI_GRAPHICS_OUTPUT_PROTOCOL* gGop = NULL;

/* Pretty-print pixel format */
static const char*
PixelFormatName(UINT32 pf)
{
    switch (pf)
    {
        case 0: return "RGBR888";      /* PixelRedGreenBlueReserved8BitPerColor */
        case 1: return "BGRR888";      /* PixelBlueGreenRedReserved8BitPerColor */
        case 2: return "BitMask";      /* PixelBitMask */
        case 3: return "BltOnly";      /* PixelBltOnly */
        default: return "Unknown";
    }
}

/* FUNCTIONS ******************************************************************/

/* AGENT-MODIFIED: Added function to select optimal GOP mode */
static UINT32
UefiFindOptimalGopMode(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop)
{
    EFI_STATUS Status;
    UINT32 BestMode = 0;
    UINT32 BestScore = 0;
    UINT32 CurrentMode;
    UINTN SizeOfInfo;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    
    /* Avoid TRACE on ARM64 path: use minimal ConOut only at call site */
    
    for (CurrentMode = 0; CurrentMode < gop->Mode->MaxMode; CurrentMode++)
    {
        Status = gop->QueryMode(gop, CurrentMode, &SizeOfInfo, &Info);
        if (Status == EFI_SUCCESS)
        {
            UINT32 Width = Info->HorizontalResolution;
            UINT32 Height = Info->VerticalResolution;
            UINT32 Score = 0;
            
            /* no TRACE: keep selection silent to avoid early exceptions */
            
            /* Skip modes without framebuffer support */
            if (Info->PixelFormat == PixelBltOnly)
            {
                /* skip blt-only modes */
                continue;
            }
            
            /* Preferred resolutions (in order of preference) */
            if (Width == 1024 && Height == 768)
                Score = 100;  /* Most preferred */
            else if (Width == 1280 && Height == 1024)
                Score = 95;
            else if (Width == 1280 && Height == 800)
                Score = 90;
            else if (Width == 800 && Height == 600)
                Score = 85;
            else if (Width == 1366 && Height == 768)
                Score = 80;
            else if (Width == 1440 && Height == 900)
                Score = 75;
            else if (Width == 640 && Height == 480)
                Score = 50;  /* Fallback */
            else if (Width >= PREFERRED_WIDTH_MIN && Width <= PREFERRED_WIDTH_MAX &&
                     Height >= PREFERRED_HEIGHT_MIN && Height <= PREFERRED_HEIGHT_MAX)
            {
                /* Other acceptable resolutions */
                Score = 60;
            }
            
            if (Score > BestScore)
            {
                BestScore = Score;
                BestMode = CurrentMode;
                TRACE("[GOP] Candidate mode %u: %ux%u pf=%u score=%u (new best)\n",
                      (unsigned)CurrentMode, (unsigned)Width, (unsigned)Height,
                      (unsigned)Info->PixelFormat, (unsigned)Score);
            }
        }
    }
    return BestMode;
}

EFI_STATUS
UefiInitializeVideo(VOID)
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    UINT32 OptimalMode;

    TRACE("[GOP] UefiInitializeVideo entry\n");
    RtlZeroMemory(&framebufferData, sizeof(framebufferData));
    if (!GlobalSystemTable)
        return EFI_ABORTED;
    if (!GlobalSystemTable->BootServices)
        return EFI_ABORTED;

    TRACE("[GOP] LocateProtocol(EFI_GRAPHICS_OUTPUT_PROTOCOL)\n");
    Status = GlobalSystemTable->BootServices->LocateProtocol(&EfiGraphicsOutputProtocol, 0, (void**)&gop);
    if (Status != EFI_SUCCESS)
    {
        TRACE("[GOP] LocateProtocol failed: %lx\n", (ULONG_PTR)Status);
        /* Don't fail completely, just skip GOP setup */
        return EFI_SUCCESS;
    }

    /* GOP located, continue with setup */
#ifdef _ARM64_
    /* Check if gop pointer is valid */
    if (!gop)
        return EFI_DEVICE_ERROR;
#endif

    if (!gop->Mode)
        return EFI_DEVICE_ERROR;
    TRACE("[GOP] Mode struct at %p, MaxMode=%u, CurrentMode=%u\n",
          gop->Mode, (unsigned)gop->Mode->MaxMode, (unsigned)gop->Mode->Mode);
    if (gop->Mode->Info)
    {
        TRACE("[GOP] Current: %ux%u, ppsl=%u, pf=%u(%s), FBBase=%p, FBSize=%lu\n",
              (unsigned)gop->Mode->Info->HorizontalResolution,
              (unsigned)gop->Mode->Info->VerticalResolution,
              (unsigned)gop->Mode->Info->PixelsPerScanLine,
              (unsigned)gop->Mode->Info->PixelFormat,
              PixelFormatName(gop->Mode->Info->PixelFormat),
              (PVOID)gop->Mode->FrameBufferBase,
              (ULONG_PTR)gop->Mode->FrameBufferSize);
    }
    
    /* AGENT-MODIFIED: Find and set optimal resolution instead of hardcoded low res */
    OptimalMode = UefiFindOptimalGopMode(gop);
    if (OptimalMode != gop->Mode->Mode)
    {
        TRACE("[GOP] Switching mode: %u -> %u\n", (unsigned)gop->Mode->Mode, (unsigned)OptimalMode);
        Status = gop->SetMode(gop, OptimalMode);
        TRACE("[GOP] SetMode status: %lx\n", (ULONG_PTR)Status);
        /* Continue with current mode if SetMode fails */
    }

    /* Safety checks for gop->Mode access */
    if (!gop->Mode)
        return EFI_DEVICE_ERROR;

    if (!gop->Mode->Info)
        return EFI_DEVICE_ERROR;

    /* Access framebuffer data */
    framebufferData.BaseAddress        = (ULONG_PTR)gop->Mode->FrameBufferBase;
    framebufferData.BufferSize         = gop->Mode->FrameBufferSize;
    framebufferData.ScreenWidth        = gop->Mode->Info->HorizontalResolution;
    framebufferData.ScreenHeight       = gop->Mode->Info->VerticalResolution;
    framebufferData.PixelsPerScanLine  = gop->Mode->Info->PixelsPerScanLine;
    framebufferData.PixelFormat        = gop->Mode->Info->PixelFormat;

    /* Track BLT-only state and warn if no linear framebuffer is exposed */
    GopBltOnly = (gop->Mode->Info->PixelFormat == PixelBltOnly) ||
                 (gop->Mode->FrameBufferBase == 0) ||
                 (gop->Mode->FrameBufferSize == 0);
    if (GopBltOnly)
    {
        TRACE("[GOP] Warning: BLT-only or no linear FB. Software text console will not draw to VRAM.\n");
    }

    TRACE("[GOP] Final mode: %ux%u ppsl=%u pf=%u(%s) fb=%p size=%lu\n",
          (unsigned)framebufferData.ScreenWidth,
          (unsigned)framebufferData.ScreenHeight,
          (unsigned)framebufferData.PixelsPerScanLine,
          (unsigned)framebufferData.PixelFormat,
          PixelFormatName(framebufferData.PixelFormat),
          (PVOID)framebufferData.BaseAddress,
          (ULONG_PTR)framebufferData.BufferSize);

    /* AGENT-MODIFIED: Initialize console dimensions for software text rendering */
    /* Check for division by zero */
    if (CHAR_WIDTH == 0 || CHAR_HEIGHT == 0) {
        MaxConsoleX = 80;  /* Default fallback */
        MaxConsoleY = 25;  /* Default fallback */
    } else {
        MaxConsoleX = framebufferData.ScreenWidth / CHAR_WIDTH;
        MaxConsoleY = (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT;
    }

    ConsoleX = 0;
    ConsoleY = 0;
    GopConsoleInitialized = TRUE;
    gGop = gop;
    TRACE("[GOP] UefiInitializeVideo complete\n");
    return Status;
}

VOID
UefiPrintFramebufferData(VOID)
{
    TRACE("Framebuffer BaseAddress       : %X\n", framebufferData.BaseAddress);
    TRACE("Framebuffer BufferSize        : %X\n", framebufferData.BufferSize);
    TRACE("Framebuffer ScreenWidth       : %d\n", framebufferData.ScreenWidth);
    TRACE("Framebuffer ScreenHeight      : %d\n", framebufferData.ScreenHeight);
    TRACE("Framebuffer PixelsPerScanLine : %d\n", framebufferData.PixelsPerScanLine);
    TRACE("Framebuffer PixelFormat       : %d\n", framebufferData.PixelFormat);
}

static ULONG
UefiVideoAttrToSingleColor(UCHAR Attr)
{
    UCHAR Intensity;
    Intensity = (0 == (Attr & 0x08) ? 127 : 255);

    return 0xff000000 |
           (0 == (Attr & 0x04) ? 0 : (Intensity << 16)) |
           (0 == (Attr & 0x02) ? 0 : (Intensity << 8)) |
           (0 == (Attr & 0x01) ? 0 : Intensity);
}

static VOID
UefiVideoAttrToColors(UCHAR Attr, ULONG *FgColor, ULONG *BgColor)
{
    *FgColor = UefiVideoAttrToSingleColor(Attr & 0xf);
    *BgColor = UefiVideoAttrToSingleColor((Attr >> 4) & 0xf);
}


/* Convert packed ARGB to EFI BLT pixel (BGRA with 8-bit channels) */
static VOID
UefiColorToBltPixel(ULONG Color, EFI_GRAPHICS_OUTPUT_BLT_PIXEL* P)
{
    P->Blue     = (UINT8)(Color & 0xFF);
    P->Green    = (UINT8)((Color >> 8) & 0xFF);
    P->Red      = (UINT8)((Color >> 16) & 0xFF);
    P->Reserved = 0x00;
}

static VOID
UefiVideoClearScreenColor(ULONG Color, BOOLEAN FullScreen)
{
    ULONG Delta;
    ULONG Line, Col;
    PULONG p;

#ifdef _ARM64_
    /* Safety: ensure we have something to draw to */
    if (!GopConsoleInitialized || framebufferData.ScreenWidth == 0 || framebufferData.ScreenHeight == 0)
        return;

    /* BLT-only fallback: use GOP Blt to fill the screen area */
    if ((GopBltOnly || framebufferData.BaseAddress == 0) && gGop && GlobalSystemTable && GlobalSystemTable->BootServices)
    {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
        UINTN startY = (FullScreen ? 0 : TOP_BOTTOM_LINES);
        UINTN height = framebufferData.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES);
        UefiColorToBltPixel(Color, &px);
        gGop->Blt(gGop, &px, EfiBltVideoFill, 0, 0,
                  0, startY,
                  framebufferData.ScreenWidth, height, 0);
        return;
    }
#endif

    /* Extra safety for all platforms */
    if (!framebufferData.BaseAddress)
        return;

    Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
    for (Line = 0; Line < framebufferData.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES); Line++)
    {
        p = (PULONG) ((char *) framebufferData.BaseAddress + (Line + (FullScreen ? 0 : TOP_BOTTOM_LINES)) * Delta);
        for (Col = 0; Col < framebufferData.ScreenWidth; Col++)
        {
            *p++ = Color;
        }
    }
}

VOID
UefiVideoClearScreen(UCHAR Attr)
{
    ULONG FgColor, BgColor;

    UefiVideoAttrToColors(Attr, &FgColor, &BgColor);
    UefiVideoClearScreenColor(BgColor, FALSE);
}

VOID
UefiVideoOutputChar(UCHAR Char, unsigned X, unsigned Y, ULONG FgColor, ULONG BgColor)
{
    PUCHAR FontPtr;
    PULONG Pixel;
    UCHAR Mask;
    unsigned Line;
    unsigned Col;
    ULONG Delta;

#ifdef _ARM64_
    /* Safety check - make sure video mode is initialized */
    if (!GopConsoleInitialized)
        return;

    /* BLT-only fallback: build a glyph buffer and blit to video */
    if ((GopBltOnly || framebufferData.BaseAddress == 0) && gGop && GlobalSystemTable && GlobalSystemTable->BootServices)
    {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg, bg;
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL glyph[CHAR_HEIGHT][CHAR_WIDTH];
        PUCHAR FontPtr;
        UCHAR Mask;
        unsigned Line, Col;
        UINTN destX, destY;

        UefiColorToBltPixel(FgColor, &fg);
        UefiColorToBltPixel(BgColor, &bg);

        FontPtr = BitmapFont8x16 + Char * 16;
        for (Line = 0; Line < CHAR_HEIGHT; Line++)
        {
            Mask = 0x80;
            for (Col = 0; Col < CHAR_WIDTH; Col++)
            {
                glyph[Line][Col] = (0 != (FontPtr[Line] & Mask)) ? fg : bg;
                Mask >>= 1;
            }
        }

        destX = X * CHAR_WIDTH;
        destY = Y * CHAR_HEIGHT + TOP_BOTTOM_LINES;
        gGop->Blt(gGop,
                  (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)glyph,
                  EfiBltBufferToVideo,
                  0, 0,
                  destX, destY,
                  CHAR_WIDTH, CHAR_HEIGHT,
                  0);
        return;
    }
#endif

    /* Extra safety for all platforms */
    if (!framebufferData.BaseAddress)
        return;

    Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
    FontPtr = BitmapFont8x16 + Char * 16;
    Pixel = (PULONG) ((char *) framebufferData.BaseAddress +
            (Y * CHAR_HEIGHT + TOP_BOTTOM_LINES) *  Delta + X * CHAR_WIDTH * 4);

    for (Line = 0; Line < CHAR_HEIGHT; Line++)
    {
        Mask = 0x80;
        for (Col = 0; Col < CHAR_WIDTH; Col++)
        {
            Pixel[Col] = (0 != (FontPtr[Line] & Mask) ? FgColor : BgColor);
            Mask = Mask >> 1;
        }
        Pixel = (PULONG) ((char *) Pixel + Delta);
    }
}

VOID
UefiVideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y)
{
    ULONG FgColor = 0;
    ULONG BgColor = 0;
    if (Ch != 0)
    {
        UefiVideoAttrToColors(Attr, &FgColor, &BgColor);
        UefiVideoOutputChar(Ch, X, Y, FgColor, BgColor);
    }
}

VOID
UefiVideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth)
{
#ifdef _ARM64_
    /* Check if GOP is initialized */
    if (!GopConsoleInitialized || framebufferData.ScreenWidth == 0 || framebufferData.ScreenHeight == 0)
    {
        /* Return safe defaults if not initialized */
        *Width = 80;   /* Standard text console width */
        *Height = 25;  /* Standard text console height */
        *Depth = 0;
        TRACE("[GOP] UefiVideoGetDisplaySize fallback (uninitialized).\n");
        return;
    }
#endif

    /* ARM64: Extra safety - avoid division by zero */
    if (CHAR_WIDTH == 0 || CHAR_HEIGHT == 0)
    {
        *Width = 80;
        *Height = 25;
        *Depth = 0;
        return;
    }

    *Width =  framebufferData.ScreenWidth / CHAR_WIDTH;
    *Height = (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT;
    *Depth =  0;

}

VIDEODISPLAYMODE
UefiVideoSetDisplayMode(char *DisplayMode, BOOLEAN Init)
{
#ifdef _ARM64_
    /* Check if we need to initialize GOP first */
    if (Init && !GopConsoleInitialized)
    {
        /* Try to initialize video if not already done */
        TRACE("[GOP] UefiVideoSetDisplayMode: Init requested, calling UefiInitializeVideo()\n");
        UefiInitializeVideo();
    }
#endif

    /* We only have one mode, semi-text */
    return VideoTextMode;
}

ULONG
UefiVideoGetBufferSize(VOID)
{
#ifdef _ARM64_
    /* Safety checks */
    if (!GopConsoleInitialized || framebufferData.ScreenWidth == 0 || framebufferData.ScreenHeight == 0)
        return (80 * 25 * 2);  /* Default buffer size for 80x25 text mode */
#endif

    /* Extra safety - avoid division by zero */
    if (CHAR_WIDTH == 0 || CHAR_HEIGHT == 0)
    {
        return (80 * 25 * 2);
    }

    return ((framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT * (framebufferData.ScreenWidth / CHAR_WIDTH) * 2);
}

VOID
UefiVideoCopyOffScreenBufferToVRAM(PVOID Buffer)
{
    PUCHAR OffScreenBuffer = (PUCHAR)Buffer;
    ULONG Col, Line;
    for (Line = 0; Line < (framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT; Line++)
    {
        for (Col = 0; Col < framebufferData.ScreenWidth / CHAR_WIDTH; Col++)
        {
            UefiVideoPutChar(OffScreenBuffer[0], OffScreenBuffer[1], Col, Line);
            OffScreenBuffer += 2;
        }
    }
}

VOID
UefiVideoScrollUp(VOID)
{
    ULONG BgColor, Dummy;

#ifdef _ARM64_
    if (!GopConsoleInitialized || framebufferData.ScreenWidth == 0 || framebufferData.ScreenHeight == 0)
        return;

    /* BLT-only fallback: use VideoToVideo Blt + fill last line */
    if ((GopBltOnly || framebufferData.BaseAddress == 0) && gGop && GlobalSystemTable && GlobalSystemTable->BootServices)
    {
        UINTN startY = TOP_BOTTOM_LINES;
        UINTN width = framebufferData.ScreenWidth;
        UINTN totalH = framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES;
        if (totalH == 0)
            return;

        if (totalH > CHAR_HEIGHT)
        {
            gGop->Blt(gGop,
                      NULL,
                      EfiBltVideoToVideo,
                      0, startY + CHAR_HEIGHT,
                      0, startY,
                      width, totalH - CHAR_HEIGHT,
                      0);
        }

        /* Fill the last character row with background color */
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
        UefiVideoAttrToColors(ATTR(COLOR_WHITE, COLOR_BLACK), &Dummy, &BgColor);
        UefiColorToBltPixel(BgColor, &px);
        gGop->Blt(gGop,
                  &px,
                  EfiBltVideoFill,
                  0, 0,
                  0, startY + (totalH > CHAR_HEIGHT ? (totalH - CHAR_HEIGHT) : 0),
                  width, (totalH >= CHAR_HEIGHT ? CHAR_HEIGHT : totalH),
                  0);
        return;
    }
#endif

    /* Linear FB path */
    {
        ULONG Delta = (framebufferData.PixelsPerScanLine * 4 + 3) & ~ 0x3;
        ULONG PixelCount = framebufferData.ScreenWidth * CHAR_HEIGHT *
                           (((framebufferData.ScreenHeight - 2 * TOP_BOTTOM_LINES) / CHAR_HEIGHT) - 1);
        PULONG Src = (PULONG)((PUCHAR)framebufferData.BaseAddress + (CHAR_HEIGHT + TOP_BOTTOM_LINES) * Delta);
        PULONG Dst = (PULONG)((PUCHAR)framebufferData.BaseAddress + TOP_BOTTOM_LINES * Delta);

        UefiVideoAttrToColors(ATTR(COLOR_WHITE, COLOR_BLACK), &Dummy, &BgColor);

        while (PixelCount--)
            *Dst++ = *Src++;

        for (PixelCount = 0; PixelCount < framebufferData.ScreenWidth * CHAR_HEIGHT; PixelCount++)
            *Dst++ = BgColor;
    }
}

VOID
UefiVideoSetTextCursorPosition(UCHAR X, UCHAR Y)
{
    /* We don't have a cursor yet */
}

VOID
UefiVideoHideShowTextCursor(BOOLEAN Show)
{
    /* We don't have a cursor yet */
}

BOOLEAN
UefiVideoIsPaletteFixed(VOID)
{
    return 0;
}

VOID
UefiVideoSetPaletteColor(UCHAR Color, UCHAR Red,
                         UCHAR Green, UCHAR Blue)
{
    /* Not supported */
}

VOID
UefiVideoGetPaletteColor(UCHAR Color, UCHAR* Red,
                         UCHAR* Green, UCHAR* Blue)
{
    /* Not supported */
}

/* AGENT-MODIFIED: Added software text rendering functions for GOP console */

/* AGENT-MODIFIED: Direct GOP console output function */
VOID
UefiGopConsolePutChar(CHAR Ch)
{
    ULONG FgColor, BgColor;
    
    if (!GopConsoleInitialized || framebufferData.BaseAddress == 0)
        return;
    
    /* Get current colors */
    UefiVideoAttrToColors(MachDefaultTextColor, &FgColor, &BgColor);
    
    /* Handle special characters */
    if (Ch == '\r')
    {
        ConsoleX = 0;
        return;
    }
    else if (Ch == '\n')
    {
        ConsoleX = 0;
        ConsoleY++;
    }
    else if (Ch == '\t')
    {
        ConsoleX = (ConsoleX + 8) & ~7;
    }
    else if (Ch == '\b')
    {
        if (ConsoleX > 0)
        {
            ConsoleX--;
            UefiVideoOutputChar(' ', ConsoleX, ConsoleY, FgColor, BgColor);
        }
    }
    else
    {
        /* Output normal character */
        UefiVideoOutputChar(Ch, ConsoleX, ConsoleY, FgColor, BgColor);
        ConsoleX++;
    }
    
    /* Handle line wrap */
    if (ConsoleX >= MaxConsoleX)
    {
        ConsoleX = 0;
        ConsoleY++;
    }
    
    /* Handle scrolling */
    if (ConsoleY >= MaxConsoleY)
    {
        UefiVideoScrollUp();
        ConsoleY = MaxConsoleY - 1;
    }
}

/* AGENT-MODIFIED: GOP console string output */
VOID
UefiGopConsolePutString(PCSTR String)
{
    if (!String)
        return;
        
    while (*String)
    {
        UefiGopConsolePutChar(*String);
        String++;
    }
}

/* AGENT-MODIFIED: Clear GOP console screen */
VOID
UefiGopConsoleClear(VOID)
{
    if (!GopConsoleInitialized || framebufferData.BaseAddress == 0)
        return;
    
    UefiVideoClearScreen(MachDefaultTextColor);
    ConsoleX = 0;
    ConsoleY = 0;
}

/* AGENT-MODIFIED: Set GOP console cursor position */
VOID
UefiGopConsoleSetCursor(UINT32 X, UINT32 Y)
{
    if (!GopConsoleInitialized)
        return;
        
    if (X < MaxConsoleX)
        ConsoleX = X;
    
    if (Y < MaxConsoleY)
        ConsoleY = Y;
}

/* AGENT-MODIFIED: Get GOP console status */
BOOLEAN
UefiGopConsoleIsInitialized(VOID)
{
    return GopConsoleInitialized && (framebufferData.BaseAddress != 0);
}
