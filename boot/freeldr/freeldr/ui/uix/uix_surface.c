/* Surface backend: UEFI GOP (if available), otherwise stub */

#include "uix_surface.h"

#if defined(UEFIBOOT)
#include <arch/uefi/uefildr.h> /* REACTOS_INTERNAL_BGCONTEXT */
extern REACTOS_INTERNAL_BGCONTEXT framebufferData;
#endif

BOOLEAN UixGetPrimarySurface(UIX_SURFACE* OutSurf)
{
    if (!OutSurf)
        return FALSE;

#if defined(UEFIBOOT)
    if (framebufferData.BaseAddress != 0 &&
        framebufferData.ScreenWidth  != 0 &&
        framebufferData.ScreenHeight != 0)
    {
        OutSurf->Pixels = (PUCHAR)(ULONG_PTR)framebufferData.BaseAddress;
        OutSurf->Width  = framebufferData.ScreenWidth;
        OutSurf->Height = framebufferData.ScreenHeight;
        /* Stride is PixelsPerScanLine * 4 bytes */
        OutSurf->Stride = framebufferData.PixelsPerScanLine * 4;
        OutSurf->Format = UIX_PF_X8R8G8B8;
        return TRUE;
    }
#endif

    RtlZeroMemory(OutSurf, sizeof(*OutSurf));
    OutSurf->Format = UIX_PF_UNKNOWN;
    return FALSE;
}

