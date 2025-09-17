#include "uix_draw.h"

static inline VOID clamp_rect(LONG* x, LONG* y, LONG* w, LONG* h, ULONG W, ULONG H)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > (LONG)W) *w = (LONG)W - *x;
    if (*y + *h > (LONG)H) *h = (LONG)H - *y;
}

BOOLEAN UixFillRect(const UIX_SURFACE* S, LONG x, LONG y, LONG w, LONG h, ULONG argb)
{
    if (!S || !S->Pixels || S->Format != UIX_PF_X8R8G8B8) return FALSE;
    if (w <= 0 || h <= 0) return TRUE;
    clamp_rect(&x, &y, &w, &h, S->Width, S->Height);
    if (w <= 0 || h <= 0) return TRUE;

    ULONG px = UixPackX8R8G8B8(argb);
    PUCHAR row = S->Pixels + (SIZE_T)y * S->Stride + (SIZE_T)x * 4;
    for (LONG j = 0; j < h; ++j)
    {
        PULONG p = (PULONG)row;
        for (LONG i = 0; i < w; ++i) p[i] = px;
        row += S->Stride;
    }
    return TRUE;
}

BOOLEAN UixFillVGradient(const UIX_SURFACE* S, LONG x, LONG y, LONG w, LONG h,
                         ULONG topARGB, ULONG bottomARGB)
{
    if (!S || !S->Pixels || S->Format != UIX_PF_X8R8G8B8) return FALSE;
    if (w <= 0 || h <= 0) return TRUE;
    clamp_rect(&x, &y, &w, &h, S->Width, S->Height);
    if (w <= 0 || h <= 0) return TRUE;

    /* Simple linear interpolation in sRGB (good enough for boot UI) */
    int r0 = (topARGB    >> 16) & 0xFF, r1 = (bottomARGB >> 16) & 0xFF;
    int g0 = (topARGB    >> 8 ) & 0xFF, g1 = (bottomARGB >> 8 ) & 0xFF;
    int b0 = (topARGB    >> 0 ) & 0xFF, b1 = (bottomARGB >> 0 ) & 0xFF;

    PUCHAR row = S->Pixels + (SIZE_T)y * S->Stride + (SIZE_T)x * 4;
    for (LONG j = 0; j < h; ++j)
    {
        int num = (h > 1) ? j : 0;
        int den = (h > 1) ? (h - 1) : 1;
        int r = r0 + (r1 - r0) * num / den;
        int g = g0 + (g1 - g0) * num / den;
        int b = b0 + (b1 - b0) * num / den;
        ULONG px = UixPackX8R8G8B8((0xFFu << 24) | (r << 16) | (g << 8) | b);
        PULONG p = (PULONG)row;
        for (LONG i = 0; i < w; ++i) p[i] = px;
        row += S->Stride;
    }
    return TRUE;
}

