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

    GlobalImageHandle = ImageHandle;
    GlobalSystemTable = SystemTable;

    /* High-level boot tracing to help follow UEFI/ARM64/MM flow */
    if (GlobalSystemTable)
    {
        TRACE("[UEFI] EfiEntry: ImageHandle=%p, SystemTable=%p\n", ImageHandle, SystemTable);
        TRACE("[UEFI] FirmwareVendor='%S' FirmwareRevision=0x%lx\n",
              (SystemTable->FirmwareVendor ? SystemTable->FirmwareVendor : L"<null>"),
              (ULONG_PTR)SystemTable->FirmwareRevision);
        TRACE("[UEFI] BootServices=%p, RuntimeServices=%p\n",
              SystemTable->BootServices, SystemTable->RuntimeServices);
    }

    /* Load the default settings from the command-line */
    LoadSettings(CmdLine);

    /* Debugger pre-initialization */
    DebugInit(BootMgrInfo.DebugString);
    TRACE("[UEFI] DebugInit done (serial active=%lu)\n", (ULONG)1);

    /* ARM64 UEFI: Ensure debug output only goes to serial, not to screen */
    #if defined(_M_ARM64) && defined(UEFIBOOT)
    DebugDisableScreenPort();
    #endif

    TRACE("[ARM64/UEFI] Calling MachInit...\n");
    MachInit(CmdLine);
    TRACE("[ARM64/UEFI] MachInit returned\n");

    /* UI pre-initialization */
    TRACE("[UI] UiInitialize(FALSE) starting...\n");
    if (!UiInitialize(FALSE))
    {
        UiMessageBoxCritical("Unable to initialize UI.");
        goto Quit;
    }
    TRACE("[UI] UiInitialize ok\n");

    /* Initialize memory manager */
    TRACE("[MM] MmInitializeMemoryManager starting...\n");
    if (!MmInitializeMemoryManager())
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }
    TRACE("[MM] MmInitializeMemoryManager ok\n");

    /* Initialize I/O subsystem */
    TRACE("[FS] FsInit\n");
    FsInit();

    /* Initialize the module list */

    TRACE("[LDR] PeLdrInitializeModuleList starting...\n");
    if (!PeLdrInitializeModuleList())
    {
        UiMessageBoxCritical("Unable to initialize module list.");
        goto Quit;
    }
    TRACE("[LDR] PeLdrInitializeModuleList ok\n");


    TRACE("[HW] MachInitializeBootDevices starting...\n");
    if (!MachInitializeBootDevices())
    {
        UiMessageBoxCritical("Error when detecting hardware.");
        goto Quit;
    }
    TRACE("[HW] MachInitializeBootDevices ok\n");


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
        TRACE("[ARM64] AllocatePages for BasicStack failed: %lx\n", (ULONG_PTR)Status);
        goto Quit;
    }

    BasicStack = (PVOID)((ULONG_PTR)StackBase + 0x32000);
    TRACE("[ARM64] BasicStack allocated: Base=0x%lx Size=0x%lx\n", (ULONG_PTR)StackBase, (ULONG_PTR)0x32000);
#else
    BasicStack = (PVOID)((ULONG_PTR)0x32000 + (ULONG_PTR)MmAllocateMemoryWithType(0x32000, LoaderOsloaderStack));
#endif
    _changestack();

Quit:
    /* If we reach this point, something went wrong before, therefore reboot */
    TRACE("[UEFI] Reboot(): leaving EfiEntry due to earlier failure\n");
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
