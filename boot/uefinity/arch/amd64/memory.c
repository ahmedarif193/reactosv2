/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 helpers for converting the UEFI memory map into the
 *              classic FreeLDR descriptor format expected by existing
 *              loader code.
 */

#include <freeldr.h>
#include <uefildr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(MEMORY);

static PFREELDR_MEMORY_DESCRIPTOR CachedDescriptors;
static ULONG CachedDescriptorCount;

PFREELDR_MEMORY_DESCRIPTOR
Amd64GetFreeldrMemoryMap(
    _Out_ PULONG Count)
{
    ULONG LocalCount;
    PFREELDR_MEMORY_DESCRIPTOR Map;

    if (CachedDescriptors != NULL)
    {
        if (Count) *Count = CachedDescriptorCount;
        return CachedDescriptors;
    }

    Map = UefiMemGetMemoryMap(&LocalCount);
    if (!Map)
    {
        ERR("UefiMemGetMemoryMap() failed\n");
        return NULL;
    }

    CachedDescriptors = Map;
    CachedDescriptorCount = LocalCount;

    if (Count) *Count = LocalCount;
    return Map;
}
