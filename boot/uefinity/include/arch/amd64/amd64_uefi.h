/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 UEFI-specific definitions
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <Arif193@gmail.com>
 */

#pragma once

/* UEFI AMD64 has no BIOS legacy support, minimal definitions */

/* BIOS Memory Map structures needed for compatibility */
typedef struct _BIOS_MEMORY_MAP
{
    ULONGLONG BaseAddress;
    ULONGLONG Length;
    ULONG Type;
    ULONG Reserved;
} BIOS_MEMORY_MAP, *PBIOS_MEMORY_MAP;

/* ACPI BIOS Data structure */
typedef struct _ACPI_BIOS_DATA
{
    LARGE_INTEGER RSDTAddress;
    ULONG Count;
    BIOS_MEMORY_MAP MemoryMap[1];
} ACPI_BIOS_DATA, *PACPI_BIOS_DATA;

/* Tag definitions */
#define TAG_HW_RESOURCE_LIST 'lRwH'

/* No BIOS interrupts in UEFI mode */
#define NO_BIOS_INTERRUPTS 1