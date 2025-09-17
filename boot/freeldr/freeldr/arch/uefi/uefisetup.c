/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Machine Setup
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#ifdef _ARM64_
#include <arch/arm64/arm64.h>
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);


/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* FUNCTIONS ******************************************************************/

VOID
MachInit(const char *CmdLine)
{
#ifdef _ARM64_
    /* For ARM64, use the ARM64-specific machine initialization */
    extern VOID Arm64MachInit(const char *CmdLine);
    TRACE("[ARM64] MachInit: forwarding to Arm64MachInit\n");
    Arm64MachInit(CmdLine);
    /* The ARM64MachInit will set up MachVtbl and initialize everything including GOP */
    /* Reference the debug channel to avoid unused warnings in release builds */
    (void)DbgDefaultChannel;
    return;
#else
    /* For non-ARM64 platforms, use the generic UEFI setup */
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

    /* Reference the debug channel to avoid unused warnings in release builds */
    (void)DbgDefaultChannel;
    TRACE("[UEFI] MachInit (generic) complete\n");
#endif
}
