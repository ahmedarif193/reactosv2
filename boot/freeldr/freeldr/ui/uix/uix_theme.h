/* Simple theme container */
#pragma once

#include <freeldr.h>

typedef struct _UIX_THEME
{
    ULONG ColorBackground;     /* ARGB */
    ULONG ColorBackgroundAlt;  /* ARGB */
    ULONG ColorAccent;         /* ARGB */
    ULONG ColorTitleFg;        /* ARGB */
    ULONG TitleBarHeight;      /* pixels */
} UIX_THEME;

static inline UIX_THEME UixTheme_Default(void)
{
    UIX_THEME t;
    t.ColorBackground    = 0xFF1E1E1E; /* dark gray */
    t.ColorBackgroundAlt = 0xFF2A2A2A; /* slightly lighter */
    t.ColorAccent        = 0xFF0078D4; /* Windows 10 blue */
    t.ColorTitleFg       = 0xFFFFFFFF; /* white */
    t.TitleBarHeight     = 36;
    return t;
}

