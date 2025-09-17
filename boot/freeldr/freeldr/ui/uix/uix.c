#include "uix.h"
#include "uix_surface.h"
#include "uix_draw.h"
#include "uix_theme.h"

VOID UixInitialize(VOID)
{
    /* Placeholder for future state */
}

BOOLEAN UixGetScreenSize(UIX_SIZE* OutSize)
{
    if (!OutSize) return FALSE;
    UIX_SURFACE s;
    if (!UixGetPrimarySurface(&s))
    {
        OutSize->Width = OutSize->Height = 0;
        return FALSE;
    }
    OutSize->Width = s.Width;
    OutSize->Height = s.Height;
    return TRUE;
}

VOID UixDemoDrawWelcome(VOID)
{
    UIX_SURFACE s;
    if (!UixGetPrimarySurface(&s))
        return; /* No pixel backend */

    UIX_THEME t = UixTheme_Default();

    /* Background gradient */
    (void)UixFillVGradient(&s, 0, 0, (LONG)s.Width, (LONG)s.Height,
                           t.ColorBackground, t.ColorBackgroundAlt);

    /* Accent top bar */
    (void)UixFillRect(&s, 0, 0, (LONG)s.Width, (LONG)t.TitleBarHeight, t.ColorAccent);
}

