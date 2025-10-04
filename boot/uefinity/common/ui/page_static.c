#include "ui/page_static.h"

#include "ui/display.h"

void
UiStaticPageStateInit(UI_StaticPageState* state,
                      const char* title,
                      const char* body,
                      UI_Page* menu_page,
                      UI_Page** active_page_slot,
                      const char* status_text,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg)
{
    if (!state)
        return;

    state->title = title;
    state->body = body;
    state->background_color = UiDisplayMakeColor(0x12, 0x12, 0x22);
    state->title_color = UiDisplayMakeColor(0xFF, 0xFF, 0xFF);
    state->body_color = UiDisplayMakeColor(0xD0, 0xD0, 0xD0);
    state->menu_page = menu_page;
    state->active_page_slot = active_page_slot;
    state->status_text = status_text;
    state->status_bg = status_bg;
    state->status_fg = status_fg;
}

static UI_StaticPageState*
UiStaticStateFromPage(UI_Page* page)
{
    return page ? (UI_StaticPageState*)page->state : NULL;
}

static EFI_STATUS
UiStaticPageInitImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    (void)page;
    (void)ctx;
    return EFI_SUCCESS;
}

static EFI_STATUS
UiStaticPageConstructImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    (void)page;
    (void)ctx;
    return EFI_SUCCESS;
}

static EFI_STATUS
UiStaticPageRenderImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    UI_StaticPageState* state = UiStaticStateFromPage(page);
    if (!state || !ctx)
        return EFI_INVALID_PARAMETER;

    UiDisplayFillRect(ctx, 0, 0, ctx->screen_width, ctx->screen_height, state->background_color);

    unsigned title_x = ctx->margin_px;
    unsigned title_y = ctx->margin_px;
    UiDisplayDrawText(ctx, title_x, title_y, state->title ? state->title : "", state->title_color, state->background_color);

    unsigned body_x = ctx->margin_px;
    unsigned body_y = title_y + UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 24);
    UiDisplayDrawText(ctx, body_x, body_y, state->body ? state->body : "", state->body_color, state->background_color);

    UiDisplayDrawStatusBar(ctx,
                           state->status_text ? state->status_text : "ESC to return",
                           state->status_bg,
                           state->status_fg);

    return EFI_SUCCESS;
}

static EFI_STATUS
UiStaticPageHandleInputImpl(UI_Page* page, const UI_DisplayContext* ctx, EFI_INPUT_KEY key)
{
    (void)ctx;
    UI_StaticPageState* state = UiStaticStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 0x1b)
    {
        if (state->active_page_slot && state->menu_page)
        {
            *state->active_page_slot = state->menu_page;
            state->menu_page->constructed = FALSE;
        }
    }

    return EFI_SUCCESS;
}

const UI_PageOps UiStaticPageOps = {
    UiStaticPageInitImpl,
    UiStaticPageConstructImpl,
    UiStaticPageRenderImpl,
    UiStaticPageHandleInputImpl
};
