#include "ui/draw.h"

#include <limits.h>

void
UiResampleRGBA8(const uint8_t* src, unsigned sw, unsigned sh,
                uint8_t* dst, unsigned dw, unsigned dh)
{
    if (!src || !dst || sw == 0 || sh == 0 || dw == 0 || dh == 0)
        return;

    for (unsigned y = 0; y < dh; ++y)
    {
        unsigned sy = (unsigned)((uint64_t)y * sh / dh);
        const uint8_t* srow = src + (size_t)sy * sw * 4;
        uint8_t* drow = dst + (size_t)y * dw * 4;
        for (unsigned x = 0; x < dw; ++x)
        {
            unsigned sx = (unsigned)((uint64_t)x * sw / dw);
            const uint8_t* sp = srow + (size_t)sx * 4;
            uint8_t* dp = drow + (size_t)x * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
}

void
UiRGBAtoBGRA(const uint8_t* rgba, uint8_t* bgra, unsigned w, unsigned h)
{
    if (!rgba || !bgra || w == 0 || h == 0)
        return;

    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t* s = rgba + i * 4;
        uint8_t* d = bgra + i * 4;
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
        d[3] = 0x00;
    }
}

void
UiComputeCoverDimensions(unsigned src_w, unsigned src_h,
                         unsigned screen_w, unsigned screen_h,
                         unsigned* out_w, unsigned* out_h)
{
    if (!out_w || !out_h)
        return;

    if (src_w == 0 || src_h == 0 || screen_w == 0 || screen_h == 0)
    {
        *out_w = screen_w;
        *out_h = screen_h;
        return;
    }

    uint64_t width_prod = (uint64_t)screen_w * (uint64_t)src_h;
    uint64_t height_prod = (uint64_t)screen_h * (uint64_t)src_w;
    uint64_t scale_num;
    uint64_t scale_den;

    if (width_prod >= height_prod)
    {
        scale_num = screen_w;
        scale_den = src_w;
    }
    else
    {
        scale_num = screen_h;
        scale_den = src_h;
    }

    uint64_t tw = ((uint64_t)src_w * scale_num + scale_den - 1) / scale_den;
    uint64_t th = ((uint64_t)src_h * scale_num + scale_den - 1) / scale_den;

    if (tw < screen_w) tw = screen_w;
    if (th < screen_h) th = screen_h;
    if (tw > UINT_MAX) tw = UINT_MAX;
    if (th > UINT_MAX) th = UINT_MAX;

    *out_w = (unsigned)tw;
    *out_h = (unsigned)th;
}

