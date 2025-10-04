#pragma once

#include "ui/page.h"

typedef struct UI_StaticPageState {
    const char* title;
    const char* body;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL background_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL title_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL body_color;
    UI_Page* menu_page;
    UI_Page** active_page_slot;
    const char* status_text;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg;
} UI_StaticPageState;

void UiStaticPageStateInit(UI_StaticPageState* state,
                           const char* title,
                           const char* body,
                           UI_Page* menu_page,
                           UI_Page** active_page_slot,
                           const char* status_text,
                           EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                           EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg);

extern const UI_PageOps UiStaticPageOps;
