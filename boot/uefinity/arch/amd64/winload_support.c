/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Convenience helpers for wiring the classic winload path on
 *              AMD64. These wrappers allow the new UEFI-centric code to
 *              reuse the already-ported FreeLDR winload routines without
 *              exposing UEFI-specific details everywhere.
 */

#include <freeldr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WINDOWS);

extern PFREELDR_MEMORY_DESCRIPTOR Amd64GetFreeldrMemoryMap(PULONG Count);

BOOLEAN
Amd64EnumerateFirmwareMemory(
    _Out_ PFREELDR_MEMORY_DESCRIPTOR *Descriptors,
    _Out_ PULONG Count)
{
    PFREELDR_MEMORY_DESCRIPTOR Map;

    Map = Amd64GetFreeldrMemoryMap(Count);
    if (!Map)
    {
        ERR("Amd64GetFreeldrMemoryMap() failed\n");
        return FALSE;
    }

    if (Descriptors)
    {
        *Descriptors = Map;
    }

    return TRUE;
}
