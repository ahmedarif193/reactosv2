/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64-specific machine initialization helpers
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <uefildr.h>
#include <machuefi.h>
#include <arch/amd64.h>
#include <arch/arch_interface.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* Forward declarations from the AMD64 trap support */
extern VOID Amd64InitializeExceptions(VOID);

VOID
Amd64MachInit(const char *CmdLine)
{
    UNREFERENCED_PARAMETER(CmdLine);

    TRACE("[AMD64] Amd64MachInit begin\n");

    /* Bring up the architecture service table (exceptions, CPUID, timers...) */
    EFI_STATUS Status = ArchInitialize();
    if (EFI_ERROR(Status))
    {
        ERR("ArchInitialize failed: %I64x\n",
            (unsigned long long)(UINTN)Status);
    }

    if (ArchInterface.ArchDetectCPU)
    {
        ARCH_CPU_INFO CpuInfo;
        if (EFI_ERROR(ArchInterface.ArchDetectCPU(&CpuInfo)))
        {
            WARN("CPU detection not available\n");
        }
        else
        {
            TRACE("[AMD64] CPU vendor %s, type %lu, family/model 0x%lx\n",
                  CpuInfo.VendorString,
                  CpuInfo.ProcessorType,
                  (ULONG)CpuInfo.ProcessorRevision);
        }
    }

    TRACE("[AMD64] Amd64MachInit complete\n");
}
