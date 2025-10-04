#pragma once

#include <uefildr.h>
#include <stddef.h>

#include "ui/display.h"
#include "ui/page.h"

typedef struct UI_MenuItem {
    const char* label;
    UI_Page* target_page;
} UI_MenuItem;

typedef struct UI_MenuLayout {
    unsigned left;
    unsigned top;
    unsigned width;
    unsigned item_height;
    unsigned item_spacing;
} UI_MenuLayout;

typedef struct UI_Menu {
    UI_MenuItem* items;
    size_t item_count;
    size_t selected_index;
    UI_MenuLayout layout;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL background_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL highlight_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL text_color;
} UI_Menu;

void UiMenuInitialize(UI_Menu* menu, UI_MenuItem* items, size_t count);
void UiMenuApplyDefaultStyle(UI_Menu* menu);
void UiMenuComputeLayout(UI_Menu* menu, const UI_DisplayContext* ctx);
EFI_STATUS UiMenuRender(const UI_Menu* menu, const UI_DisplayContext* ctx);
EFI_STATUS UiMenuHandleInput(UI_Menu* menu, EFI_INPUT_KEY key);
UI_Page* UiMenuGetSelected(const UI_Menu* menu);

typedef struct UI_MenuPageState {
    UI_Menu menu;
    UI_Page** active_page_slot;
    const char* status_text;
    UI_Page* parent_page;
    UI_Page* advanced_page;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg;
} UI_MenuPageState;

void UiMenuPageStateInit(UI_MenuPageState* state,
                         UI_MenuItem* items,
                         size_t count,
                         UI_Page** active_page_slot,
                         const char* status_text,
                         UI_Page* parent_page,
                         UI_Page* advanced_page,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg);

extern const UI_PageOps UiMenuPageOps;
