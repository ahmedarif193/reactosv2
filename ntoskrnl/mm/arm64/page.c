/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/arm64/page.c
 * PURPOSE:         ARM64 Page Management
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

const
ULONG_PTR
MmProtectToPteMask[32] =
{
    //
    // These are the base MM_ protection flags for ARM64
    // ARM64 uses different bit patterns than x86/AMD64
    //
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    //
    // These OR in the MM_NOCACHE flag
    //
    0,
    PTE_READONLY            | PTE_DISABLE_CACHE,
    PTE_EXECUTE             | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_DISABLE_CACHE,
    PTE_READWRITE           | PTE_DISABLE_CACHE,
    PTE_WRITECOPY           | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_DISABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_DISABLE_CACHE,
    //
    // These OR in the MM_DECOMMIT flag
    //
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    //
    // These OR in the MM_NOACCESS flag, which seems to be Windows 2000 only
    //
    0,
    PTE_READONLY            | PTE_DISABLE_CACHE,
    PTE_EXECUTE             | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_DISABLE_CACHE,
    PTE_READWRITE           | PTE_DISABLE_CACHE,
    PTE_WRITECOPY           | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_DISABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_DISABLE_CACHE,
};

const
ULONG MmProtectToValue[32] =
{
    PAGE_NOACCESS,
    PAGE_READONLY,
    PAGE_EXECUTE,
    PAGE_EXECUTE_READ,
    PAGE_READWRITE,
    PAGE_WRITECOPY,
    PAGE_EXECUTE_READWRITE,
    PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_NOCACHE | PAGE_READONLY,
    PAGE_NOCACHE | PAGE_EXECUTE,
    PAGE_NOCACHE | PAGE_EXECUTE_READ,
    PAGE_NOCACHE | PAGE_READWRITE,
    PAGE_NOCACHE | PAGE_WRITECOPY,
    PAGE_NOCACHE | PAGE_EXECUTE_READWRITE,
    PAGE_NOCACHE | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_READONLY,
    PAGE_EXECUTE,
    PAGE_EXECUTE_READ,
    PAGE_READWRITE,
    PAGE_WRITECOPY,
    PAGE_EXECUTE_READWRITE,
    PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_NOCACHE | PAGE_READONLY,
    PAGE_NOCACHE | PAGE_EXECUTE,
    PAGE_NOCACHE | PAGE_EXECUTE_READ,
    PAGE_NOCACHE | PAGE_READWRITE,
    PAGE_NOCACHE | PAGE_WRITECOPY,
    PAGE_NOCACHE | PAGE_EXECUTE_READWRITE,
    PAGE_NOCACHE | PAGE_EXECUTE_WRITECOPY
};

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MmIsPagePresent(PEPROCESS Process, PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

BOOLEAN
NTAPI
MmIsPageSwapEntry(PEPROCESS Process, PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

VOID
NTAPI
MmGetPageFileMapping(PEPROCESS Process, PVOID Address, SWAPENTRY* SwapEntry)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

BOOLEAN
NTAPI
MmIsDisabledPage(PEPROCESS Process, PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

BOOLEAN
NTAPI
MmCreateVirtualMappingForKernel(IN PVOID Address,
                                IN ULONG Protection,
                                IN PPFN_NUMBER Pages,
                                IN ULONG PageCount)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

NTSTATUS
NTAPI
MmCreateVirtualMappingUnsafe(IN PEPROCESS Process,
                             IN PVOID Address,
                             IN ULONG Protection,
                             IN PFN_NUMBER Page)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmCreateVirtualMapping(IN PEPROCESS Process,
                      IN PVOID Address,
                      IN ULONG Protection,
                      IN PFN_NUMBER Page)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmRawDeleteVirtualMapping(IN PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

BOOLEAN
NTAPI
MmDeleteVirtualMapping(IN PEPROCESS Process,
                      IN PVOID Address,
                      OUT PBOOLEAN WasDirty,
                      OUT PPFN_NUMBER Page)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

VOID
NTAPI
MmEnableVirtualMapping(IN PEPROCESS Process,
                      IN PVOID Address,
                      OUT PBOOLEAN WasDirty,
                      OUT PPFN_NUMBER Page)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

BOOLEAN
NTAPI
MmIsDirtyPage(IN PEPROCESS Process,
             IN PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return FALSE;
}

VOID
NTAPI
MmSetDirtyBit(IN PEPROCESS Process,
             IN PVOID Address,
             IN BOOLEAN Bit)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}


ULONG
NTAPI
MmGetPageProtect(IN PEPROCESS Process,
                IN PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return 0;
}

VOID
NTAPI
MmSetPageProtect(IN PEPROCESS Process,
                IN PVOID Address,
                IN ULONG Protection)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

PFN_NUMBER
NTAPI
MmGetPfnForProcess(IN PEPROCESS Process,
                  IN PVOID Address)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return 0;
}

VOID
NTAPI
MmDisableVirtualMapping(IN PEPROCESS Process,
                       IN PVOID Address,
                       OUT PBOOLEAN WasDirty,
                       OUT PPFN_NUMBER Page)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

VOID
NTAPI
MmDeletePageFileMapping(IN PEPROCESS Process,
                       IN PVOID Address,
                       OUT SWAPENTRY* SwapEntry)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
}

NTSTATUS
NTAPI
MmCreatePageFileMapping(IN PEPROCESS Process,
                       IN PVOID Address,
                       IN SWAPENTRY SwapEntry)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmSetExecuteOptions(IN ULONG ExecuteOptions)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmGetExecuteOptions(IN PULONG ExecuteOptions)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

ULONG
NTAPI
MmGetFaultCode(VOID)
{
    /* Not yet implemented for ARM64 */
    UNIMPLEMENTED;
    return 0;
}

/* PHYSICAL MAPPING FUNCTIONS ***********************************************/

/**
 * @brief Create a physical mapping on ARM64
 * @param Process Process context (NULL for kernel)
 * @param Address Virtual address to map
 * @param flProtect Protection flags
 * @param Page Physical page number to map
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmCreatePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG flProtect,
    _In_ PFN_NUMBER Page)
{
    /* For ARM64, physical mappings typically disable caching */
    ULONG AdjustedProtect = flProtect | PAGE_NOCACHE;

    /* Use the virtual mapping function with adjusted protection */
    return MmCreateVirtualMapping(Process, Address, AdjustedProtect, Page);
}

/**
 * @brief Delete a physical mapping on ARM64
 * @param Process Process context (NULL for kernel)
 * @param Address Virtual address to unmap
 * @param WasDirty Returns whether the page was dirty
 * @param Page Returns the physical page that was mapped
 * @return TRUE if successful, FALSE otherwise
 */
_Success_(return)
BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    /* For ARM64, physical and virtual mapping deletion is the same */
    return MmDeleteVirtualMapping(Process, Address, WasDirty, Page);
}