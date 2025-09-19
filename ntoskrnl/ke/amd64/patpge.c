/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/ke/amd64/patpge.c
 * PURPOSE:         AMD64 PAT (Page Attribute Table) and PGE (Page Global Enable) Management
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

/* PAT MSR */
#define MSR_IA32_PAT    0x277

/* PAT Memory Types */
#define PAT_TYPE_UC     0x00  /* Uncacheable */
#define PAT_TYPE_WC     0x01  /* Write Combining */
#define PAT_TYPE_WT     0x04  /* Write Through */
#define PAT_TYPE_WP     0x05  /* Write Protected */
#define PAT_TYPE_WB     0x06  /* Write Back */
#define PAT_TYPE_UC_MINUS 0x07 /* Uncacheable, can be overridden by MTRR */

/* PAT Index Selection (PTE bits: PCD, PWT, PAT) */
#define PAT_INDEX_0     0  /* 000: PCD=0, PWT=0, PAT=0 */
#define PAT_INDEX_1     1  /* 001: PCD=0, PWT=1, PAT=0 */
#define PAT_INDEX_2     2  /* 010: PCD=1, PWT=0, PAT=0 */
#define PAT_INDEX_3     3  /* 011: PCD=1, PWT=1, PAT=0 */
#define PAT_INDEX_4     4  /* 100: PCD=0, PWT=0, PAT=1 */
#define PAT_INDEX_5     5  /* 101: PCD=0, PWT=1, PAT=1 */
#define PAT_INDEX_6     6  /* 110: PCD=1, PWT=0, PAT=1 */
#define PAT_INDEX_7     7  /* 111: PCD=1, PWT=1, PAT=1 */

/* CR4_PGE is already defined in sdk/include/ndk/amd64/ketypes.h */
/* Using the system definition (0x80) instead of redefining */

/* PTE bits for cache control */
#define PTE_PAT         (1ULL << 7)   /* PAT bit in PTE */
#define PTE_PCD         (1ULL << 4)   /* Page Cache Disable */
#define PTE_PWT         (1ULL << 3)   /* Page Write Through */
#define CR4_PAT         (1ULL << 16)  /* PAT Enable (not a real bit, for tracking) */

/* GLOBALS *******************************************************************/

static BOOLEAN PatInitialized = FALSE;
static BOOLEAN PgeEnabled = FALSE;
static ULONGLONG OriginalPatMsr = 0;
static ULONGLONG CurrentPatMsr = 0;

/* PAT configuration table */
static UCHAR PatConfiguration[8] = {
    PAT_TYPE_WB,        /* PAT0: Write Back (default) */
    PAT_TYPE_WT,        /* PAT1: Write Through */
    PAT_TYPE_UC_MINUS,  /* PAT2: Uncacheable- */
    PAT_TYPE_UC,        /* PAT3: Uncacheable */
    PAT_TYPE_WB,        /* PAT4: Write Back */
    PAT_TYPE_WT,        /* PAT5: Write Through */
    PAT_TYPE_UC_MINUS,  /* PAT6: Uncacheable- */
    PAT_TYPE_UC         /* PAT7: Uncacheable */
};

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Builds a PAT MSR value from configuration
 * @param Config - Array of 8 PAT types
 * @return PAT MSR value
 */
static ULONGLONG
KiBuildPatMsr(
    _In_ PUCHAR Config)
{
    ULONGLONG PatMsr = 0;
    ULONG i;

    for (i = 0; i < 8; i++)
    {
        PatMsr |= ((ULONGLONG)Config[i] & 0x07) << (i * 8);
    }

    return PatMsr;
}

/*
 * @brief Validates a PAT configuration
 * @param Config - Array of 8 PAT types
 * @return TRUE if valid, FALSE otherwise
 */
static BOOLEAN
KiValidatePatConfiguration(
    _In_ PUCHAR Config)
{
    ULONG i;

    for (i = 0; i < 8; i++)
    {
        /* Check for valid memory types */
        switch (Config[i])
        {
            case PAT_TYPE_UC:
            case PAT_TYPE_WC:
            case PAT_TYPE_WT:
            case PAT_TYPE_WP:
            case PAT_TYPE_WB:
            case PAT_TYPE_UC_MINUS:
                break;

            default:
                DPRINT1("Invalid PAT type %u at index %u\n", Config[i], i);
                return FALSE;
        }
    }

    /* Ensure PAT0 is always WB for compatibility */
    if (Config[0] != PAT_TYPE_WB)
    {
        DPRINT1("PAT0 must be Write Back for compatibility\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * @brief Updates PAT MSR on current CPU
 * @param PatMsr - PAT MSR value to write
 * @return None
 */
static VOID
KiUpdatePatMsr(
    _In_ ULONGLONG PatMsr)
{
    ULONG_PTR Cr0;

    /* Disable interrupts */
    _disable();

    /* Save and modify CR0 to disable caching */
    Cr0 = __readcr0();
    __writecr0(Cr0 | CR0_CD | CR0_NW);

    /* Flush caches */
    __wbinvd();

    /* Write PAT MSR */
    __writemsr(MSR_IA32_PAT, PatMsr);

    /* Flush caches again */
    __wbinvd();

    /* Restore CR0 */
    __writecr0(Cr0);

    /* Re-enable interrupts */
    _enable();
}

/*
 * @brief IPI routine to update PAT on all CPUs
 * @param PatMsr - PAT MSR value
 * @return 0
 */
static ULONG_PTR
NTAPI
KiUpdatePatIpiRoutine(
    _In_ ULONG_PTR PatMsr)
{
    KiUpdatePatMsr((ULONGLONG)PatMsr);
    return 0;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Initializes PAT (Page Attribute Table)
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializePAT(VOID)
{
    ULONGLONG PatMsr;

    /* Check if CPU supports PAT */
    if (!(KeFeatureBits & KF_PAT))
    {
        DPRINT1("CPU does not support PAT\n");
        return;
    }

    /* Read current PAT MSR */
    OriginalPatMsr = __readmsr(MSR_IA32_PAT);

    DPRINT("Original PAT MSR: %016llx\n", OriginalPatMsr);

    /* Setup optimal PAT configuration for ReactOS */
    PatConfiguration[0] = PAT_TYPE_WB;        /* WB - Write Back */
    PatConfiguration[1] = PAT_TYPE_WT;        /* WT - Write Through */
    PatConfiguration[2] = PAT_TYPE_UC_MINUS;  /* UC- - Uncacheable minus */
    PatConfiguration[3] = PAT_TYPE_UC;        /* UC - Uncacheable */
    PatConfiguration[4] = PAT_TYPE_WC;        /* WC - Write Combining */
    PatConfiguration[5] = PAT_TYPE_WP;        /* WP - Write Protected */
    PatConfiguration[6] = PAT_TYPE_UC_MINUS;  /* UC- - Uncacheable minus */
    PatConfiguration[7] = PAT_TYPE_UC;        /* UC - Uncacheable */

    /* Build PAT MSR value */
    PatMsr = KiBuildPatMsr(PatConfiguration);

    /* Update PAT MSR on all CPUs */
    if (KeNumberProcessors > 1)
    {
        /* Use IPI to update all CPUs */
        KeIpiGenericCall(KiUpdatePatIpiRoutine, PatMsr);
    }
    else
    {
        /* Single CPU, update directly */
        KiUpdatePatMsr(PatMsr);
    }

    CurrentPatMsr = PatMsr;
    PatInitialized = TRUE;

    DPRINT1("PAT initialized with MSR: %016llx\n", PatMsr);
    DPRINT1("PAT0=WB, PAT1=WT, PAT2=UC-, PAT3=UC, PAT4=WC, PAT5=WP, PAT6=UC-, PAT7=UC\n");
}

/*
 * @implemented
 * @brief Initializes PGE (Page Global Enable)
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializePGE(VOID)
{
    ULONG_PTR Cr4;

    /* Check if CPU supports global pages */
    if (!(KeFeatureBits & KF_GLOBAL_PAGE))
    {
        DPRINT1("CPU does not support global pages\n");
        return;
    }

    /* Enable PGE in CR4 */
    Cr4 = __readcr4();
    if (!(Cr4 & CR4_PGE))
    {
        __writecr4(Cr4 | CR4_PGE);
        PgeEnabled = TRUE;
        DPRINT1("Page Global Enable (PGE) activated\n");
    }
    else
    {
        PgeEnabled = TRUE;
        DPRINT("PGE already enabled\n");
    }
}

/*
 * @implemented
 * @brief Sets PAT configuration
 * @param Config - Array of 8 PAT types
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KeSetPatConfiguration(
    _In_ PUCHAR Config)
{
    ULONGLONG PatMsr;

    /* Validate parameters */
    if (!Config)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if PAT is initialized */
    if (!PatInitialized)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Validate configuration */
    if (!KiValidatePatConfiguration(Config))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Copy configuration */
    RtlCopyMemory(PatConfiguration, Config, 8);

    /* Build new PAT MSR */
    PatMsr = KiBuildPatMsr(PatConfiguration);

    /* Update on all CPUs */
    if (KeNumberProcessors > 1)
    {
        KeIpiGenericCall(KiUpdatePatIpiRoutine, PatMsr);
    }
    else
    {
        KiUpdatePatMsr(PatMsr);
    }

    CurrentPatMsr = PatMsr;

    DPRINT("PAT configuration updated: MSR=%016llx\n", PatMsr);

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Gets current PAT configuration
 * @param Config - Buffer to receive 8 PAT types
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KeGetPatConfiguration(
    _Out_ PUCHAR Config)
{
    /* Validate parameters */
    if (!Config)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if PAT is initialized */
    if (!PatInitialized)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Copy configuration */
    RtlCopyMemory(Config, PatConfiguration, 8);

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Maps memory type to PAT index
 * @param MemoryType - Memory type (UC, WC, WT, WP, WB)
 * @return PAT index (0-7) or -1 if not found
 */
LONG
NTAPI
KeGetPatIndexForMemoryType(
    _In_ UCHAR MemoryType)
{
    ULONG i;

    if (!PatInitialized)
    {
        /* Default mapping without PAT */
        switch (MemoryType)
        {
            case PAT_TYPE_WB: return 0;
            case PAT_TYPE_WT: return 1;
            case PAT_TYPE_UC_MINUS: return 2;
            case PAT_TYPE_UC: return 3;
            default: return -1;
        }
    }

    /* Search PAT configuration */
    for (i = 0; i < 8; i++)
    {
        if (PatConfiguration[i] == MemoryType)
        {
            return i;
        }
    }

    return -1;
}

/*
 * @implemented
 * @brief Converts PAT index to PTE flags
 * @param PatIndex - PAT index (0-7)
 * @param PteFlags - Pointer to PTE flags to update
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KeSetPtePatIndex(
    _In_ ULONG PatIndex,
    _Inout_ PULONGLONG PteFlags)
{
    /* Validate parameters */
    if (!PteFlags || PatIndex > 7)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Clear existing PAT, PCD, PWT bits */
    *PteFlags &= ~(PTE_PAT | PTE_PCD | PTE_PWT);

    /* Set bits based on PAT index */
    if (PatIndex & 1) *PteFlags |= PTE_PWT;  /* Bit 0 -> PWT */
    if (PatIndex & 2) *PteFlags |= PTE_PCD;  /* Bit 1 -> PCD */
    if (PatIndex & 4) *PteFlags |= PTE_PAT;  /* Bit 2 -> PAT */

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Checks if PAT is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsPatEnabled(VOID)
{
    return PatInitialized;
}

/*
 * @implemented
 * @brief Checks if PGE is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsPgeEnabled(VOID)
{
    return PgeEnabled;
}

/*
 * @implemented
 * @brief Restores original PAT configuration
 * @return None
 */
VOID
NTAPI
KeRestoreOriginalPat(VOID)
{
    if (PatInitialized && OriginalPatMsr != 0)
    {
        /* Restore original PAT on all CPUs */
        if (KeNumberProcessors > 1)
        {
            KeIpiGenericCall(KiUpdatePatIpiRoutine, OriginalPatMsr);
        }
        else
        {
            KiUpdatePatMsr(OriginalPatMsr);
        }

        CurrentPatMsr = OriginalPatMsr;
        DPRINT("PAT restored to original value: %016llx\n", OriginalPatMsr);
    }
}

/*
 * @implemented
 * @brief Sets memory type for a physical range using PAT
 * @param PhysicalAddress - Start of physical range
 * @param Size - Size of range
 * @param MemoryType - Desired memory type
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KeSetPhysicalMemoryType(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Size,
    _In_ UCHAR MemoryType)
{
    LONG PatIndex;
    PFN_NUMBER StartPfn, EndPfn, Pfn;
    PMMPFN Pfn1;

    /* Get PAT index for memory type */
    PatIndex = KeGetPatIndexForMemoryType(MemoryType);
    if (PatIndex < 0)
    {
        DPRINT1("Memory type %u not available in PAT\n", MemoryType);
        return STATUS_NOT_SUPPORTED;
    }

    /* Calculate PFN range */
    StartPfn = PhysicalAddress.QuadPart >> PAGE_SHIFT;
    EndPfn = (PhysicalAddress.QuadPart + Size - 1) >> PAGE_SHIFT;

    /* Update PFN database entries */
    for (Pfn = StartPfn; Pfn <= EndPfn; Pfn++)
    {
        if (Pfn < MmHighestPhysicalPage)
        {
            Pfn1 = MiGetPfnEntry(Pfn);
            if (Pfn1)
            {
                /* Store PAT index in PFN entry */
                /* TODO: Add PAT index field to MMPFN structure */
                Pfn1->u3.e1.CacheAttribute = PatIndex;
            }
        }
    }

    DPRINT("Set memory type %u (PAT%ld) for range %llx-%llx\n",
           MemoryType, PatIndex,
           PhysicalAddress.QuadPart,
           PhysicalAddress.QuadPart + Size);

    return STATUS_SUCCESS;
}

/* END OF FILE */