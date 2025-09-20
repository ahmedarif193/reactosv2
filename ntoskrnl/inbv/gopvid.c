#include <ntoskrnl.h>
#include <drivers/bootvid/display.h>
#define NDEBUG
#include <debug.h>

#include "gopfont.h"

typedef struct tagBITMAPINFOHEADER
{
    ULONG  biSize;
    LONG   biWidth;
    LONG   biHeight;
    USHORT biPlanes;
    USHORT biBitCount;
    ULONG  biCompression;
    ULONG  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    ULONG  biClrUsed;
    ULONG  biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

static PHYSICAL_ADDRESS GopFbPhys = {{0}};
static PUCHAR           GopFbBase = NULL;
static SIZE_T           GopFbSize = 0;

static ULONG GopWidth  = 0;
static ULONG GopHeight = 0;
static ULONG GopPsl    = 0;
static ULONG GopPitch  = 0;

static ULONG GopFormat = 0; /* 0=RGBX8, 1=BGRX8, 2=BitMask */
static ULONG GopBpp    = 4; /* bytes per pixel: 2 or 4 (never 3) */

static ULONG RMShift=0, GMShift=0, BMShift=0;
static ULONG RWidth=0,  GWidth=0,  BWidth=0;
static ULONG RMask=0,   GMask=0,   BMask=0;

static BOOLEAN   BgrtValid = FALSE;
static ULONG     BgrtX = 0, BgrtY = 0, BgrtW = 0, BgrtH = 0;
static ULONGLONG BgrtAddr = 0;
static ULONG     BgrtSize = 0;

static const ULONG GopFontWidth = 8;
static const ULONG GopFontHeight = 16;
static const ULONG GopLineSpacing = 2;
static const ULONG GopTabColumns = 4;

static const ULONG GopPaletteDefault[BV_MAX_COLORS] =
{
    0x000000, /* Black */
    0x800000, /* Red */
    0x008000, /* Green */
    0x808000, /* Brown */
    0x000080, /* Blue */
    0x800080, /* Magenta */
    0x008080, /* Cyan */
    0x808080, /* Dark Gray */
    0xC0C0C0, /* Light Gray */
    0xFF0000, /* Light Red */
    0x00FF00, /* Light Green */
    0xFFFF00, /* Yellow */
    0x0000FF, /* Light Blue */
    0xFF00FF, /* Light Magenta */
    0x00FFFF, /* Light Cyan */
    0xFFFFFF  /* White */
};

static ULONG GopPalette[BV_MAX_COLORS] =
{
    0x000000, 0x800000, 0x008000, 0x808000,
    0x000080, 0x800080, 0x008080, 0x808080,
    0xC0C0C0, 0xFF0000, 0x00FF00, 0xFFFF00,
    0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
};

static ULONG GopScrollLeft = 0;
static ULONG GopScrollTop = 0;
static ULONG GopScrollRight = 0;
static ULONG GopScrollBottom = 0;
static ULONG GopRegionWidth = 0;
static ULONG GopRegionHeight = 0;
static ULONG GopCharsPerLine = 0;
static ULONG GopMaxLines = 0;
static ULONG GopColumn = 0;
static ULONG GopLine = 0;
static ULONG GopLineHeight = 18;
static UCHAR  GopTextColorIndex = BV_COLOR_WHITE;
static ULONG  GopTextColor = 0xFFFFFF;
static ULONG  GopBgColor = 0x000000;
static BOOLEAN GopTextReady = FALSE;

static VOID ResetGopPalette(VOID)
{
    for (ULONG i = 0; i < BV_MAX_COLORS; ++i)
        GopPalette[i] = GopPaletteDefault[i];
    GopTextColor = GopPalette[GopTextColorIndex & (BV_MAX_COLORS - 1)];
}

static __inline VOID ComputeMaskInfo(ULONG Mask, PULONG Shift, PULONG Width)
{
    ULONG s = 0, w = 0;
    if (Mask)
    {
        while (((Mask >> s) & 1) == 0 && s < 32) s++;
        ULONG m = Mask >> s;
        while ((m & 1) && w < 32) { w++; m >>= 1; }
    }
    *Shift = s;
    *Width = w;
}

static __inline ULONG Scale8ToMask(UCHAR v8, ULONG width)
{
    if (!width) return 0;
    const ULONG maxv = (1u << width) - 1u;
    return (ULONG)((v8 * maxv + 127u) / 255u);
}

static __inline UCHAR ScaleMaskTo8(ULONG v, ULONG width)
{
    if (!width) return 0;
    const ULONG maxv = (1u << width) - 1u;
    return (UCHAR)((v * 255u + (maxv >> 1)) / maxv);
}

static __inline ULONG PackRGB888(UCHAR r, UCHAR g, UCHAR b)
{
    switch (GopFormat)
    {
        case 0: return ((ULONG)r) | ((ULONG)g << 8) | ((ULONG)b << 16);  /* RGBX: mem R,G, B, X */
        case 1: return ((ULONG)b) | ((ULONG)g << 8) | ((ULONG)r << 16);  /* BGRX: mem B,G, R, X */
        case 2:
        {
            ULONG out = 0;
            if (RWidth) out |= (Scale8ToMask(r, RWidth) << RMShift) & RMask;
            if (GWidth) out |= (Scale8ToMask(g, GWidth) << GMShift) & GMask;
            if (BWidth) out |= (Scale8ToMask(b, BWidth) << BMShift) & BMask;
            return out;
        }
        default: return 0;
    }
}

static __inline ULONG UnpackToRGB888(ULONG px)
{
    UCHAR r=0,g=0,b=0;
    switch (GopFormat)
    {
        case 0:
            r = (UCHAR)( px        & 0xFF);
            g = (UCHAR)((px >> 8)  & 0xFF);
            b = (UCHAR)((px >> 16) & 0xFF);
            break;
        case 1:
            b = (UCHAR)( px        & 0xFF);
            g = (UCHAR)((px >> 8)  & 0xFF);
            r = (UCHAR)((px >> 16) & 0xFF);
            break;
        case 2:
        {
            ULONG rv = (px & RMask) >> RMShift;
            ULONG gv = (px & GMask) >> GMShift;
            ULONG bv = (px & BMask) >> BMShift;
            r = ScaleMaskTo8(rv, RWidth);
            g = ScaleMaskTo8(gv, GWidth);
            b = ScaleMaskTo8(bv, BWidth);
            break;
        }
        default: break;
    }
    return ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
}

static __inline BOOLEAN RectEmpty(LONG l, LONG t, LONG r, LONG b)
{
    return (r < l) || (b < t);
}

static __inline BOOLEAN PointInBgrt(ULONG x, ULONG y)
{
    if (!BgrtValid || !BgrtW || !BgrtH) return FALSE;
    return (x >= BgrtX && x < (BgrtX + BgrtW) && y >= BgrtY && y < (BgrtY + BgrtH));
}

static __inline VOID WritePixel(ULONG x, ULONG y, ULONG rgb)
{
    if (!GopFbBase) return;
    if (x >= GopWidth || y >= GopHeight) return;
    if (PointInBgrt(x, y)) return;
    SIZE_T offset = (SIZE_T)y * GopPitch + (SIZE_T)x * GopBpp;
    if (offset + GopBpp > GopFbSize) return;
    PUCHAR p = GopFbBase + offset;
    ULONG packed = PackRGB888((UCHAR)((rgb >> 16) & 0xFF),
                              (UCHAR)((rgb >> 8)  & 0xFF),
                              (UCHAR)( rgb        & 0xFF));
    if (GopBpp == 4)      *(PULONG)p  = packed;
    else if (GopBpp == 2) *(PUSHORT)p = (USHORT)packed;
    else { p[0]=(UCHAR)(packed & 0xFF); p[1]=(UCHAR)((packed>>8)&0xFF); p[2]=(UCHAR)((packed>>16)&0xFF); }
}

static __inline ULONG ReadPixel(ULONG x, ULONG y)
{
    if (!GopFbBase || x >= GopWidth || y >= GopHeight) return 0;
    SIZE_T offset = (SIZE_T)y * GopPitch + (SIZE_T)x * GopBpp;
    if (offset + GopBpp > GopFbSize) return 0;
    PUCHAR p = GopFbBase + offset;
    ULONG raw = 0;
    if (GopBpp == 4) raw = *(PULONG)p;
    else if (GopBpp == 2) raw = *(PUSHORT)p;
    else raw = (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16);
    return UnpackToRGB888(raw);
}

static VOID FillRectPixels(ULONG left, ULONG top, ULONG right, ULONG bottom, ULONG rgb)
{
    if (!GopFbBase) return;
    if (left > right || top > bottom) return;
    if (right >= GopWidth) right = GopWidth - 1;
    if (bottom >= GopHeight) bottom = GopHeight - 1;
    for (ULONG y = top; y <= bottom; ++y)
        for (ULONG x = left; x <= right; ++x)
            WritePixel(x, y, rgb);
}

static VOID ClearLineArea(ULONG lineIndex)
{
    if (!GopTextReady) return;
    ULONG top = GopScrollTop + lineIndex * GopLineHeight;
    ULONG bottom = top + GopLineHeight - 1;
    FillRectPixels(GopScrollLeft,
                   top,
                   (GopScrollRight >= GopScrollLeft) ? GopScrollRight : (GopWidth - 1),
                   (bottom <= GopScrollBottom) ? bottom : GopScrollBottom,
                   GopBgColor);
}

static VOID ScrollTextUp(VOID)
{
    if (!GopTextReady || GopRegionHeight <= GopLineHeight) return;

    SIZE_T bytesPerRow = (SIZE_T)GopRegionWidth * GopBpp;
    PUCHAR dest = GopFbBase + (SIZE_T)GopScrollTop * GopPitch + (SIZE_T)GopScrollLeft * GopBpp;
    PUCHAR src  = dest + (SIZE_T)GopLineHeight * GopPitch;
    ULONG rowsToMove = (GopRegionHeight > GopLineHeight) ? (GopRegionHeight - GopLineHeight) : 0;

    for (ULONG row = 0; row < rowsToMove; ++row)
    {
        RtlMoveMemory(dest + (SIZE_T)row * GopPitch,
                     src  + (SIZE_T)row * GopPitch,
                     bytesPerRow);
    }

    ULONG clearTop = (GopScrollBottom >= GopLineHeight)
        ? GopScrollBottom - GopLineHeight + 1
        : GopScrollTop;

    FillRectPixels(GopScrollLeft,
                   clearTop,
                   (GopScrollRight >= GopScrollLeft) ? GopScrollRight : (GopWidth - 1),
                   GopScrollBottom,
                   GopBgColor);

    if (GopMaxLines > 0)
    {
        ClearLineArea(GopMaxLines - 1);
    }
}

static VOID ClearCharCell(ULONG column, ULONG line)
{
    if (!GopTextReady) return;
    ULONG left = GopScrollLeft + column * GopFontWidth;
    ULONG top = GopScrollTop + line * GopLineHeight;
    ULONG right = left + GopFontWidth - 1;
    ULONG bottom = top + GopFontHeight - 1;
    FillRectPixels(left, top, right, bottom, GopBgColor);
}

static VOID UpdateRegionMetrics(VOID)
{
    if (GopScrollLeft >= GopWidth)  GopScrollLeft  = (GopWidth  > 0) ? GopWidth  - 1 : 0;
    if (GopScrollTop >= GopHeight)  GopScrollTop   = (GopHeight > 0) ? GopHeight - 1 : 0;
    if (GopScrollRight >= GopWidth) GopScrollRight = (GopWidth  > 0) ? GopWidth  - 1 : 0;
    if (GopScrollBottom >= GopHeight) GopScrollBottom = (GopHeight > 0) ? GopHeight - 1 : 0;

    if (GopScrollRight < GopScrollLeft) GopScrollRight = GopScrollLeft;
    if (GopScrollBottom < GopScrollTop) GopScrollBottom = GopScrollTop;

    GopRegionWidth  = GopScrollRight - GopScrollLeft + 1;
    GopRegionHeight = GopScrollBottom - GopScrollTop + 1;
    if (GopRegionWidth == 0)  GopRegionWidth  = 1;
    if (GopRegionHeight == 0) GopRegionHeight = 1;

    GopLineHeight = GopFontHeight + GopLineSpacing;
    if (GopLineHeight == 0) GopLineHeight = GopFontHeight;

    GopCharsPerLine = (GopRegionWidth >= GopFontWidth && GopFontWidth) ? (GopRegionWidth / GopFontWidth) : 1;
    if (GopCharsPerLine == 0) GopCharsPerLine = 1;

    GopMaxLines = (GopRegionHeight >= GopLineHeight && GopLineHeight) ? (GopRegionHeight / GopLineHeight) : 1;
    if (GopMaxLines == 0) GopMaxLines = 1;

    GopColumn = 0;
    GopLine = 0;
    GopTextReady = TRUE;

    ULONG sampleX = (GopScrollLeft < GopWidth) ? GopScrollLeft : 0;
    ULONG sampleY = (GopScrollTop < GopHeight) ? GopScrollTop : 0;
    ULONG sample = ReadPixel(sampleX, sampleY);
    GopBgColor = sample ? sample : GopPalette[BV_COLOR_BLACK];
    GopTextColorIndex = BV_COLOR_WHITE;
    GopTextColor = GopPalette[GopTextColorIndex & (BV_MAX_COLORS - 1)];
}

static VOID EnsureTextReady(VOID)
{
    if (GopTextReady) return;

    ULONG marginX = (GopWidth  > 64) ? 32 : 0;
    ULONG marginY = (GopHeight > 64) ? 32 : 0;

    GopScrollLeft = (marginX < GopWidth) ? marginX : 0;
    GopScrollTop = (marginY < GopHeight) ? marginY : 0;
    GopScrollRight = (marginX < GopWidth && GopWidth > marginX)
        ? GopWidth - marginX - 1
        : (GopWidth ? GopWidth - 1 : 0);
    GopScrollBottom = (marginY < GopHeight && GopHeight > marginY)
        ? GopHeight - marginY - 1
        : (GopHeight ? GopHeight - 1 : 0);

    UpdateRegionMetrics();
    ClearLineArea(0);
}

static VOID AdvanceLine(VOID)
{
    if (!GopTextReady) return;

    if (GopMaxLines == 0)
    {
        EnsureTextReady();
        if (!GopTextReady) return;
    }

    if (GopLine + 1 >= GopMaxLines)
    {
        ScrollTextUp();
        GopLine = (GopMaxLines > 0) ? GopMaxLines - 1 : 0;
        ClearLineArea(GopLine);
    }
    else
    {
        ++GopLine;
        ClearLineArea(GopLine);
    }
    GopColumn = 0;
}

static VOID WriteGlyph(UCHAR ch)
{
    const UCHAR* glyph = &GopFont8x16[(SIZE_T)ch * GopFontHeight];
    ULONG left = GopScrollLeft + GopColumn * GopFontWidth;
    ULONG top = GopScrollTop + GopLine * GopLineHeight;

    for (ULONG row = 0; row < GopFontHeight; ++row)
    {
        UCHAR bits = glyph[row];
        ULONG y = top + row;
        if (y > GopScrollBottom) break;
        for (ULONG col = 0; col < GopFontWidth; ++col)
        {
            if (bits & (0x80 >> col))
            {
                ULONG x = left + col;
                if (x > GopScrollRight) break;
                WritePixel(x, y, GopTextColor);
            }
        }
    }
}

static VOID WriteChar(UCHAR ch)
{
    EnsureTextReady();
    if (!GopTextReady) return;

    switch (ch)
    {
    case '\r':
        GopColumn = 0;
        break;
    case '\n':
        AdvanceLine();
        break;
    case '\t':
    {
        ULONG nextColumn = ((GopColumn / GopTabColumns) + 1) * GopTabColumns;
        if (nextColumn >= GopCharsPerLine)
        {
            AdvanceLine();
        }
        else
        {
            for (; GopColumn < nextColumn; ++GopColumn)
                ClearCharCell(GopColumn, GopLine);
        }
        break;
    }
    case '\b':
        if (GopColumn > 0)
        {
            --GopColumn;
            ClearCharCell(GopColumn, GopLine);
        }
        break;
    default:
        if (ch < 32)
            break;

        if (GopColumn >= GopCharsPerLine)
            AdvanceLine();

        ClearCharCell(GopColumn, GopLine);
        WriteGlyph(ch);
        ++GopColumn;
        break;
    }
}

BOOLEAN NTAPI GopVidInitialize(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) return FALSE;
    if (!LoaderBlock || !LoaderBlock->Extension) return FALSE;

    PLOADER_PARAMETER_EXTENSION Ext = LoaderBlock->Extension;
    if (Ext->GopFramebuffer.FrameBufferBase.QuadPart == 0 || Ext->GopFramebuffer.FrameBufferSize == 0) return FALSE;

    GopWidth  = Ext->GopFramebuffer.HorizontalResolution;
    GopHeight = Ext->GopFramebuffer.VerticalResolution;
    GopPsl    = Ext->GopFramebuffer.PixelsPerScanLine;
    GopFormat = Ext->GopFramebuffer.PixelFormat;
    RMask     = Ext->GopFramebuffer.RedMask;
    GMask     = Ext->GopFramebuffer.GreenMask;
    BMask     = Ext->GopFramebuffer.BlueMask;

    SIZE_T denom = (SIZE_T)GopPsl * (SIZE_T)GopHeight;
    ULONG bpp_from_size = (denom && (Ext->GopFramebuffer.FrameBufferSize % denom) == 0)
                          ? (ULONG)(Ext->GopFramebuffer.FrameBufferSize / denom)
                          : 0;

    switch (GopFormat)
    {
        case 0:
        case 1:
            GopBpp = 4;
            break;
        case 2:
        {
            if (bpp_from_size == 2 || bpp_from_size == 4) GopBpp = bpp_from_size;
            else
            {
                ULONG masks = RMask | GMask | BMask;
                GopBpp = (masks <= 0xFFFF) ? 2 : 4;
            }
            ComputeMaskInfo(RMask, &RMShift, &RWidth);
            ComputeMaskInfo(GMask, &GMShift, &GWidth);
            ComputeMaskInfo(BMask, &BMShift, &BWidth);
            break;
        }
        default:
            return FALSE;
    }

    if (GopBpp == 3) GopBpp = 4;

    GopPitch  = GopPsl * GopBpp;
    GopFbSize = (SIZE_T)Ext->GopFramebuffer.FrameBufferSize;
    GopFbPhys = Ext->GopFramebuffer.FrameBufferBase;

    GopFbBase = MmMapIoSpace(GopFbPhys, GopFbSize, MmWriteCombined);
    if (!GopFbBase) GopFbBase = MmMapIoSpace(GopFbPhys, GopFbSize, MmNonCached);
    if (!GopFbBase) return FALSE;

    SIZE_T total = (SIZE_T)GopPitch * (SIZE_T)GopHeight;
    if (total <= GopFbSize) RtlZeroMemory(GopFbBase, total); else RtlZeroMemory(GopFbBase, GopFbSize);

    if (Ext->BgrtInfo.Valid)
    {
        BgrtX    = Ext->BgrtInfo.ImageOffsetX;
        BgrtY    = Ext->BgrtInfo.ImageOffsetY;
        BgrtAddr = Ext->BgrtInfo.ImageAddress;
        BgrtSize = Ext->BgrtInfo.ImageSize;
        BgrtW = 0; BgrtH = 0;

#pragma pack(push,1)
        typedef struct { USHORT bfType; ULONG bfSize; USHORT r1; USHORT r2; ULONG bfOffBits; } BMPFILEHEADER;
#pragma pack(pop)
        if (BgrtAddr && BgrtSize >= (sizeof(BMPFILEHEADER) + sizeof(BITMAPINFOHEADER)))
        {
            PUCHAR p = (PUCHAR)(ULONG_PTR)BgrtAddr;
            BMPFILEHEADER* bfh = (BMPFILEHEADER*)p;
            BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(p + sizeof(BMPFILEHEADER));
            if (bfh->bfType == 0x4D42 && bih->biWidth > 0 && bih->biHeight != 0)
            {
                BgrtW = (ULONG)bih->biWidth;
                BgrtH = (ULONG)((bih->biHeight < 0) ? -bih->biHeight : bih->biHeight);
            }
        }
        BgrtValid = (BgrtW && BgrtH);
    }
    else BgrtValid = FALSE;

    DPRINT1("[GOP] %ux%u PSL=%u Bpp=%u Fmt=%lu\n", GopWidth, GopHeight, GopPsl, GopBpp, GopFormat);
    ResetGopPalette();
    GopTextReady = FALSE;
    return TRUE;
}

VOID NTAPI GopVidCleanUp(VOID)
{
    if (GopFbBase) { MmUnmapIoSpace(GopFbBase, GopFbSize); GopFbBase = NULL; }
}

VOID NTAPI GopVidResetDisplay(BOOLEAN HalReset)
{
    UNREFERENCED_PARAMETER(HalReset);
    if (!GopFbBase) return;

    if (!BgrtValid)
    {
        SIZE_T total = (SIZE_T)GopPitch * (SIZE_T)GopHeight;
        if (total <= GopFbSize) RtlZeroMemory(GopFbBase, total); else RtlZeroMemory(GopFbBase, GopFbSize);
        GopTextReady = FALSE;
        ResetGopPalette();
        return;
    }

    for (ULONG y = 0; y < GopHeight; ++y)
    {
        PUCHAR row = GopFbBase + (SIZE_T)y * GopPitch;
        if (y < BgrtY || y >= (BgrtY + BgrtH))
        {
            RtlZeroMemory(row, (SIZE_T)GopPsl * GopBpp);
        }
        else
        {
            if (BgrtX > 0) RtlZeroMemory(row, (SIZE_T)BgrtX * GopBpp);
            ULONG after = BgrtX + BgrtW;
            if (after < GopWidth)
            {
                RtlZeroMemory(row + (SIZE_T)after * GopBpp,
                              ((SIZE_T)GopPsl - after) * GopBpp);
            }
        }
    }
    GopTextReady = FALSE;
    ResetGopPalette();
}

VOID NTAPI GopVidSolidColorFill(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, UCHAR Color)
{
    if (!GopFbBase) return;

    LONG l = (LONG)Left, t = (LONG)Top, r = (LONG)Right, b = (LONG)Bottom;
    if (RectEmpty(l,t,r,b)) return;

    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if ((ULONG)r >= GopWidth)  r = (LONG)GopWidth  - 1;
    if ((ULONG)b >= GopHeight) b = (LONG)GopHeight - 1;
    if (RectEmpty(l,t,r,b)) return;

    const ULONG rgb = GopPalette[Color & (BV_MAX_COLORS - 1)];

    if (!BgrtValid && GopBpp == 4 && (ULONG)l <= (ULONG)r)
    {
        const ULONG packed = PackRGB888((UCHAR)((rgb >> 16) & 0xFF),
                                        (UCHAR)((rgb >> 8)  & 0xFF),
                                        (UCHAR)( rgb        & 0xFF));
        SIZE_T npx = (SIZE_T)(r - l + 1);
        for (ULONG y = (ULONG)t; y <= (ULONG)b; ++y)
        {
            PULONG row = (PULONG)(GopFbBase + (SIZE_T)y * GopPitch + (SIZE_T)l * 4);
            SIZE_T bytes = npx * 4;
            RtlFillMemoryUlong(row, (ULONG)bytes, packed);
        }
        return;
    }

    for (ULONG y = (ULONG)t; y <= (ULONG)b; ++y)
        for (ULONG x = (ULONG)l; x <= (ULONG)r; ++x)
            WritePixel(x, y, rgb);
}

VOID NTAPI GopVidBufferToScreenBlt(PUCHAR Buffer, ULONG Left, ULONG Top, ULONG Width, ULONG Height, ULONG Delta)
{
    if (!GopFbBase || !Buffer) return;
    if (!Width || !Height) return;
    if (Left >= GopWidth || Top >= GopHeight) return;

    if (Left + Width  > GopWidth)  Width  = GopWidth  - Left;
    if (Top  + Height > GopHeight) Height = GopHeight - Top;

    ULONG srcBpp = 0;
    if (Delta >= Width * 4) srcBpp = 4;
    else if (Delta >= Width * 3) srcBpp = 3;
    else srcBpp = 1;

    for (ULONG y = 0; y < Height; ++y)
    {
        PUCHAR src = Buffer + (SIZE_T)y * Delta;
        for (ULONG x = 0; x < Width; ++x)
        {
            ULONG rgb;
            if (srcBpp == 4)
            {
                ULONG spx = *(PULONG)(src + (SIZE_T)x * 4); /* BGRA/BGRX in memory */
                UCHAR b = (UCHAR)(spx & 0xFF);
                UCHAR g = (UCHAR)((spx >> 8) & 0xFF);
                UCHAR r = (UCHAR)((spx >> 16) & 0xFF);
                rgb = ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
            }
            else if (srcBpp == 3)
            {
                rgb = ((ULONG)src[x*3 + 2] << 16) |
                      ((ULONG)src[x*3 + 1] << 8)  |
                       (ULONG)src[x*3 + 0];
            }
            else
            {
                rgb = GopPalette[src[x] & (BV_MAX_COLORS - 1)];
            }
            WritePixel(Left + x, Top + y, rgb);
        }
    }
}

VOID NTAPI GopVidScreenToBufferBlt(PUCHAR Buffer, ULONG Left, ULONG Top, ULONG Width, ULONG Height, ULONG Delta)
{
    if (!GopFbBase || !Buffer) return;
    if (!Width || !Height) return;
    if (Left >= GopWidth || Top >= GopHeight) return;

    if (Left + Width  > GopWidth)  Width  = GopWidth  - Left;
    if (Top  + Height > GopHeight) Height = GopHeight - Top;

    ULONG dstBpp = 0;
    if (Delta >= Width * 4) dstBpp = 4;
    else if (Delta >= Width * 3) dstBpp = 3;
    else dstBpp = 1;

    for (ULONG y = 0; y < Height; ++y)
    {
        PUCHAR dst = Buffer + (SIZE_T)y * Delta;
        for (ULONG x = 0; x < Width; ++x)
        {
            PUCHAR p = GopFbBase + (SIZE_T)(Top + y) * GopPitch + (SIZE_T)(Left + x) * GopBpp;
            ULONG px = 0;
            if (GopBpp == 4) px = *(PULONG)p;
            else if (GopBpp == 2) px = *(PUSHORT)p;
            else px = (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16);

            const ULONG rgb = UnpackToRGB888(px);

            if (dstBpp == 4)
            {
                *(PULONG)(dst + (SIZE_T)x * 4) = rgb; /* 0x00RRGGBB */
            }
            else if (dstBpp == 3)
            {
                dst[x*3 + 0] = (UCHAR)( rgb        & 0xFF);
                dst[x*3 + 1] = (UCHAR)((rgb >>  8) & 0xFF);
                dst[x*3 + 2] = (UCHAR)((rgb >> 16) & 0xFF);
            }
            else
            {
                UCHAR r = (UCHAR)((rgb >> 16) & 0xFF);
                UCHAR g = (UCHAR)((rgb >>  8) & 0xFF);
                UCHAR b = (UCHAR)( rgb        & 0xFF);
                UCHAR idx =
                    (r > 128 && g > 128 && b > 128) ? 15 :
                    (r > 128) ? 4 :
                    (g > 128) ? 2 :
                    (b > 128) ? 1 : 0;
                dst[x] = idx;
            }
        }
    }
}

VOID NTAPI GopVidSetScrollRegion(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom)
{
    GopScrollLeft = Left;
    GopScrollTop = Top;
    GopScrollRight = Right;
    GopScrollBottom = Bottom;
    UpdateRegionMetrics();
    ClearLineArea(0);
}

VOID NTAPI GopVidSetTextColor(UCHAR Color)
{
    GopTextColorIndex = Color & (BV_MAX_COLORS - 1);
    GopTextColor = GopPalette[GopTextColorIndex];
}

VOID NTAPI GopVidDisplayString(PUCHAR String)
{
    if (!String) return;

    while (*String)
    {
        UCHAR ch = *String++;
        WriteChar(ch);

        if (ch == '\r')
        {
            if (*String == '\n')
            {
                WriteChar(*String++);
                DbgPrint("\n");
                continue;
            }
            DbgPrint("\r");
        }
        else if (ch == '\n')
        {
            DbgPrint("\n");
        }
        else
        {
            DbgPrint("%c", ch);
        }
    }
}

VOID NTAPI GopVidBitBlt(PUCHAR Buffer, ULONG Left, ULONG Top)
{
    if (!Buffer || !GopFbBase) return;

    PBITMAPINFOHEADER bih = (PBITMAPINFOHEADER)Buffer;
    if (bih->biSize < sizeof(BITMAPINFOHEADER)) return;

    ULONG Width = (ULONG)bih->biWidth;
    ULONG Height = (ULONG)((bih->biHeight < 0) ? -bih->biHeight : bih->biHeight);
    ULONG BitCount = bih->biBitCount;
    ULONG Compression = bih->biCompression;

    ULONG PaletteCount = bih->biClrUsed ? bih->biClrUsed : ((BitCount <= 8) ? (1u << BitCount) : 0u);
    if (BitCount == 4 && PaletteCount < 16) PaletteCount = 16;
    if (PaletteCount > 256) PaletteCount = 256;

    PUCHAR afterHeader = Buffer + bih->biSize;
    PUCHAR paletteEndCandidate = afterHeader + (SIZE_T)PaletteCount * sizeof(ULONG);
    if (paletteEndCandidate < afterHeader) return; /* overflow guard */

    PULONG Palette = (PULONG)afterHeader;

    ULONG BitmapPalette[256] = {0};
    ULONG LoadedPalette = (PaletteCount <= 256) ? PaletteCount : 256;
    BOOLEAN HasPixels = (Width != 0 && Height != 0);
    BOOLEAN PaletteHasColorData = FALSE;

    for (ULONG i = 0; i < LoadedPalette; i++)
    {
        ULONG bgra = Palette[i];
        UCHAR b = (UCHAR)(bgra & 0xFF);
        UCHAR g = (UCHAR)((bgra >> 8) & 0xFF);
        UCHAR r = (UCHAR)((bgra >> 16) & 0xFF);
        ULONG rgb = ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
        if ((bgra & 0x00FFFFFF) != 0)
        {
            PaletteHasColorData = TRUE;
        }
        BitmapPalette[i] = rgb;
    }

    BOOLEAN ShouldUpdatePalette = (!HasPixels) || PaletteHasColorData;
    if (ShouldUpdatePalette)
    {
        for (ULONG i = 0; i < LoadedPalette && i < BV_MAX_COLORS; i++)
        {
            ULONG rgb = BitmapPalette[i];
            GopPalette[i] = rgb;
            if (i == GopTextColorIndex)
            {
                GopTextColor = rgb;
            }
        }
    }

    BOOLEAN UseBitmapPalette = PaletteHasColorData;

    LONG Delta = ((BitCount * Width + 31) / 32) * 4;
    PUCHAR DataStart = Buffer + sizeof(BITMAPINFOHEADER) + (SIZE_T)PaletteCount * sizeof(ULONG);

    SIZE_T expected;
    if (bih->biSizeImage) expected = (SIZE_T)bih->biSizeImage;
    else expected = (SIZE_T)((((BitCount * Width + 31) / 32) * 4) * Height);

    SIZE_T maxReasonable =
        (BitCount == 4) ? (SIZE_T)Delta * Height :
        (SIZE_T)Delta * Height;

    SIZE_T dataSize = (expected <= maxReasonable) ? expected : maxReasonable;
    PUCHAR DataEnd = DataStart + dataSize;
    if (DataEnd < DataStart) return; /* overflow guard */

    BOOLEAN IsTopDown = (bih->biHeight < 0);

    if (Compression == 2 && BitCount == 4)
    {
        ULONG CurrentY = IsTopDown ? Top : (Top + Height - 1);
        ULONG CurrentX = Left;
        LONG YInc = IsTopDown ? 1 : -1;
        PUCHAR BitmapOffset = DataStart;

        while (BitmapOffset < DataEnd)
        {
            if ((SIZE_T)(DataEnd - BitmapOffset) < 1) break;
            ULONG RleValue = *BitmapOffset++;
            if (RleValue)
            {
                if ((SIZE_T)(DataEnd - BitmapOffset) < 1) break;
                ULONG NewRleValue = *BitmapOffset++;
                ULONG Color1 = (NewRleValue >> 4) & 0x0F;
                ULONG Color2 = NewRleValue & 0x0F;
                for (ULONG i = 0; i < RleValue; i++)
                {
                    ULONG ColorIndex = (i & 1) ? Color2 : Color1;
                    ULONG ColorRgb;
                    if (UseBitmapPalette && ColorIndex < LoadedPalette)
                    {
                        ColorRgb = BitmapPalette[ColorIndex];
                    }
                    else
                    {
                        ColorRgb = GopPalette[ColorIndex & (BV_MAX_COLORS - 1)];
                    }
                    if (CurrentX < GopWidth && CurrentY < GopHeight &&
                        CurrentX >= Left && CurrentX < (Left + Width) &&
                        CurrentY >= Top && CurrentY < (Top + Height))
                    {
                        WritePixel(CurrentX, CurrentY, ColorRgb);
                    }
                    CurrentX++;
                }
            }
            else
            {
                if ((SIZE_T)(DataEnd - BitmapOffset) < 1) break;
                ULONG Esc = *BitmapOffset++;
                if (Esc == 0)
                {
                    CurrentY += YInc;
                    CurrentX = Left;
                }
                else if (Esc == 1)
                {
                    break;
                }
                else if (Esc == 2)
                {
                    if ((SIZE_T)(DataEnd - BitmapOffset) < 2) break;
                    CurrentX += *BitmapOffset++;
                    CurrentY += ((ULONG)(*BitmapOffset++)) * YInc;
                }
                else
                {
                    ULONG j = Esc;
                    for (ULONG i = 0; i < j; i++)
                    {
                        if (BitmapOffset >= DataEnd) break;
                        ULONG Code;
                        if ((i & 1) == 0)
                        {
                            Code = (*BitmapOffset >> 4) & 0x0F;
                        }
                        else
                        {
                            Code = *BitmapOffset & 0x0F;
                            BitmapOffset++;
                        }
                        if (CurrentX < GopWidth && CurrentY < GopHeight &&
                            CurrentX >= Left && CurrentX < (Left + Width) &&
                            CurrentY >= Top && CurrentY < (Top + Height))
                        {
                            ULONG CodeRgb;
                            if (UseBitmapPalette && Code < LoadedPalette)
                            {
                                CodeRgb = BitmapPalette[Code];
                            }
                            else
                            {
                                CodeRgb = GopPalette[Code & (BV_MAX_COLORS - 1)];
                            }
                            WritePixel(CurrentX, CurrentY, CodeRgb);
                        }
                        CurrentX++;
                    }
                    if ((j & 1) && BitmapOffset < DataEnd) BitmapOffset++;
                    if (((ULONG_PTR)BitmapOffset & 1) && BitmapOffset < DataEnd) BitmapOffset++;
                }
            }
        }
        return;
    }
    else if (BitCount == 4 && Compression == 0)
    {
        for (ULONG y = 0; y < Height; y++)
        {
            PUCHAR rowBase = IsTopDown
                ? (DataStart + (SIZE_T)y * Delta)
                : (DataStart + (SIZE_T)(Height - 1 - y) * Delta);

            PUCHAR InputBuffer = rowBase;
            UCHAR Colors = 0;
            for (ULONG x = 0; x < Width; x++)
            {
                ULONG ColorRgb;
                if ((x & 1) == 0)
                {
                    if (InputBuffer >= DataEnd) return;
                    Colors = *InputBuffer;
                    ULONG Index = (Colors >> 4) & 0x0F;
                    if (UseBitmapPalette && Index < LoadedPalette)
                    {
                        ColorRgb = BitmapPalette[Index];
                    }
                    else
                    {
                        ColorRgb = GopPalette[Index & (BV_MAX_COLORS - 1)];
                    }
                }
                else
                {
                    ULONG Index = Colors & 0x0F;
                    if (UseBitmapPalette && Index < LoadedPalette)
                    {
                        ColorRgb = BitmapPalette[Index];
                    }
                    else
                    {
                        ColorRgb = GopPalette[Index & (BV_MAX_COLORS - 1)];
                    }
                    InputBuffer++;
                }
                if ((Left + x) < GopWidth && (Top + y) < GopHeight)
                    WritePixel(Left + x, Top + y, ColorRgb);
            }
        }
        return;
    }
}
