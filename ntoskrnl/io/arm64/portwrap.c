/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Port I/O Wrappers for HAL imports
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * These wrapper functions provide the __imp_ symbols that kdps2kbd.c expects.
 * They forward to the HAL implementations.
 */

/* Forward declarations from HAL */
NTHALAPI UCHAR NTAPI READ_PORT_UCHAR(IN PUCHAR Port);
NTHALAPI VOID NTAPI WRITE_PORT_UCHAR(IN PUCHAR Port, IN UCHAR Value);

/* Export the __imp_ symbols */
#ifdef _MSC_VER
#pragma comment(linker, "/EXPORT:__imp_READ_PORT_UCHAR=READ_PORT_UCHAR")
#pragma comment(linker, "/EXPORT:__imp_WRITE_PORT_UCHAR=WRITE_PORT_UCHAR")
#else
/* For GCC, we create the import symbols manually */
UCHAR (*__imp_READ_PORT_UCHAR)(IN PUCHAR Port) = READ_PORT_UCHAR;
VOID (*__imp_WRITE_PORT_UCHAR)(IN PUCHAR Port, IN UCHAR Value) = WRITE_PORT_UCHAR;
#endif