/* Basic drawing helpers */
#pragma once

#include "uix_surface.h"

/* Convert ARGB (0xAARRGGBB) to packed 0x00BBGGRR used by X8R8G8B8 */
static inline ULONG UixPackX8R8G8B8(ULONG argb)
{
    ULONG r = (argb >> 16) & 0xFF;
    ULONG g = (argb >> 8)  & 0xFF;
    ULONG b = (argb >> 0)  & 0xFF;
    return (b) | (g << 8) | (r << 16) | 0xFF000000;
}

BOOLEAN UixFillRect(const UIX_SURFACE* S, LONG x, LONG y, LONG w, LONG h, ULONG argb);
BOOLEAN UixFillVGradient(const UIX_SURFACE* S, LONG x, LONG y, LONG w, LONG h,
                         ULONG topARGB, ULONG bottomARGB);

