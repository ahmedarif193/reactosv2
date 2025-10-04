#include "ui/display.h"

#include <debug.h>
#include <string.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern UCHAR BitmapFont8x16[256 * 16];

DBG_DEFAULT_CHANNEL(WARNING);

static EFI_GRAPHICS_OUTPUT_PROTOCOL* g_cached_gop = NULL;

static float
UiDisplayComputeDpiScale(unsigned w, unsigned h)
{
    if (w == 0 || h == 0)
        return 1.0f;

    float sx = (float)w / 1280.0f;
    float sy = (float)h / 720.0f;
    float scale = (sx > sy) ? sx : sy;
    if (scale < 1.0f)
        scale = 1.0f;
    return scale;
}

static EFI_STATUS
LocateGop(EFI_GRAPHICS_OUTPUT_PROTOCOL** Gop)
{
    if (!Gop)
        return EFI_INVALID_PARAMETER;

    if (g_cached_gop)
    {
        *Gop = g_cached_gop;
        return EFI_SUCCESS;
    }

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return EFI_ABORTED;

    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS st = GlobalSystemTable->BootServices->LocateProtocol(&GopGuid, 0, (void**)&gop);
    if (EFI_ERROR(st))
        return st;

    if (!gop || !gop->Mode || !gop->Mode->Info)
        return EFI_ABORTED;

    g_cached_gop = gop;
    *Gop = g_cached_gop;
    return EFI_SUCCESS;
}

EFI_STATUS
UiDisplayAcquire(UI_DisplayContext* ctx)
{
    if (!ctx)
        return EFI_INVALID_PARAMETER;

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS st = LocateGop(&gop);
    if (EFI_ERROR(st))
        return st;

    if (!gop || !gop->Mode || !gop->Mode->Info)
        return EFI_ABORTED;

    ctx->gop = gop;
    ctx->boot_services = GlobalSystemTable ? GlobalSystemTable->BootServices : NULL;
    ctx->screen_width = gop->Mode->Info->HorizontalResolution;
    ctx->screen_height = gop->Mode->Info->VerticalResolution;
    ctx->dpi_scale = UiDisplayComputeDpiScale(ctx->screen_width, ctx->screen_height);
    ctx->margin_px = UiDisplayDpiScaled(ctx, 50);
    return EFI_SUCCESS;
}

VOID
UiDisplayRelease(UI_DisplayContext* ctx)
{
    if (!ctx)
        return;

    ctx->gop = NULL;
    ctx->boot_services = NULL;
}

unsigned
UiDisplayDpiScaled(const UI_DisplayContext* ctx, unsigned value)
{
    if (!ctx)
        return value;
    float scaled = (float)value * ctx->dpi_scale;
    if (scaled < 1.0f)
        scaled = 1.0f;
    return (unsigned)(scaled + 0.5f);
}

EFI_GRAPHICS_OUTPUT_BLT_PIXEL
UiDisplayMakeColor(uint8_t r, uint8_t g, uint8_t b)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = {0};
    px.Red = r;
    px.Green = g;
    px.Blue = b;
    px.Reserved = 0x00;
    return px;
}

EFI_STATUS
UiDisplayFillRect(const UI_DisplayContext* ctx,
                  unsigned x, unsigned y,
                  unsigned w, unsigned h,
                  EFI_GRAPHICS_OUTPUT_BLT_PIXEL color)
{
    if (!ctx || !ctx->gop)
        return EFI_INVALID_PARAMETER;

    if (w == 0 || h == 0)
        return EFI_SUCCESS;

    if (x >= ctx->screen_width || y >= ctx->screen_height)
        return EFI_SUCCESS;

    if (x + w > ctx->screen_width)
        w = ctx->screen_width - x;
    if (y + h > ctx->screen_height)
        h = ctx->screen_height - y;

    return ctx->gop->Blt(ctx->gop, &color, EfiBltVideoFill,
                         0, 0,
                         x, y,
                         w, h,
                         0);
}

static VOID
BuildGlyph(uint8_t ch,
           EFI_GRAPHICS_OUTPUT_BLT_PIXEL glyph[UI_FONT_HEIGHT][UI_FONT_WIDTH],
           EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg,
           EFI_GRAPHICS_OUTPUT_BLT_PIXEL bg)
{
    PUCHAR FontPtr = BitmapFont8x16 + ch * UI_FONT_HEIGHT;
    for (unsigned line = 0; line < UI_FONT_HEIGHT; ++line)
    {
        UCHAR mask = 0x80;
        for (unsigned col = 0; col < UI_FONT_WIDTH; ++col)
        {
            glyph[line][col] = (FontPtr[line] & mask) ? fg : bg;
            mask >>= 1;
        }
    }
}

EFI_STATUS
UiDisplayDrawText(const UI_DisplayContext* ctx,
                  unsigned x, unsigned y,
                  const char* text,
                  EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg,
                  EFI_GRAPHICS_OUTPUT_BLT_PIXEL bg)
{
    if (!ctx || !ctx->gop || !text)
        return EFI_INVALID_PARAMETER;

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL glyph[UI_FONT_HEIGHT][UI_FONT_WIDTH];
    unsigned cursor_x = x;
    unsigned cursor_y = y;

    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            cursor_x = x;
            cursor_y += UI_FONT_HEIGHT;
            continue;
        }

        BuildGlyph((uint8_t)*p, glyph, fg, bg);

        ctx->gop->Blt(ctx->gop,
                      (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)glyph,
                      EfiBltBufferToVideo,
                      0, 0,
                      cursor_x, cursor_y,
                      UI_FONT_WIDTH, UI_FONT_HEIGHT,
                      0);

        cursor_x += UI_FONT_WIDTH;
    }

    return EFI_SUCCESS;
}

EFI_STATUS
UiDisplayDrawTextTransparent(const UI_DisplayContext* ctx,
                             unsigned x, unsigned y,
                             const char* text,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg)
{
    if (!ctx || !ctx->gop || !text)
        return EFI_INVALID_PARAMETER;

    unsigned cursor_x = x;
    unsigned cursor_y = y;

    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            cursor_x = x;
            cursor_y += UI_FONT_HEIGHT;
            continue;
        }

        EFI_GRAPHICS_OUTPUT_BLT_PIXEL buffer[UI_FONT_HEIGHT][UI_FONT_WIDTH];
        EFI_STATUS st = ctx->gop->Blt(ctx->gop,
                                      (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)buffer,
                                      EfiBltVideoToBltBuffer,
                                      cursor_x,
                                      cursor_y,
                                      0,
                                      0,
                                      UI_FONT_WIDTH,
                                      UI_FONT_HEIGHT,
                                      0);
        if (EFI_ERROR(st))
            return st;

        unsigned char ch = (unsigned char)*p;
        PUCHAR font_line = BitmapFont8x16 + ch * UI_FONT_HEIGHT;
        for (unsigned line = 0; line < UI_FONT_HEIGHT; ++line)
        {
            UCHAR mask = 0x80;
            for (unsigned col = 0; col < UI_FONT_WIDTH; ++col)
            {
                if (font_line[line] & mask)
                    buffer[line][col] = fg;
                mask >>= 1;
            }
        }

        st = ctx->gop->Blt(ctx->gop,
                           (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)buffer,
                           EfiBltBufferToVideo,
                           0,
                           0,
                           cursor_x,
                           cursor_y,
                           UI_FONT_WIDTH,
                           UI_FONT_HEIGHT,
                           0);
        if (EFI_ERROR(st))
            return st;

        cursor_x += UI_FONT_WIDTH;
    }

    return EFI_SUCCESS;
}

unsigned
UiDisplayMeasureTextWidth(const char* text)
{
    if (!text)
        return 0;

    unsigned current = 0;
    unsigned maximum = 0;
    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            if (current > maximum)
                maximum = current;
            current = 0;
        }
        else
        {
            current += UI_FONT_WIDTH;
        }
    }
    if (current > maximum)
        maximum = current;
    return maximum;
}

unsigned
UiDisplayGetStatusBarHeight(const UI_DisplayContext* ctx)
{
    if (!ctx)
        return 40;
    return UiDisplayDpiScaled(ctx, 40);
}

EFI_STATUS
UiDisplayDrawStatusBar(const UI_DisplayContext* ctx,
                       const char* text,
                       EFI_GRAPHICS_OUTPUT_BLT_PIXEL bg,
                       EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg)
{
    if (!ctx)
        return EFI_INVALID_PARAMETER;

    unsigned bar_height = UiDisplayGetStatusBarHeight(ctx);
    unsigned bar_y = (ctx->screen_height > bar_height) ? (ctx->screen_height - bar_height) : 0;
    EFI_STATUS st = UiDisplayFillRect(ctx, 0, bar_y, ctx->screen_width, bar_height, bg);
    if (EFI_ERROR(st))
        return st;

    if (text && *text)
    {
        unsigned text_x = UiDisplayDpiScaled(ctx, 24);
        unsigned text_y = bar_y;
        if (bar_height > UI_FONT_HEIGHT)
            text_y += (bar_height - UI_FONT_HEIGHT) / 2;
        UiDisplayDrawText(ctx, text_x, text_y, text, fg, bg);
    }

    return EFI_SUCCESS;
}

EFI_STATUS
UiDisplayFillRoundedRectAlpha(const UI_DisplayContext* ctx,
                              unsigned x, unsigned y,
                              unsigned w, unsigned h,
                              EFI_GRAPHICS_OUTPUT_BLT_PIXEL color,
                              float alpha,
                              float radius_percent)
{
    if (!ctx || !ctx->gop || w == 0 || h == 0)
        return EFI_INVALID_PARAMETER;

    if (alpha <= 0.0f)
        return EFI_SUCCESS;
    if (alpha > 1.0f)
        alpha = 1.0f;

    EFI_BOOT_SERVICES* bs = ctx->boot_services;
    if (!bs && GlobalSystemTable)
        bs = GlobalSystemTable->BootServices;
    if (!bs)
        return EFI_UNSUPPORTED;

    size_t buffer_size = (size_t)w * (size_t)h * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL* buffer = NULL;
    EFI_STATUS st = bs->AllocatePool(EfiLoaderData, buffer_size, (VOID**)&buffer);
    if (EFI_ERROR(st) || !buffer)
        return EFI_OUT_OF_RESOURCES;

    st = ctx->gop->Blt(ctx->gop,
                       buffer,
                       EfiBltVideoToBltBuffer,
                       x,
                       y,
                       0,
                       0,
                       w,
                       h,
                       0);
    if (EFI_ERROR(st))
    {
        bs->FreePool(buffer);
        return st;
    }

    UINT32 alpha_u8 = (UINT32)(alpha * 255.0f + 0.5f);
    if (alpha_u8 > 255)
        alpha_u8 = 255;
    UINT32 inv = 255 - alpha_u8;

    unsigned min_dim = (w < h) ? w : h;
    unsigned radius = 0;
    if (radius_percent > 0.0f && min_dim > 0)
    {
        float r = ((radius_percent / 100.0f) * (float)min_dim);
        if (r < 0.5f)
            r = 0.0f;
        radius = (unsigned)(r + 0.5f);
        if (radius > min_dim / 2)
            radius = min_dim / 2;
    }

    unsigned radius_sq = radius * radius;

    for (unsigned iy = 0; iy < h; ++iy)
    {
        for (unsigned ix = 0; ix < w; ++ix)
        {
            BOOLEAN apply = TRUE;
            if (radius > 0)
            {
                if (ix < radius && iy < radius)
                {
                    int dx = (int)radius - 1 - (int)ix;
                    int dy = (int)radius - 1 - (int)iy;
                    if ((dx * dx + dy * dy) > (int)radius_sq)
                        apply = FALSE;
                }
                else if (ix >= w - radius && iy < radius)
                {
                    int dx = (int)ix - (int)(w - radius);
                    int dy = (int)radius - 1 - (int)iy;
                    if ((dx * dx + dy * dy) > (int)radius_sq)
                        apply = FALSE;
                }
                else if (ix < radius && iy >= h - radius)
                {
                    int dx = (int)radius - 1 - (int)ix;
                    int dy = (int)iy - (int)(h - radius);
                    if ((dx * dx + dy * dy) > (int)radius_sq)
                        apply = FALSE;
                }
                else if (ix >= w - radius && iy >= h - radius)
                {
                    int dx = (int)ix - (int)(w - radius);
                    int dy = (int)iy - (int)(h - radius);
                    if ((dx * dx + dy * dy) > (int)radius_sq)
                        apply = FALSE;
                }
            }

            if (!apply)
                continue;

            EFI_GRAPHICS_OUTPUT_BLT_PIXEL* px = buffer + (iy * w + ix);
            UINT32 b = (UINT32)px->Blue;
            UINT32 g = (UINT32)px->Green;
            UINT32 r = (UINT32)px->Red;

            px->Blue  = (UINT8)((color.Blue  * alpha_u8 + b * inv) / 255);
            px->Green = (UINT8)((color.Green * alpha_u8 + g * inv) / 255);
            px->Red   = (UINT8)((color.Red   * alpha_u8 + r * inv) / 255);
        }
    }

    st = ctx->gop->Blt(ctx->gop,
                       buffer,
                       EfiBltBufferToVideo,
                       0,
                       0,
                       x,
                       y,
                       w,
                       h,
                       0);

    bs->FreePool(buffer);
    return st;
}
