/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ex/efi.c
 * PURPOSE:         I/O Functions for EFI Machines
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
NtAddBootEntry(IN PBOOT_ENTRY Entry,
               IN ULONG Id)
{
    UNREFERENCED_PARAMETER(Entry);
    UNREFERENCED_PARAMETER(Id);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtAddDriverEntry(IN PEFI_DRIVER_ENTRY Entry,
                 IN ULONG Id)
{
    UNREFERENCED_PARAMETER(Entry);
    UNREFERENCED_PARAMETER(Id);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtDeleteBootEntry(IN ULONG Id)
{
    UNREFERENCED_PARAMETER(Id);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtDeleteDriverEntry(IN ULONG Id)
{
    UNREFERENCED_PARAMETER(Id);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtEnumerateBootEntries(IN PVOID Buffer,
                       IN PULONG BufferLength)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtEnumerateDriverEntries(IN PVOID Buffer,
                        IN PULONG BufferLength)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtModifyBootEntry(IN PBOOT_ENTRY BootEntry)
{
    UNREFERENCED_PARAMETER(BootEntry);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtModifyDriverEntry(IN PEFI_DRIVER_ENTRY DriverEntry)
{
    UNREFERENCED_PARAMETER(DriverEntry);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryBootEntryOrder(IN PULONG Ids,
                      IN PULONG Count)
{
    UNREFERENCED_PARAMETER(Ids);
    UNREFERENCED_PARAMETER(Count);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryDriverEntryOrder(IN PULONG Ids,
                        IN PULONG Count)
{
    UNREFERENCED_PARAMETER(Ids);
    UNREFERENCED_PARAMETER(Count);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryBootOptions(IN PBOOT_OPTIONS BootOptions,
                   IN PULONG BootOptionsLength)
{
    UNREFERENCED_PARAMETER(BootOptions);
    UNREFERENCED_PARAMETER(BootOptionsLength);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetBootEntryOrder(IN PULONG Ids,
                    IN PULONG Count)
{
    UNREFERENCED_PARAMETER(Ids);
    UNREFERENCED_PARAMETER(Count);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetDriverEntryOrder(IN PULONG Ids,
                      IN PULONG Count)
{
    UNREFERENCED_PARAMETER(Ids);
    UNREFERENCED_PARAMETER(Count);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetBootOptions(IN PBOOT_OPTIONS BootOptions,
                 IN ULONG FieldsToChange)
{
    UNREFERENCED_PARAMETER(BootOptions);
    UNREFERENCED_PARAMETER(FieldsToChange);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtTranslateFilePath(PFILE_PATH InputFilePath,
                    ULONG OutputType,
                    PFILE_PATH OutputFilePath,
                    ULONG OutputFilePathLength)
{
    UNREFERENCED_PARAMETER(InputFilePath);
    UNREFERENCED_PARAMETER(OutputType);
    UNREFERENCED_PARAMETER(OutputFilePath);
    UNREFERENCED_PARAMETER(OutputFilePathLength);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
