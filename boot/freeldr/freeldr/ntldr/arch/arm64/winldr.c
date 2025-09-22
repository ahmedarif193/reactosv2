/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Windows NT Loader Functions
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <ntldr/winldr.h>
#include <peloader.h>
#include <arch/arm64/arm64.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

static BOOLEAN Arm64InitializeMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion);

BOOLEAN
MempSetupPaging(
    IN PFN_NUMBER StartPage,
    IN PFN_NUMBER NumberOfPages,
    IN BOOLEAN KernelMapping)
{
    ULONGLONG phys_start;
    ULONGLONG phys_end;
    ULONGLONG map_start;
    ULONGLONG map_end;
    ULONGLONG current;
    BOOLEAN Status = TRUE;
    const ULONGLONG block_size = ARM64_BLOCK_SIZE_1G;
    const ULONGLONG block_mask = ARM64_BLOCK_MASK_1G;
    const ULONG attrs = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_EXECUTE;

    TRACE("ARM64: Setting up paging for StartPage=0x%lx, NumberOfPages=0x%lx, KernelMapping=%d\n",
          (ULONG)StartPage, (ULONG)NumberOfPages, KernelMapping);

    if (NumberOfPages == 0)
        return TRUE;

    phys_start = ((ULONGLONG)StartPage) << MM_PAGE_SHIFT;
    phys_end = phys_start + (((ULONGLONG)NumberOfPages) << MM_PAGE_SHIFT);

    /* Expand to 1GB boundaries because current mapper works at that granularity */
    map_start = phys_start & ~block_mask;
    map_end = (phys_end + block_mask) & ~block_mask;

    TRACE("ARM64: Paging span phys_start=0x%llx phys_end=0x%llx map_start=0x%llx map_end=0x%llx\n",
          phys_start, phys_end, map_start, map_end);

    for (current = map_start; current < map_end; current += block_size)
    {
        TRACE("ARM64:   TTBR0 map block @PA 0x%llx -> Size 0x%llx\n", current, block_size);

        if (!Arm64MapVirtualMemory(current, current, block_size, attrs))
        {
            ERR("ARM64: Failed to identity map PA 0x%llx\n", current);
            Status = FALSE;
            break;
        }

        TRACE("ARM64:   TTBR0 map success for 0x%llx\n", current);

        if (KernelMapping)
        {
            ULONGLONG kernel_va = ARM64_KSEG0_BASE + current;
            TRACE("ARM64:   TTBR1 map block @VA 0x%llx -> PA 0x%llx\n", kernel_va, current);
            if (!Arm64MapVirtualMemory(kernel_va, current, block_size, attrs))
            {
                ERR("ARM64: Failed to map kernel VA 0x%llx -> PA 0x%llx\n", kernel_va, current);
                Status = FALSE;
                break;
            }

            TRACE("ARM64:   TTBR1 map success for VA 0x%llx\n", kernel_va);
        }
    }

    TRACE("ARM64: Paging setup result=%d for StartPage=0x%lx\n", Status, (ULONG)StartPage);

    return Status;
}

VOID
MempUnmapPage(
    PFN_NUMBER Page)
{
    /* ARM64 page unmapping */
    TRACE("ARM64: Unmapping page 0x%lx\n", (ULONG)Page);
    
    /* For UEFI ARM64, page unmapping is handled by UEFI/MMU */
    /* Individual page unmapping is not typically needed in the bootloader */
}

VOID
MempDump(VOID)
{
    /* ARM64 memory dump for debugging */
    TRACE("ARM64: Memory dump requested\n");
    
    /* This would dump memory allocation information for debugging */
    /* Implementation can be added when needed for debugging purposes */
}

BOOLEAN
Arm64SetupForNt(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PVOID *GdtIdt,
    IN ULONG *PcrBasePage,
    IN ULONG *TssBasePage)
{
    TRACE("ARM64: Setting up for NT kernel\n");
    
    /* ARM64 doesn't use GDT/IDT or TSS like x86 */
    *GdtIdt = NULL;
    *PcrBasePage = 0;
    *TssBasePage = 0;
    
    /* Setup ARM64 specific structures for NT */
    /* Initialize ARM64 exception vectors if needed */
    /* The kernel will set up its own exception handling */
    
    /* Ensure memory is properly prepared */
    if (!Arm64InitializeMemory(LoaderBlock))
    {
        ERR("ARM64: Failed to initialize memory for NT\n");
        return FALSE;
    }
    
    TRACE("ARM64: Successfully set up for NT kernel\n");
    return TRUE;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    Arm64ConfigureProcessorContext(OperatingSystemVersion);
}

/* Provide the machine-dependent setup entry used by the generic NT loader path */
VOID
WinLdrSetupMachineDependent(
    PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID GdtIdt = NULL;
    ULONG PcrBasePage = 0;
    ULONG TssBasePage = 0;

    /* Delegate to the ARM64-specific setup; GDT/IDT/TSS are not used on AArch64 */
    (void)Arm64SetupForNt(LoaderBlock, &GdtIdt, &PcrBasePage, &TssBasePage);
}

/* ARM64 specific memory setup */
BOOLEAN
Arm64InitializeMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    TRACE("ARM64: Initializing memory management structures\n");
    
    /* Validate loader parameter block */
    if (!LoaderBlock)
    {
        ERR("ARM64: Invalid LoaderBlock\n");
        return FALSE;
    }
    
    /* Note: Memory descriptor list will be populated later by WinLdrSetupMemoryLayout() */
    /* For now, just ensure the LoaderBlock is valid - the list may still be empty at this point */

    /* Memory descriptors will be dumped later after WinLdrSetupMemoryLayout() */
    
    /* ARM64 specific memory initialization */
    /* The UEFI firmware has already set up basic memory management */
    /* Additional ARM64 specific setup can be added here if needed */
    
    TRACE("ARM64: Memory management structures initialized\n");
    return TRUE;
}

static VOID
Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    TRACE("ARM64: WinLdrSetProcessorContext\n");

    /*
     * UEFI firmware already leaves the CPU in EL1 with MMU enabled and the
     * kernel is expected to reset the environment. We currently rely on the
     * identity mapping established by the firmware/boot manager, so there is
     * nothing mandatory to program here yet.
     *
     * Hook for future enhancements: switch stacks, adjust translation base,
     * or clean caches before transferring control to the kernel.
     */
    /* Nothing to do yet. */
}

/* WinLdrpDumpMemoryDescriptors and WinLdrLoadModule are defined in the main winldr.c */

/* Other architecture-specific functions can be added here as needed */

BOOLEAN
WinLdrCheckForLoadedDll(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PCH DllName,
    OUT PLDR_DATA_TABLE_ENTRY *LoadedEntry)
{
    /* Delegate to the generic PE loader helper which performs
       proper case-insensitive comparison against UNICODE names. */
    return PeLdrCheckForLoadedDll(&LoaderBlock->LoadOrderListHead,
                                  DllName,
                                  LoadedEntry);
}
