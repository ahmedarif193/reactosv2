/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Loader.
 * COPYRIGHT:   Copyright 2006-2019 Aleksey Bragin <aleksey@reactos.org>
 */

#pragma once

#include <arc/setupblk.h>

// See freeldr/ntldr/winldr.h
#define TAG_WLDR_DTE 'eDlW'
#define TAG_WLDR_BDE 'dBlW'
#define TAG_WLDR_NAME 'mNlW'

// Some definitions

#include <pshpack1.h>
typedef struct  /* Root System Descriptor Pointer */
{
    CHAR             signature [8];          /* contains "RSD PTR " */
    UCHAR            checksum;               /* to make sum of struct == 0 */
    CHAR             oem_id [6];             /* OEM identification */
    UCHAR            revision;               /* Must be 0 for 1.0, 2 for 2.0 */
    ULONG            rsdt_physical_address;  /* 32-bit physical address of RSDT */
    ULONG            length;                 /* XSDT Length in bytes including hdr */
    ULONGLONG        xsdt_physical_address;  /* 64-bit physical address of XSDT */
    UCHAR            extended_checksum;      /* Checksum of entire table */
    CHAR             reserved [3];           /* reserved field must be 0 */
} RSDP_DESCRIPTOR, *PRSDP_DESCRIPTOR;
#include <poppack.h>


typedef struct _ARC_DISK_SIGNATURE_EX
{
    ARC_DISK_SIGNATURE DiskSignature;
    CHAR ArcName[MAX_PATH];
} ARC_DISK_SIGNATURE_EX, *PARC_DISK_SIGNATURE_EX;

#define MAX_OPTIONS_LENGTH 255

typedef struct _LOADER_SYSTEM_BLOCK
{
    LOADER_PARAMETER_BLOCK LoaderBlock;
    LOADER_PARAMETER_EXTENSION Extension;
    SETUP_LOADER_BLOCK SetupBlock;
#ifdef _M_IX86
    HEADLESS_LOADER_BLOCK HeadlessLoaderBlock;
#endif
    NLS_DATA_BLOCK NlsDataBlock;
    CHAR LoadOptions[MAX_OPTIONS_LENGTH+1];
    CHAR ArcBootDeviceName[MAX_PATH+1];
    // CHAR ArcHalDeviceName[MAX_PATH];
    CHAR NtBootPathName[MAX_PATH+1];
    CHAR NtHalPathName[MAX_PATH+1];
    ARC_DISK_INFORMATION ArcDiskInformation;
    LOADER_PERFORMANCE_DATA LoaderPerformanceData;
} LOADER_SYSTEM_BLOCK, *PLOADER_SYSTEM_BLOCK;

extern PLOADER_SYSTEM_BLOCK WinLdrSystemBlock;
extern PCWSTR BootFileSystem;

////////////////////////////////////////////////////////////////////////////////
//
// ReactOS Loading Functions
//
////////////////////////////////////////////////////////////////////////////////

ARC_STATUS
LoadAndBootWindows(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]);

ARC_STATUS
LoadReactOSSetup(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]);

VOID
NtLdrOutputLoadMsg(
    _In_ PCSTR FileName,
    _In_opt_ PCSTR Description);

PVOID WinLdrLoadModule(PCSTR ModuleName, PULONG Size,
                       TYPE_OF_MEMORY MemoryType);

// wlmemory.c
BOOLEAN
WinLdrSetupMemoryLayout(IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock);

// wlregistry.c
BOOLEAN
WinLdrInitSystemHive(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PCSTR SystemRoot,
    IN BOOLEAN Setup);

BOOLEAN WinLdrScanSystemHive(IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
                             IN PCSTR SystemRoot);

BOOLEAN
WinLdrLoadNLSData(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCSTR DirectoryPath,
    _In_ PCUNICODE_STRING AnsiFileName,
    _In_ PCUNICODE_STRING OemFileName,
    _In_ PCUNICODE_STRING LangFileName, // CaseTable
    _In_ PCUNICODE_STRING OemHalFileName);

// Debug/utility functions
VOID
WinLdrpDumpMemoryDescriptors(PLOADER_PARAMETER_BLOCK LoaderBlock);

// arch/xxx/winldr.c
BOOLEAN
MempSetupPaging(IN PFN_NUMBER StartPage,
                IN PFN_NUMBER NumberOfPages,
                IN BOOLEAN KernelMapping);

VOID
MempUnmapPage(PFN_NUMBER Page);

VOID
MempDump(VOID);

// conversion.c and conversion.h
PVOID VaToPa(PVOID Va);
PVOID PaToVa(PVOID Pa);
VOID List_PaToVa(_In_ LIST_ENTRY *ListEntry);
