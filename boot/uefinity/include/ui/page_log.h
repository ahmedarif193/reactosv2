#pragma once

#include "ui/page.h"

typedef struct UI_LogPageState {
    const CHAR* title;
    const CHAR** snapshot;
    size_t snapshot_count;
    size_t top_index;
    unsigned scroll_accel;
    int last_direction;
    UI_Page* parent_page;
    UI_Page** active_page_slot;
    const CHAR* status_text;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL background_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL title_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL text_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg;
} UI_LogPageState;

void UiLogPageStateInit(UI_LogPageState* state,
                        const CHAR* title,
                        UI_Page* parent_page,
                        UI_Page** active_page_slot,
                        const CHAR* status_text,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL background_color,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL title_color,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL text_color,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg);

extern const UI_PageOps UiLogPageOps;
