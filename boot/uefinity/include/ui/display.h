#pragma once

#include <uefildr.h>
#include <stdint.h>

typedef struct UI_DisplayContext {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_BOOT_SERVICES* boot_services;
    unsigned screen_width;
    unsigned screen_height;
    float dpi_scale;
    unsigned margin_px;
} UI_DisplayContext;

#define UI_FONT_WIDTH   8U
#define UI_FONT_HEIGHT 16U

EFI_STATUS UiDisplayAcquire(UI_DisplayContext* ctx);
VOID UiDisplayRelease(UI_DisplayContext* ctx);

unsigned UiDisplayDpiScaled(const UI_DisplayContext* ctx, unsigned value);

EFI_GRAPHICS_OUTPUT_BLT_PIXEL UiDisplayMakeColor(uint8_t r, uint8_t g, uint8_t b);

EFI_STATUS UiDisplayFillRect(const UI_DisplayContext* ctx,
                             unsigned x, unsigned y,
                             unsigned w, unsigned h,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL color);

EFI_STATUS UiDisplayDrawText(const UI_DisplayContext* ctx,
                             unsigned x, unsigned y,
                             const char* text,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL bg);

EFI_STATUS UiDisplayDrawTextTransparent(const UI_DisplayContext* ctx,
                                        unsigned x, unsigned y,
                                        const char* text,
                                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg);

unsigned UiDisplayMeasureTextWidth(const char* text);

unsigned UiDisplayGetStatusBarHeight(const UI_DisplayContext* ctx);

EFI_STATUS UiDisplayDrawStatusBar(const UI_DisplayContext* ctx,
                                  const char* text,
                                  EFI_GRAPHICS_OUTPUT_BLT_PIXEL bg,
                                  EFI_GRAPHICS_OUTPUT_BLT_PIXEL fg);

EFI_STATUS UiDisplayFillRoundedRectAlpha(const UI_DisplayContext* ctx,
                                         unsigned x, unsigned y,
                                         unsigned w, unsigned h,
                                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL color,
                                         float alpha,
                                         float radius_percent);
