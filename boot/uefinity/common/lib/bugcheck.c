/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal bugcheck handling for UEFI-only build
 */

#include <freeldr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

VOID StallExecutionProcessor(ULONG Microseconds);

DECLSPEC_NORETURN
VOID
NTAPI
FrLdrBugCheck(ULONG BugCode)
{
    FrLdrBugCheckWithMessage(BugCode, __FILE__, __LINE__, "Fatal bootloader error %lu", BugCode);
}

DECLSPEC_NORETURN
VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list ap;

    va_start(ap, Format);
    RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    ERR("BUGCHECK %lu at %s:%lu -- %s\n", BugCode, File ? File : "<unknown>", Line, Buffer);

    UiMessageBoxCritical(Buffer);

    /* Halt the CPU */
    for (;;)
    {
        StallExecutionProcessor(1000000);
    }
}
