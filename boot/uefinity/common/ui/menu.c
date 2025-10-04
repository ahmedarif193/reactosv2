#include "ui/menu.h"

#include "ui/display.h"

void
UiMenuInitialize(UI_Menu* menu, UI_MenuItem* items, size_t count)
{
    if (!menu)
        return;

    menu->items = items;
    menu->item_count = count;
    menu->selected_index = 0;
    menu->layout.left = 0;
    menu->layout.top = 0;
    menu->layout.width = 0;
    menu->layout.item_height = 0;
    menu->layout.item_spacing = 0;
}

void
UiMenuApplyDefaultStyle(UI_Menu* menu)
{
    if (!menu)
        return;

    menu->background_color = UiDisplayMakeColor(0x20, 0x20, 0x28);
    menu->highlight_color  = UiDisplayMakeColor(0x2F, 0x6F, 0xFF);
    menu->text_color       = UiDisplayMakeColor(0xEE, 0xEE, 0xEE);
}

void
UiMenuComputeLayout(UI_Menu* menu, const UI_DisplayContext* ctx)
{
    if (!menu || !ctx)
        return;

    unsigned margin = ctx->margin_px;
    unsigned status_height = UiDisplayGetStatusBarHeight(ctx);
    unsigned usable_width = (ctx->screen_width > 2 * margin)
        ? (ctx->screen_width - 2 * margin)
        : ctx->screen_width;

    menu->layout.left = margin;
    menu->layout.top = margin;
    menu->layout.width = usable_width;

    unsigned base_height = UiDisplayDpiScaled(ctx, 60);
    unsigned min_height = UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 16);
    if (base_height < min_height)
        base_height = min_height;
    menu->layout.item_height = base_height;
    menu->layout.item_spacing = UiDisplayDpiScaled(ctx, 18);

    unsigned total_needed = menu->item_count * menu->layout.item_height;
    if (menu->item_count > 1)
        total_needed += (menu->item_count - 1) * menu->layout.item_spacing;
    unsigned available_height = (ctx->screen_height > margin + status_height)
        ? (ctx->screen_height - status_height - margin)
        : ctx->screen_height;
    if (total_needed + margin > available_height && menu->item_count > 0)
    {
        unsigned shrink_height = (available_height - margin) / menu->item_count;
        if (shrink_height >= min_height)
            menu->layout.item_height = shrink_height;
    }
}

static VOID
UiMenuRenderItem(const UI_Menu* menu,
                 const UI_DisplayContext* ctx,
                 const UI_MenuItem* item,
                 size_t index,
                 BOOLEAN selected)
{
    unsigned item_y = menu->layout.top + index * (menu->layout.item_height + menu->layout.item_spacing);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL base_bg = menu->background_color;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL text_fg = selected ? UiDisplayMakeColor(0xFF, 0xFF, 0xFF) : menu->text_color;

    EFI_STATUS base = UiDisplayFillRoundedRectAlpha(ctx,
                                                   menu->layout.left,
                                                   item_y,
                                                   menu->layout.width,
                                                   menu->layout.item_height,
                                                   base_bg,
                                                   0.35f,
                                                   10.0f);
    if (EFI_ERROR(base))
    {
        UiDisplayFillRect(ctx,
                          menu->layout.left,
                          item_y,
                          menu->layout.width,
                          menu->layout.item_height,
                          base_bg);
    }

    if (selected)
    {
        EFI_STATUS highlight = UiDisplayFillRoundedRectAlpha(ctx,
                                                             menu->layout.left,
                                                             item_y,
                                                             menu->layout.width,
                                                             menu->layout.item_height,
                                                             menu->highlight_color,
                                                             0.65f,
                                                             12.5f);
        if (EFI_ERROR(highlight))
        {
            UiDisplayFillRect(ctx,
                              menu->layout.left,
                              item_y,
                              menu->layout.width,
                              menu->layout.item_height,
                              menu->highlight_color);
        }
    }

    unsigned text_offset_x = UiDisplayDpiScaled(ctx, 24);
    unsigned text_offset_y = (menu->layout.item_height > UI_FONT_HEIGHT)
        ? (menu->layout.item_height - UI_FONT_HEIGHT) / 2
        : 0;

    const char* label = item->label ? item->label : "";
    unsigned text_x = menu->layout.left + text_offset_x;
    unsigned text_y = item_y + text_offset_y;

    if (selected)
    {
        UiDisplayDrawTextTransparent(ctx,
                                     text_x,
                                     text_y,
                                     label,
                                     text_fg);
    }
    else
    {
        UiDisplayDrawText(ctx,
                          text_x,
                          text_y,
                          label,
                          text_fg,
                          base_bg);
    }
}

EFI_STATUS
UiMenuRender(const UI_Menu* menu, const UI_DisplayContext* ctx)
{
    if (!menu || !ctx || !ctx->gop)
        return EFI_INVALID_PARAMETER;

    for (size_t i = 0; i < menu->item_count; ++i)
    {
        const UI_MenuItem* item = &menu->items[i];
        BOOLEAN selected = (i == menu->selected_index);
        UiMenuRenderItem(menu, ctx, item, i, selected);
    }

    return EFI_SUCCESS;
}

EFI_STATUS
UiMenuHandleInput(UI_Menu* menu, EFI_INPUT_KEY key)
{
    if (!menu)
        return EFI_INVALID_PARAMETER;

    if (menu->item_count == 0)
        return EFI_SUCCESS;

    switch (key.ScanCode)
    {
        case SCAN_UP:
            if (menu->selected_index == 0)
                menu->selected_index = menu->item_count - 1;
            else
                --menu->selected_index;
            break;
        case SCAN_DOWN:
            menu->selected_index = (menu->selected_index + 1) % menu->item_count;
            break;
        case SCAN_HOME:
            menu->selected_index = 0;
            break;
        case SCAN_END:
            menu->selected_index = menu->item_count - 1;
            break;
        default:
            break;
    }

    return EFI_SUCCESS;
}

UI_Page*
UiMenuGetSelected(const UI_Menu* menu)
{
    if (!menu || menu->item_count == 0)
        return NULL;
    if (menu->selected_index >= menu->item_count)
        return NULL;
    return menu->items[menu->selected_index].target_page;
}

void
UiMenuPageStateInit(UI_MenuPageState* state,
                    UI_MenuItem* items,
                    size_t count,
                    UI_Page** active_page_slot,
                    const char* status_text,
                    UI_Page* parent_page,
                    UI_Page* advanced_page,
                    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg)
{
    if (!state)
        return;

    UiMenuInitialize(&state->menu, items, count);
    UiMenuApplyDefaultStyle(&state->menu);
    state->active_page_slot = active_page_slot;
    state->status_text = status_text;
    state->parent_page = parent_page;
    state->advanced_page = advanced_page;
    state->status_bg = status_bg;
    state->status_fg = status_fg;
}

static UI_MenuPageState*
UiMenuPageStateFromPage(UI_Page* page)
{
    return page ? (UI_MenuPageState*)page->state : NULL;
}

static EFI_STATUS
UiMenuPageInit(UI_Page* page, const UI_DisplayContext* ctx)
{
    UI_MenuPageState* state = UiMenuPageStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    state->menu.selected_index = 0;
    UiMenuComputeLayout(&state->menu, ctx);
    return EFI_SUCCESS;
}

static EFI_STATUS
UiMenuPageConstruct(UI_Page* page, const UI_DisplayContext* ctx)
{
    UI_MenuPageState* state = UiMenuPageStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    state->menu.selected_index = 0;
    UiMenuComputeLayout(&state->menu, ctx);
    return EFI_SUCCESS;
}

static EFI_STATUS
UiMenuPageRenderImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    UI_MenuPageState* state = UiMenuPageStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    EFI_STATUS st = UiDisplayFillRect(ctx,
                                      0,
                                      0,
                                      ctx->screen_width,
                                      ctx->screen_height,
                                      state->menu.background_color);
    if (EFI_ERROR(st))
        return st;

    st = UiMenuRender(&state->menu, ctx);
    if (EFI_ERROR(st))
        return st;

    return UiDisplayDrawStatusBar(ctx,
                                  state->status_text ? state->status_text : "",
                                  state->status_bg,
                                  state->status_fg);
}

static EFI_STATUS
UiMenuPageHandleInputImpl(UI_Page* page, const UI_DisplayContext* ctx, EFI_INPUT_KEY key)
{
    UI_MenuPageState* state = UiMenuPageStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    if (key.ScanCode == SCAN_F8 && state->advanced_page)
    {
        if (state->active_page_slot)
        {
            *state->active_page_slot = state->advanced_page;
            state->advanced_page->constructed = FALSE;
        }
        return EFI_SUCCESS;
    }

    if ((key.ScanCode == SCAN_ESC || key.UnicodeChar == 0x1b) && state->parent_page)
    {
        if (state->active_page_slot)
        {
            *state->active_page_slot = state->parent_page;
            state->parent_page->constructed = FALSE;
        }
        return EFI_SUCCESS;
    }

    if (key.UnicodeChar == CHAR_CARRIAGE_RETURN)
    {
        UI_Page* target = UiMenuGetSelected(&state->menu);
        if (state->active_page_slot && target)
        {
            *state->active_page_slot = target;
            target->constructed = FALSE;
        }
        return EFI_SUCCESS;
    }

    return UiMenuHandleInput(&state->menu, key);
}

const UI_PageOps UiMenuPageOps = {
    UiMenuPageInit,
    UiMenuPageConstruct,
    UiMenuPageRenderImpl,
    UiMenuPageHandleInputImpl
};
