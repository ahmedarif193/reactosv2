/* Pixel surface abstraction */
#pragma once

#include <freeldr.h>

typedef enum _UIX_PIXEL_FORMAT
{
    UIX_PF_UNKNOWN = 0,
    UIX_PF_X8R8G8B8,  /* 32bpp, little endian: B,G,R,A? depends on firmware */
} UIX_PIXEL_FORMAT;

typedef struct _UIX_SURFACE
{
    PUCHAR           Pixels;   /* Base pointer */
    ULONG            Width;    /* in pixels */
    ULONG            Height;   /* in pixels */
    ULONG            Stride;   /* bytes per scanline */
    UIX_PIXEL_FORMAT Format;
} UIX_SURFACE;

/* Returns TRUE if a linear framebuffer is available and fills OutSurf. */
BOOLEAN UixGetPrimarySurface(UIX_SURFACE* OutSurf);

