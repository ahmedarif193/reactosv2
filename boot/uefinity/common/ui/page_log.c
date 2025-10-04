#include "ui/page_log.h"

#include <debug.h>

#include "ui/display.h"

#define LOG_SCROLL_ACCEL_MAX 8U

static unsigned
UiLogPageVisibleLines(const UI_LogPageState* state, const UI_DisplayContext* ctx)
{
    if (!state || !ctx)
        return 0;

    unsigned status_height = UiDisplayGetStatusBarHeight(ctx);
    unsigned margin = ctx->margin_px;
    unsigned title_block = UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 24);
    unsigned available = (ctx->screen_height > margin + status_height + title_block)
        ? (ctx->screen_height - status_height - margin - title_block)
        : 0;
    unsigned line_step = UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 6);
    if (line_step == 0)
        line_step = UI_FONT_HEIGHT + 1;
    unsigned visible = (available / line_step);
    if (visible == 0)
        visible = 1;
    return visible;
}

void
UiLogPageStateInit(UI_LogPageState* state,
                   const CHAR* title,
                   UI_Page* parent_page,
                   UI_Page** active_page_slot,
                   const CHAR* status_text,
                   EFI_GRAPHICS_OUTPUT_BLT_PIXEL background_color,
                   EFI_GRAPHICS_OUTPUT_BLT_PIXEL title_color,
                   EFI_GRAPHICS_OUTPUT_BLT_PIXEL text_color,
                   EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg,
                   EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg)
{
    if (!state)
        return;

    state->title = title;
    state->snapshot = NULL;
    state->snapshot_count = 0;
    state->top_index = 0;
    state->scroll_accel = 1;
    state->last_direction = 0;
    state->parent_page = parent_page;
    state->active_page_slot = active_page_slot;
    state->status_text = status_text;
    state->background_color = background_color;
    state->title_color = title_color;
    state->text_color = text_color;
    state->status_bg = status_bg;
    state->status_fg = status_fg;
}

static UI_LogPageState*
UiLogStateFromPage(UI_Page* page)
{
    return page ? (UI_LogPageState*)page->state : NULL;
}

static EFI_STATUS
UiLogPageInitImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    (void)ctx;
    UI_LogPageState* state = UiLogStateFromPage(page);
    if (!state)
        return EFI_INVALID_PARAMETER;

    state->top_index = 0;
    state->scroll_accel = 1;
    state->last_direction = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS
UiLogPageConstructImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    return UiLogPageInitImpl(page, ctx);
}

static EFI_STATUS
UiLogPageRenderImpl(UI_Page* page, const UI_DisplayContext* ctx)
{
    UI_LogPageState* state = UiLogStateFromPage(page);
    if (!state || !ctx)
        return EFI_INVALID_PARAMETER;

    UiDisplayFillRect(ctx, 0, 0, ctx->screen_width, ctx->screen_height, state->background_color);

    state->snapshot_count = DebugLogSnapshot(&state->snapshot);
    const CHAR* const* lines = state->snapshot;
    size_t line_count = state->snapshot_count;

    unsigned margin = ctx->margin_px;
    unsigned title_y = margin;
    UiDisplayDrawText(ctx, margin, title_y, state->title ? state->title : "", state->title_color, state->background_color);

    unsigned line_step = UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 6);
    if (line_step == 0)
        line_step = UI_FONT_HEIGHT + 1;

    unsigned content_y = title_y + UI_FONT_HEIGHT + UiDisplayDpiScaled(ctx, 24);
    unsigned visible = UiLogPageVisibleLines(state, ctx);
    size_t max_top = (line_count > visible) ? (line_count - visible) : 0;
    if (state->top_index > max_top)
        state->top_index = max_top;

    for (unsigned i = 0; i < visible; ++i)
    {
        size_t idx = state->top_index + i;
        if (idx >= line_count)
            break;
        unsigned y = content_y + i * line_step;
        UiDisplayDrawText(ctx,
                          margin,
                          y,
                          (lines && lines[idx]) ? lines[idx] : "",
                          state->text_color,
                          state->background_color);
    }

    UiDisplayDrawStatusBar(ctx,
                           state->status_text ? state->status_text : "ESC to return",
                           state->status_bg,
                           state->status_fg);

    return EFI_SUCCESS;
}

static EFI_STATUS
UiLogPageHandleInputImpl(UI_Page* page, const UI_DisplayContext* ctx, EFI_INPUT_KEY key)
{
    UI_LogPageState* state = UiLogStateFromPage(page);
    if (!state || !ctx)
        return EFI_INVALID_PARAMETER;

    unsigned visible = UiLogPageVisibleLines(state, ctx);
    size_t line_count = state->snapshot_count;
    size_t max_top = (line_count > visible) ? (line_count - visible) : 0;

    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 0x1b)
    {
        if (state->active_page_slot && state->parent_page)
        {
            *state->active_page_slot = state->parent_page;
            state->parent_page->constructed = FALSE;
        }
        return EFI_SUCCESS;
    }

    if (line_count == 0)
        return EFI_SUCCESS;

    if (key.ScanCode == SCAN_DOWN)
    {
        if (state->last_direction == 1 && state->scroll_accel < LOG_SCROLL_ACCEL_MAX)
            state->scroll_accel++;
        else if (state->last_direction != 1)
            state->scroll_accel = 1;
        state->last_direction = 1;

        size_t step = state->scroll_accel;
        if (step > line_count)
            step = line_count;
        if (state->top_index + step > max_top)
            state->top_index = max_top;
        else
            state->top_index += step;
        return EFI_SUCCESS;
    }

    if (key.ScanCode == SCAN_UP)
    {
        if (state->last_direction == -1 && state->scroll_accel < LOG_SCROLL_ACCEL_MAX)
            state->scroll_accel++;
        else if (state->last_direction != -1)
            state->scroll_accel = 1;
        state->last_direction = -1;

        size_t step = state->scroll_accel;
        if (step > state->top_index)
            state->top_index = 0;
        else
            state->top_index -= step;
        return EFI_SUCCESS;
    }

    if (key.ScanCode == SCAN_HOME)
    {
        state->top_index = 0;
        state->scroll_accel = 1;
        state->last_direction = 0;
        return EFI_SUCCESS;
    }

    if (key.ScanCode == SCAN_END)
    {
        state->top_index = max_top;
        state->scroll_accel = 1;
        state->last_direction = 0;
        return EFI_SUCCESS;
    }

    state->scroll_accel = 1;
    state->last_direction = 0;
    return EFI_SUCCESS;
}

const UI_PageOps UiLogPageOps = {
    UiLogPageInitImpl,
    UiLogPageConstructImpl,
    UiLogPageRenderImpl,
    UiLogPageHandleInputImpl
};
