/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Entry point and helpers
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS ********************************************************************/

EFI_HANDLE GlobalImageHandle;
EFI_SYSTEM_TABLE *GlobalSystemTable;
PVOID UefiServiceStack;
PVOID BasicStack;

void _changestack(VOID);

/* FUNCTIONS ******************************************************************/

EFI_STATUS
EfiEntry(
    _In_ EFI_HANDLE ImageHandle,
    _In_ EFI_SYSTEM_TABLE *SystemTable)
{
    PCSTR CmdLine = ""; // FIXME: Determine a command-line from UEFI boot options

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI EntryPoint: Starting freeldr from UEFI\r\n");
    GlobalImageHandle = ImageHandle;
    GlobalSystemTable = SystemTable;

    /* Load the default settings from the command-line */
    LoadSettings(CmdLine);

    /* Debugger pre-initialization */
    DebugInit(BootMgrInfo.DebugString);

    /* Progress marker */
    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: Debug initialized\r\n");

    MachInit(CmdLine);

    if (SystemTable && SystemTable->ConOut)
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: Mach initialized (GOP setup attempted)\r\n");
        /* Debug: Check if MachVtbl.GetMemoryMap is set */
        if (!MachVtbl.GetMemoryMap)
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"ERROR: MachVtbl.GetMemoryMap is NULL after MachInit!\r\n");
        else
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: MachVtbl.GetMemoryMap is set\r\n");
    }

    /* UI pre-initialization */
    if (!UiInitialize(FALSE))
    {
        UiMessageBoxCritical("Unable to initialize UI.");
        goto Quit;
    }

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: UI initialized\r\n");

    /* Initialize memory manager */
    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: MM init start\r\n");

    BOOLEAN mmResult = FALSE;
    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: About to call MmInitializeMemoryManager\r\n");

    mmResult = MmInitializeMemoryManager();

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: MmInitializeMemoryManager returned\r\n");

    if (!mmResult)
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: MM initialized\r\n");

    /* Initialize I/O subsystem */
    FsInit();

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: FS initialized\r\n");

    /* Initialize the module list */
    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: About to call PeLdrInitializeModuleList\r\n");

    if (!PeLdrInitializeModuleList())
    {
        UiMessageBoxCritical("Unable to initialize module list.");
        goto Quit;
    }

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: PE loader initialized\r\n");

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: About to call MachInitializeBootDevices\r\n");

    if (!MachInitializeBootDevices())
    {
        UiMessageBoxCritical("Error when detecting hardware.");
        goto Quit;
    }

    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: Boot devices initialized\r\n");

    /* 0x32000 is what UEFI defines, but we can go smaller if we want */
#ifdef _ARM64_
    /* ARM64: Use UEFI allocation directly since MM may not be fully initialized */
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS StackBase = 0;
    UINTN StackPages = (0x32000 + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

    Status = GlobalSystemTable->BootServices->AllocatePages(AllocateAnyPages,
                                                            EfiLoaderData,
                                                            StackPages,
                                                            &StackBase);
    if (EFI_ERROR(Status)) {
        if (SystemTable && SystemTable->ConOut)
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: Failed to allocate stack\r\n");
        goto Quit;
    }

    BasicStack = (PVOID)((ULONG_PTR)StackBase + 0x32000);
#else
    BasicStack = (PVOID)((ULONG_PTR)0x32000 + (ULONG_PTR)MmAllocateMemoryWithType(0x32000, LoaderOsloaderStack));
#endif
    if (SystemTable && SystemTable->ConOut)
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"UEFI: Switching to loader stack\r\n");
    _changestack();

Quit:
    /* If we reach this point, something went wrong before, therefore reboot */
    Reboot();

    UNREACHABLE;
    return 0;
}

void
ExecuteLoaderCleanly(PVOID PreviousStack)
{
    TRACE("ExecuteLoaderCleanly Entry\n");
    UefiServiceStack = PreviousStack;

    RunLoader();
    UNREACHABLE;
}

#ifndef _M_ARM
DECLSPEC_NORETURN
VOID __cdecl Reboot(VOID)
{
    //TODO: Replace with a true firmware reboot eventually
    WARN("Something has gone wrong - halting FreeLoader\n");
    for (;;)
    {
        NOTHING;
    }
}
#endif
