/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Translation Lookaside Buffer (TLB) Management
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 TLB Invalidation Operations */
#define ARM64_TLBI_ALLE1IS      "tlbi alle1is"      /* All EL1 entries, Inner Shareable */
#define ARM64_TLBI_ALLE1        "tlbi alle1"        /* All EL1 entries, local */
#define ARM64_TLBI_VMALLE1IS    "tlbi vmalle1is"    /* All EL1 VA entries, Inner Shareable */
#define ARM64_TLBI_VMALLE1      "tlbi vmalle1"      /* All EL1 VA entries, local */
#define ARM64_TLBI_VAE1IS       "tlbi vae1is, %0"   /* VA EL1, Inner Shareable */
#define ARM64_TLBI_VAE1         "tlbi vae1, %0"     /* VA EL1, local */
#define ARM64_TLBI_ASIDE1IS     "tlbi aside1is, %0" /* ASID EL1, Inner Shareable */
#define ARM64_TLBI_ASIDE1       "tlbi aside1, %0"   /* ASID EL1, local */

/* GLOBALS ********************************************************************/

static ULONG CurrentAsid = 1;
static KSPIN_LOCK AsidLock;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Initialize ARM64 TLB management
 */
VOID
NTAPI
MiInitializeTlbManagement(VOID)
{
    DPRINT("Initializing ARM64 TLB management\n");

    /* Initialize ASID allocation lock */
    KeInitializeSpinLock(&AsidLock);

    /* TODO: Initialize ARM64-specific TLB management
     * - Set up ASID allocation strategy
     * - Configure TLB invalidation broadcast
     * - Initialize per-processor TLB state
     * - Set up TLB maintenance interrupt handlers
     */

    DPRINT("ARM64 TLB management initialized\n");
}

/*
 * @brief Flush entire TLB on current processor
 */
VOID
NTAPI
MiFlushTlbAll(VOID)
{
    /* TODO: Implement complete TLB flush for ARM64
     * - Invalidate all TLB entries at EL1
     * - Use appropriate memory barriers
     * - Handle both local and broadcast scenarios
     */

    DPRINT("Flushing all TLB entries on current CPU\n");

    /* Ensure all memory operations complete before TLB invalidation */
    __asm__ __volatile__ (
        "dsb ishst\n"                   /* Data Synchronization Barrier */
        ARM64_TLBI_ALLE1IS "\n"         /* Invalidate all EL1 TLB entries (Inner Shareable) */
        "dsb ish\n"                     /* Ensure TLB invalidation completes */
        "isb\n"                         /* Instruction Synchronization Barrier */
        ::: "memory"
    );

    DPRINT("TLB flush completed\n");
}

/*
 * @brief Flush TLB entries for a specific virtual address
 */
VOID
NTAPI
MiFlushTlbAddress(
    IN PVOID VirtualAddress)
{
    ULONG_PTR Va = (ULONG_PTR)VirtualAddress >> 12; /* Convert to page address */

    /* TODO: Implement address-specific TLB flush for ARM64
     * - Invalidate TLB entry for specific virtual address
     * - Handle large page entries appropriately
     * - Use ASID if available for more targeted invalidation
     */

    DPRINT("Flushing TLB entry for VA %p\n", VirtualAddress);

    /* Ensure memory operations complete before TLB invalidation */
    __asm__ __volatile__ (
        "dsb ishst\n"                   /* Data Synchronization Barrier */
        "tlbi vae1is, %0\n"             /* Invalidate VA at EL1 (Inner Shareable) */
        "dsb ish\n"                     /* Ensure TLB invalidation completes */
        "isb\n"                         /* Instruction Synchronization Barrier */
        :
        : "r"(Va >> 12)  /* TLB instructions need page number, not full address */
        : "memory"
    );

    DPRINT("TLB flush completed for VA %p\n", VirtualAddress);
}

/*
 * @brief Flush TLB entries for a virtual address range
 */
VOID
NTAPI
MiFlushTlbRange(
    IN PVOID StartAddress,
    IN PVOID EndAddress)
{
    ULONG_PTR StartVa, EndVa, CurrentVa;
    ULONG_PTR PageSize = PAGE_SIZE;

    StartVa = (ULONG_PTR)StartAddress & ~(PageSize - 1);
    EndVa = ((ULONG_PTR)EndAddress + PageSize - 1) & ~(PageSize - 1);

    DPRINT("Flushing TLB range %p - %p\n", (PVOID)StartVa, (PVOID)EndVa);

    /* TODO: Optimize ARM64 range TLB invalidation
     * - Use range-based TLBI operations if available
     * - Consider using broadcast invalidation for large ranges
     * - Handle different page sizes (4KB, 16KB, 64KB)
     * - Use ASID-specific invalidation when appropriate
     */

    /* For small ranges, invalidate individual pages */
    if ((EndVa - StartVa) <= (16 * PageSize))
    {
        __asm__ __volatile__ ("dsb ishst" ::: "memory");

        for (CurrentVa = StartVa; CurrentVa < EndVa; CurrentVa += PageSize)
        {
            ULONG_PTR PageVa = CurrentVa >> 12;
            __asm__ __volatile__ (
                ARM64_TLBI_VAE1IS
                :
                : "r"(PageVa)
                : "memory"
            );
        }

        __asm__ __volatile__ (
            "dsb ish\n"
            "isb\n"
            ::: "memory"
        );
    }
    else
    {
        /* For large ranges, flush entire TLB */
        MiFlushTlbAll();
    }

    DPRINT("TLB range flush completed\n");
}

/*
 * @brief Flush TLB entries for a specific ASID
 */
VOID
NTAPI
MiFlushTlbAsid(
    IN ULONG Asid)
{
    /* TODO: Implement ASID-specific TLB flush for ARM64
     * - Invalidate all TLB entries for specific ASID
     * - Handle ASID rollover and global ASID management
     * - Use appropriate broadcast for multi-processor systems
     */

    DPRINT("Flushing TLB entries for ASID %lu\n", Asid);

    if (Asid == 0)
    {
        /* ASID 0 means flush all */
        MiFlushTlbAll();
        return;
    }

    /* Flush TLB entries for specific ASID */
    __asm__ __volatile__ (
        "dsb ishst\n"                   /* Data Synchronization Barrier */
        "tlbi aside1is, %0\n"           /* Invalidate ASID at EL1 (Inner Shareable) */
        "dsb ish\n"                     /* Ensure TLB invalidation completes */
        "isb\n"                         /* Instruction Synchronization Barrier */
        :
        : "r"((ULONGLONG)Asid << 48)    /* ASID goes in bits 63:48 */
        : "memory"
    );

    DPRINT("TLB ASID flush completed\n");
}

/*
 * @brief Allocate a new ASID for a process
 */
ULONG
NTAPI
MiAllocateAsid(VOID)
{
    KIRQL OldIrql;
    ULONG Asid;

    /* TODO: Implement proper ASID allocation for ARM64
     * - Manage ASID space efficiently
     * - Handle ASID rollover and recycling
     * - Coordinate with TLB management
     * - Support both 8-bit and 16-bit ASID configurations
     */

    KeAcquireSpinLock(&AsidLock, &OldIrql);

    CurrentAsid++;
    if (CurrentAsid > 255) /* Assuming 8-bit ASID for now */
    {
        /* ASID rollover - flush all TLBs and restart */
        CurrentAsid = 1;
        MiFlushTlbAll();
        DPRINT("ASID rollover occurred, flushed all TLBs\n");
    }

    Asid = CurrentAsid;

    KeReleaseSpinLock(&AsidLock, OldIrql);

    DPRINT("Allocated ASID %lu\n", Asid);
    return Asid;
}

/*
 * @brief Free an ASID
 */
VOID
NTAPI
MiFreeAsid(
    IN ULONG Asid)
{
    /* TODO: Implement ASID deallocation for ARM64
     * - Mark ASID as available for reuse
     * - Optionally flush TLB entries for this ASID
     * - Update ASID allocation bitmap
     */

    DPRINT("Freeing ASID %lu\n", Asid);

    /* For now, just flush TLB entries for this ASID */
    if (Asid != 0)
    {
        MiFlushTlbAsid(Asid);
    }
}

/*
 * @brief Set current ASID for the processor
 */
VOID
NTAPI
MiSetCurrentAsid(
    IN ULONG Asid)
{
    /* TODO: Implement ASID switching for ARM64
     * - Write to TTBR0_EL1 or TTBR1_EL1 with new ASID
     * - Handle ASID in upper bits of translation table base register
     * - Coordinate with page table switching
     */

    DPRINT("Setting current ASID to %lu\n", Asid);

    /* For ARM64, ASID is stored in bits 63:48 of TTBR0_EL1/TTBR1_EL1 */
    /* This would typically be done as part of process context switching */

    UNREFERENCED_PARAMETER(Asid);
}

/*
 * @brief Get current ASID for the processor
 */
ULONG
NTAPI
MiGetCurrentAsid(VOID)
{
    ULONG64 Ttbr0;
    ULONG Asid;

    /* TODO: Read current ASID from TTBR0_EL1 or TTBR1_EL1
     * - Extract ASID from bits 63:48 of translation table base register
     * - Handle different ASID sizes (8-bit vs 16-bit)
     */

    __asm__ __volatile__ (
        "mrs %0, TTBR0_EL1\n"
        : "=r"(Ttbr0)
    );

    /* Extract ASID from upper 16 bits */
    Asid = (ULONG)((Ttbr0 >> 48) & 0xFFFF);

    return Asid;
}

/*
 * @brief Broadcast TLB flush to all processors
 */
VOID
NTAPI
MiBroadcastTlbFlush(
    IN ULONG FlushType,
    IN PVOID Address OPTIONAL,
    IN ULONG Asid OPTIONAL)
{
    /* TODO: Implement TLB flush broadcasting for ARM64 SMP systems
     * - Use IPI (SGI) to notify other processors
     * - Coordinate TLB flush operations across all CPUs
     * - Handle different flush types (all, address, range, ASID)
     * - Implement proper synchronization
     */

    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Asid);

    DPRINT("Broadcasting TLB flush type %lu to all processors\n", FlushType);

    switch (FlushType)
    {
        case 0: /* Flush all */
            MiFlushTlbAll();
            /* TODO: Send IPI to other processors */
            break;

        case 1: /* Flush address */
            if (Address)
            {
                MiFlushTlbAddress(Address);
                /* TODO: Send IPI to other processors */
            }
            break;

        case 2: /* Flush ASID */
            MiFlushTlbAsid(Asid);
            /* TODO: Send IPI to other processors */
            break;

        default:
            DPRINT1("Unknown TLB flush type: %lu\n", FlushType);
            break;
    }
}

/*
 * @brief Handle TLB maintenance interrupts
 */
BOOLEAN
NTAPI
MiTlbMaintenanceInterruptHandler(
    IN PKTRAP_FRAME TrapFrame)
{
    /* TODO: Implement TLB maintenance interrupt handler for ARM64
     * - Handle TLB maintenance operations from other processors
     * - Process queued TLB flush requests
     * - Coordinate with IPI system for broadcast operations
     * - Acknowledge the maintenance interrupt
     */

    UNREFERENCED_PARAMETER(TrapFrame);

    DPRINT("TLB maintenance interrupt received\n");

    /* For now, just flush the entire TLB */
    MiFlushTlbAll();

    return TRUE;
}

/*
 * @brief Get TLB statistics for debugging
 */
VOID
NTAPI
MiGetTlbStatistics(
    OUT PULONG FlushCount,
    OUT PULONG AsidAllocCount,
    OUT PULONG CurrentAsidValue)
{
    /* TODO: Implement TLB statistics collection
     * - Track TLB flush operations
     * - Monitor ASID allocation patterns
     * - Provide debugging information
     */

    if (FlushCount)
        *FlushCount = 0;    /* TODO: Track actual flush count */

    if (AsidAllocCount)
        *AsidAllocCount = CurrentAsid;

    if (CurrentAsidValue)
        *CurrentAsidValue = MiGetCurrentAsid();
}

/*
 * @brief Validate TLB state for debugging
 */
BOOLEAN
NTAPI
MiValidateTlbState(VOID)
{
    /* TODO: Implement TLB state validation for ARM64
     * - Check ASID consistency
     * - Validate TLB entry coherency
     * - Verify proper TLB maintenance
     * - Report any inconsistencies
     */

    DPRINT("Validating ARM64 TLB state\n");

    /* For now, always return success */
    return TRUE;
}