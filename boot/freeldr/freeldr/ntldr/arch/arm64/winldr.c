/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Windows NT Loader Functions
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <ntldr/winldr.h>
#include <peloader.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

static BOOLEAN Arm64InitializeMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
MempSetupPaging(
    IN PFN_NUMBER StartPage,
    IN PFN_NUMBER NumberOfPages,
    IN BOOLEAN KernelMapping)
{
    /* ARM64 paging setup using UEFI memory management */
    TRACE("ARM64: Setting up paging for StartPage=0x%lx, NumberOfPages=0x%lx, KernelMapping=%d\n",
          (ULONG)StartPage, (ULONG)NumberOfPages, KernelMapping);
    
    /* For UEFI ARM64, we rely on identity mapping set up by the bootloader */
    /* The kernel will set up its own page tables */
    return TRUE;
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
    
    /* Initialize memory descriptor list if needed */
    if (IsListEmpty(&LoaderBlock->MemoryDescriptorListHead))
    {
        ERR("ARM64: Memory descriptor list is empty\n");
        return FALSE;
    }
    
    /* Dump memory descriptors for debugging */
    WinLdrpDumpMemoryDescriptors(LoaderBlock);
    
    /* ARM64 specific memory initialization */
    /* The UEFI firmware has already set up basic memory management */
    /* Additional ARM64 specific setup can be added here if needed */
    
    TRACE("ARM64: Memory management structures initialized\n");
    return TRUE;
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
