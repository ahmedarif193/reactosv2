/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Entry point and helpers
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#include <debug.h>
#include <ui/pngui.h>

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
    /* Keep the .symlite section from being garbage-collected */
    extern const char gSymlitePlaceholder[];
    static volatile const void* gSymliteAnchor;
    gSymliteAnchor = (const void*)&gSymlitePlaceholder; /* force a relocation to keep .symlite */
    PCSTR CmdLine = ""; // FIXME: Determine a command-line from UEFI boot options

    GlobalImageHandle = ImageHandle;
    GlobalSystemTable = SystemTable;

    /* High-level boot tracing to help follow UEFI/ARM64/MM flow */
    if (GlobalSystemTable)
    {
    TRACE("[UEFI] EfiEntry: ImageHandle=%p, SystemTable=%p\n", ImageHandle, SystemTable);
    TRACE("[UEFI] FirmwareVendor='%S' FirmwareRevision=0x%lx\n",
              (SystemTable->FirmwareVendor ? SystemTable->FirmwareVendor : L"<null>"),
              (unsigned long)(SystemTable->FirmwareRevision));
    TRACE("[UEFI] BootServices=%p, RuntimeServices=%p\n",
              SystemTable->BootServices, SystemTable->RuntimeServices);

        /* GDB hint: print our actual load base so symbols can be relocated */
        do {
            EFI_STATUS __st;
            EFI_LOADED_IMAGE_PROTOCOL* __img = NULL;
            EFI_GUID __guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
            __st = SystemTable->BootServices->HandleProtocol(ImageHandle, &__guid, (VOID**)&__img);
            if (!EFI_ERROR(__st) && __img)
            {
                TRACE("[GDB] ImageBase=%p Size=0x%lx\n", __img->ImageBase, (ULONG_PTR)__img->ImageSize);
                TRACE("[GDB] Use: add-symbol-file boot/uefinity/uefinity.efi.debug 0x%p\n", __img->ImageBase);
            }
        } while (0);
    }

    /* Load the default settings from the command-line */
    LoadSettings(CmdLine);

    /* Debugger pre-initialization */
    DebugInit(BootMgrInfo.DebugString);
    TRACE("[UEFI] DebugInit done (serial active=%lu)\n", (ULONG)1);

    /* Now that debug output is reliably active, print GDB image base hint */
    {
        EFI_STATUS St;
        EFI_LOADED_IMAGE_PROTOCOL* Img = NULL;
        EFI_GUID Guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        St = GlobalSystemTable->BootServices->HandleProtocol(ImageHandle, &Guid, (VOID**)&Img);
        if (!EFI_ERROR(St) && Img)
        {
            TRACE("[GDB] ImageBase=%p Size=0x%lx\n", Img->ImageBase, (ULONG_PTR)Img->ImageSize);
            TRACE("[GDB] Use: add-symbol-file boot/uefinity/uefinity.efi.debug 0x%p\n", Img->ImageBase);
        }
    }

    /* Initialize runtime image list for better symbolization outside our image */
    UefiInitializeDebugImageInfo();

    /* Ensure debug output only goes to serial, not to screen */
    DebugDisableScreenPort();

    TRACE("[UEFI] Calling MachInit...\n");
    MachInit(CmdLine);
    TRACE("[UEFI] MachInit returned\n");

    /* UI pre-initialization */
    TRACE("[UI] UiInitialize(FALSE) starting...\n");
    if (!UiInitialize(FALSE))
    {
        UiMessageBoxCritical("Unable to initialize UI.");
        goto Quit;
    }
    TRACE("[UI] UiInitialize ok\n");

    /* List assets, show splash and halt waiting for key events */
    UiSplashListAndWait();
    /* Not reached */

    /* Initialize memory manager */
    TRACE("[MM] MmInitializeMemoryManager starting...\n");
    /* Avoid using UEFI console/serial during memory map and heap bootstrap */
    DebugSetEarlyQuiet(TRUE);
    if (!MmInitializeMemoryManager())
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }
    DebugSetEarlyQuiet(FALSE);
    DebugFlushEarlyLog();
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
    /* UEFI allocation directly since MM may not be fully initialized */
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS StackBase = 0;
    UINTN StackPages = (0x32000 + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    PVOID AllocatedStackBase = NULL;

    Status = GlobalSystemTable->BootServices->AllocatePages(AllocateAnyPages,
                                                            EfiLoaderData,
                                                            StackPages,
                                                            &StackBase);
    if (EFI_ERROR(Status)) {
        TRACE("[STACK] AllocatePages for BasicStack failed: %I64x\n",
              (unsigned long long)(UINTN)Status);
        goto Quit;
    }

    AllocatedStackBase = (PVOID)(ULONG_PTR)StackBase;
    BasicStack = (PVOID)((ULONG_PTR)AllocatedStackBase + 0x32000);
    TRACE("[STACK] BasicStack allocated: Base=0x%I64x Size=0x%I64x\n",
          (unsigned long long)(UINTN)StackBase,
          (unsigned long long)(UINTN)0x32000);
    TRACE("[STACK] Switching to new stack at %p (base %p)\n", BasicStack, AllocatedStackBase);
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
