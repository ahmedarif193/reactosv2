/*
 * Experimental modern UI helpers for FreeLDR
 */
#pragma once

#include <freeldr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _UIX_SIZE
{
    ULONG Width;
    ULONG Height;
} UIX_SIZE;

/* Initialize any per‑session state; currently a no‑op. */
VOID UixInitialize(VOID);

/* Try to obtain the primary drawing surface size (if any). */
BOOLEAN UixGetScreenSize(UIX_SIZE* OutSize);

/* Simple demo: paint a modern background and a title bar. */
VOID UixDemoDrawWelcome(VOID);

#ifdef __cplusplus
}
#endif

