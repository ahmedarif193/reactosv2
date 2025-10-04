/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Machine Setup
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#include <arch/arch_interface.h>

#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);


/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* FUNCTIONS ******************************************************************/

static
VOID
UefiConfigureMachVtbl(VOID)
{
    TRACE("[UEFI] MachInit (generic) setting up MachVtbl...\n");
    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));

    MachVtbl.ConsPutChar = UefiConsPutChar;
    MachVtbl.ConsKbHit = UefiConsKbHit;
    MachVtbl.ConsGetCh = UefiConsGetCh;
    MachVtbl.VideoClearScreen = UefiVideoClearScreen;
    MachVtbl.VideoSetDisplayMode = UefiVideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize = UefiVideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize = UefiVideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware = UefiVideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition = UefiVideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor = UefiVideoHideShowTextCursor;
    MachVtbl.VideoPutChar = UefiVideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = UefiVideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed = UefiVideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor = UefiVideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor = UefiVideoGetPaletteColor;
    MachVtbl.VideoSync = UefiVideoSync;
    MachVtbl.Beep = UefiPcBeep;
    MachVtbl.PrepareForReactOS = UefiPrepareForReactOS;
    MachVtbl.GetMemoryMap = UefiMemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData = UefiGetExtendedBIOSData;
    MachVtbl.GetFloppyCount = UefiGetFloppyCount;
    MachVtbl.DiskReadLogicalSectors = UefiDiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry = UefiDiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = UefiDiskGetCacheableBlockCount;
    MachVtbl.GetTime = UefiGetTime;
    // AGENT-MODIFIED: Keep existing UefiInitializeBootDevices from uefidisk.c
    MachVtbl.InitializeBootDevices = UefiInitializeBootDevices;
    MachVtbl.HwDetect = UefiHwDetect;
    MachVtbl.HwIdle = UefiHwIdle;
    /* Setup GOP for non-ARM64 platforms */
    TRACE("[GOP] Calling UefiInitializeVideo() (non-ARM64)\n");
    if (UefiInitializeVideo() != EFI_SUCCESS)
    {
        ERR("Failed to setup GOP\n");
    }
    else
    {
        /* Keep screen reserved for UI/splash; debug goes to serial only. */
        /* DebugEnableScreenPort(); */
    }

    /* Reference the debug channel to avoid unused warnings in release builds */
    (void)DbgDefaultChannel;
    TRACE("[UEFI] MachInit (generic) complete\n");
}

VOID
MachInit(const char *CmdLine)
{
    UNREFERENCED_PARAMETER(CmdLine);
    UefiConfigureMachVtbl();

    /* Allow arch-specific initialization without #ifdefs */
    if (ArchInterface.ArchInit)
    {
        EFI_STATUS s = ArchInterface.ArchInit();
        TRACE("[UEFI] ArchInit status=%I64x\n", (unsigned long long)(UINTN)s);
    }
}
