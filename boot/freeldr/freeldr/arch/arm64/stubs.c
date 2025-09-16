/*
 * Minimal ARM64 stubs to satisfy links in UEFI FreeLDR
 */

#include <freeldr.h>
#include <stdarg.h>

void __debugbreak(void)
{
    __asm__ volatile("brk #0");
}

void __fastfail(unsigned int code)
{
    (void)code;
    for (;;) { __asm__ volatile("wfi"); }
}

void DbgBreakPoint(void)
{
    __asm__ volatile("brk #0");
}

VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PSTR Format,
    ...)
{
    CHAR buf[256];
    va_list ap;
    va_start(ap, Format);
    RtlStringCbVPrintfA(buf, sizeof(buf), Format, ap);
    va_end(ap);
    UiMessageBoxCritical(buf);
    for (;;) { __asm__ volatile("wfi"); }
}

/* ARM64 FreeLDR uses identity mapping under UEFI */
PVOID VaToPa(PVOID Va) { return Va; }
PVOID PaToVa(PVOID Pa) { return Pa; }

double floor(double x) { return (double)((long long)x); }

VOID
RtlFillMemoryUlong(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length,
    _In_ ULONG Fill)
{
    ULONG *p = (ULONG*)Destination;
    SIZE_T n = Length / sizeof(ULONG);
    for (SIZE_T i = 0; i < n; ++i) p[i] = Fill;
}
