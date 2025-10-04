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

PFREELDR_MEMORY_DESCRIPTOR
Amd64GetFreeldrMemoryMap(
    _Out_ PULONG Count);

BOOLEAN
Amd64EnumerateFirmwareMemory(
    _Out_ PFREELDR_MEMORY_DESCRIPTOR *Descriptors,
    _Out_ PULONG Count);

typedef struct _AMD64_EXCEPTION_REGISTERS
{
    ULONGLONG Rax;
    ULONGLONG Rbx;
    ULONGLONG Rcx;
    ULONGLONG Rdx;
    ULONGLONG Rsi;
    ULONGLONG Rdi;
    ULONGLONG Rbp;
    ULONGLONG R8;
    ULONGLONG R9;
    ULONGLONG R10;
    ULONGLONG R11;
    ULONGLONG R12;
    ULONGLONG R13;
    ULONGLONG R14;
    ULONGLONG R15;
} AMD64_EXCEPTION_REGISTERS, *PAMD64_EXCEPTION_REGISTERS;

typedef struct _AMD64_EXCEPTION_SNAPSHOT
{
    BOOLEAN Valid;
    ULONGLONG Vector;
    ULONGLONG ErrorCode;
    ULONGLONG Rip;
    ULONGLONG Cs;
    ULONGLONG Rflags;
    ULONGLONG Rsp;
    ULONGLONG Ss;
    AMD64_EXCEPTION_REGISTERS Regs;
    ULONGLONG Cr0;
    ULONGLONG Cr2;
    ULONGLONG Cr3;
    ULONGLONG Cr4;
    ULONGLONG Cr8;
    ULONGLONG GdtrBase;
    USHORT    GdtrLimit;
    ULONGLONG IdtrBase;
    USHORT    IdtrLimit;
} AMD64_EXCEPTION_SNAPSHOT, *PAMD64_EXCEPTION_SNAPSHOT;

const AMD64_EXCEPTION_SNAPSHOT*
Amd64GetLastExceptionSnapshot(VOID);

VOID
Amd64ClearExceptionSnapshot(VOID);
