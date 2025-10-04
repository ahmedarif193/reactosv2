/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miscellaneous Function Stubs for UEFI-only bootloader
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* Architecture-specific functions */
#if !defined(_AMD64_) && !defined(_ARM64_)
VOID
_changestack(VOID)
{
    TRACE("_changestack() - stub\n");
    /* TODO: Implement platform-specific stack switching if needed */
}

VOID
_exituefi(VOID)
{
    TRACE("_exituefi() - stub\n");
    /* TODO: Implement UEFI exit for this architecture */
}
#endif

/* TUI printf function */
INT
TuiPrintf(const char* Format, ...)
{
    va_list ap;
    CHAR Buffer[512];
    INT Result;

    va_start(ap, Format);
    Result = RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    TRACE("TuiPrintf: %s", Buffer);
    /* TODO: Output via UEFI console */

    return Result;
}

ARC_STATUS
LoadReactOSSetup(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[])
{
    TRACE("LoadReactOSSetup(Argc=%lu)\n", Argc);
    /* TODO: Implement ReactOS setup booting for ARM64 UEFI */
    return ESUCCESS;
}
