/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Early Boot Debug Printing Support Header
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#ifndef _ARM64_EARLYDBG_H
#define _ARM64_EARLYDBG_H

#include <stdarg.h>

/* FUNCTION DECLARATIONS *****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Early boot debug print with variable arguments
 *
 * Use this function for formatted debug output during early kernel boot
 * before the full debug infrastructure is available.
 *
 * Example usage:
 *   KiEarlyKernelDebugPrint("Starting kernel at %p\n", KernelBase);
 *   KiEarlyKernelDebugPrint("Memory: %llu MB\n", MemorySize / (1024*1024));
 *
 * Note: Renamed from KiEarlyDebugPrint to avoid conflict with boot.S
 *
 * @param Format Printf-style format string
 * @param ... Variable arguments
 */
VOID
NTAPI
KiEarlyKernelDebugPrint(
    IN const CHAR *Format,
    IN ...);

/**
 * @brief Early boot debug print with va_list
 *
 * Use this when you already have a va_list from another varargs function.
 *
 * @param Format Printf-style format string
 * @param Args Variable argument list
 */
VOID
NTAPI
KiEarlyDebugPrintV(
    IN const CHAR *Format,
    IN va_list Args);

/**
 * @brief Write a simple string to early debug output
 *
 * Use this for plain string output without formatting.
 *
 * Example:
 *   KiEarlyDebugString("Kernel initialization starting\n");
 *
 * @param String String to output
 */
VOID
NTAPI
KiEarlyDebugString(
    IN const CHAR *String);

/**
 * @brief Write a hex value to early debug output
 *
 * Convenience function for dumping hex values with labels.
 *
 * Example:
 *   KiEarlyDebugHex("PCR Address", (ULONG64)Pcr);
 *   KiEarlyDebugHex("Exception Vector", ExceptionCode);
 *
 * @param Label Descriptive label (can be NULL)
 * @param Value Value to display in hexadecimal
 */
VOID
NTAPI
KiEarlyDebugHex(
    IN const CHAR *Label,
    IN ULONG64 Value);

#ifdef __cplusplus
}
#endif

/* CONVENIENCE MACROS ********************************************************/

/**
 * Macro for conditional early debug output
 * Can be compiled out in release builds
 */
#ifdef DBG
#define EARLY_DEBUG_PRINT(fmt, ...) KiEarlyKernelDebugPrint(fmt, ##__VA_ARGS__)
#define EARLY_DEBUG_STRING(str) KiEarlyDebugString(str)
#define EARLY_DEBUG_HEX(label, value) KiEarlyDebugHex(label, value)
#else
#define EARLY_DEBUG_PRINT(fmt, ...) ((void)0)
#define EARLY_DEBUG_STRING(str) ((void)0)
#define EARLY_DEBUG_HEX(label, value) ((void)0)
#endif

#endif /* _ARM64_EARLYDBG_H */

/* EOF */