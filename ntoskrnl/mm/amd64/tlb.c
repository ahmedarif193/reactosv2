/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/mm/amd64/tlb.c
 * PURPOSE:         AMD64 TLB (Translation Lookaside Buffer) Management
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

/* INVPCID instruction types */
#define INVPCID_INDIVIDUAL_ADDRESS     0
#define INVPCID_SINGLE_CONTEXT         1
#define INVPCID_ALL_INCLUDING_GLOBAL   2
#define INVPCID_ALL_NON_GLOBAL         3

/* TLB flush types */
#define TLB_FLUSH_SINGLE    0
#define TLB_FLUSH_MULTIPLE  1
#define TLB_FLUSH_ALL       2

/* CR4 bits */
#define CR4_PCIDE               (1ULL << 17)

/* Function forward declaration */
VOID NTAPI MiFlushEntireTlb(VOID);

/* GLOBALS *******************************************************************/

/* CPU feature flags */
BOOLEAN MiInvpcidSupported = FALSE;
BOOLEAN MiPcidSupported = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Checks CPU support for advanced TLB features
 * @return None
 */
VOID
NTAPI
MiInitializeTlbFeatures(VOID)
{
    INT CpuInfo[4];
    ULONG FeatureFlags;

    /* Check for INVPCID support */
    __cpuidex(CpuInfo, 7, 0);
    if (CpuInfo[1] & (1 << 10))  /* EBX bit 10 */
    {
        MiInvpcidSupported = TRUE;
        DPRINT1("CPU supports INVPCID instruction\n");
    }

    /* Check for PCID support */
    __cpuid(CpuInfo, 1);
    FeatureFlags = CpuInfo[2];  /* ECX */
    if (FeatureFlags & (1 << 17))  /* ECX bit 17 */
    {
        MiPcidSupported = TRUE;
        DPRINT1("CPU supports PCID (Process Context Identifiers)\n");
    }
}

/*
 * @brief Executes INVPCID instruction if available
 * @param Type - INVPCID type
 * @param Descriptor - INVPCID descriptor
 * @return TRUE if executed, FALSE if not supported
 */
static BOOLEAN
MiExecuteInvpcid(
    _In_ ULONG Type,
    _In_ PVOID Descriptor)
{
    if (!MiInvpcidSupported)
        return FALSE;

    /* Execute INVPCID instruction */
    /* Note: This requires compiler intrinsic or inline assembly */
    /* __invpcid(Type, Descriptor); */

    /* Fallback for now */
    return FALSE;
}

/*
 * @brief IPI routine for TLB flush on other CPUs
 * @param Address - Address to flush (or special values)
 * @return 0
 */
ULONG_PTR
NTAPI
MiFlushTlbIpiRoutine(
    _In_ ULONG_PTR Address)
{
    switch (Address)
    {
        case (ULONG_PTR)-1:
            /* Flush entire TLB */
            __writecr3(__readcr3());
            break;

        case (ULONG_PTR)-2:
            /* Flush all non-global entries */
            if (MiPcidSupported)
            {
                /* Use INVPCID if available */
                struct {
                    ULONG64 Pcid;
                    ULONG64 Address;
                } Descriptor = {0, 0};

                if (!MiExecuteInvpcid(INVPCID_ALL_NON_GLOBAL, &Descriptor))
                {
                    /* Fallback: toggle CR4.PGE */
                    ULONG_PTR Cr4 = __readcr4();
                    __writecr4(Cr4 & ~CR4_PGE);
                    __writecr4(Cr4);
                }
            }
            else
            {
                /* Reload CR3 */
                __writecr3(__readcr3());
            }
            break;

        default:
            /* Flush single page */
            __invlpg((PVOID)Address);
            break;
    }

    return 0;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Flushes TLB for a single page
 * @param Address - Virtual address to flush
 * @return None
 */
VOID
NTAPI
MiFlushTlb(
    _In_ PVOID Address)
{
#ifdef CONFIG_SMP
    /* Check if we need to flush on other CPUs */
    if (KeNumberProcessors > 1)
    {
        /* Send IPI to all other CPUs */
        KeIpiGenericCall(MiFlushTlbIpiRoutine, (ULONG_PTR)Address);
    }
    else
#endif
    {
        /* Single CPU, just flush locally */
        __invlpg(Address);
    }
}

/*
 * @implemented
 * @brief Flushes TLB for multiple pages
 * @param BaseAddress - Starting virtual address
 * @param NumberOfPages - Number of pages to flush
 * @return None
 */
VOID
NTAPI
MiFlushTlbMultiple(
    _In_ PVOID BaseAddress,
    _In_ ULONG NumberOfPages)
{
    ULONG i;
    PVOID Address;

    /* If too many pages, flush entire TLB instead */
    if (NumberOfPages > 32)
    {
        MiFlushEntireTlb();
        return;
    }

    /* Flush each page individually */
    Address = BaseAddress;
    for (i = 0; i < NumberOfPages; i++)
    {
        MiFlushTlb(Address);
        Address = (PVOID)((ULONG_PTR)Address + PAGE_SIZE);
    }
}

/*
 * @implemented
 * @brief Flushes the entire TLB
 * @return None
 */
VOID
NTAPI
MiFlushEntireTlb(VOID)
{
#ifdef CONFIG_SMP
    if (KeNumberProcessors > 1)
    {
        /* Send IPI to flush TLB on all CPUs */
        KeIpiGenericCall(MiFlushTlbIpiRoutine, (ULONG_PTR)-1);
    }
    else
#endif
    {
        /* Single CPU, reload CR3 */
        __writecr3(__readcr3());
    }
}

/*
 * @implemented
 * @brief Flushes all non-global TLB entries
 * @return None
 */
VOID
NTAPI
MiFlushTlbNonGlobal(VOID)
{
#ifdef CONFIG_SMP
    if (KeNumberProcessors > 1)
    {
        /* Send IPI to flush non-global entries on all CPUs */
        KeIpiGenericCall(MiFlushTlbIpiRoutine, (ULONG_PTR)-2);
    }
    else
#endif
    {
        /* Single CPU */
        if (MiPcidSupported && MiInvpcidSupported)
        {
            /* Use INVPCID to flush non-global entries */
            struct {
                ULONG64 Pcid;
                ULONG64 Address;
            } Descriptor = {0, 0};

            MiExecuteInvpcid(INVPCID_ALL_NON_GLOBAL, &Descriptor);
        }
        else
        {
            /* Toggle CR4.PGE to flush non-global entries */
            ULONG_PTR Cr4 = __readcr4();
            if (Cr4 & CR4_PGE)
            {
                __writecr4(Cr4 & ~CR4_PGE);
                __writecr4(Cr4);
            }
            else
            {
                /* No global pages, just reload CR3 */
                __writecr3(__readcr3());
            }
        }
    }
}

/*
 * @implemented
 * @brief Flushes TLB for a specific process context
 * @param Process - Process whose TLB entries to flush
 * @return None
 */
VOID
NTAPI
MiFlushProcessTlb(
    _In_ PEPROCESS Process)
{
    ULONG_PTR Cr3;

    /* Get process CR3 */
    Cr3 = Process->Pcb.DirectoryTableBase[0];

    /* Check if this is the current process */
    if (Cr3 == __readcr3())
    {
        /* Flush non-global entries for current process */
        MiFlushTlbNonGlobal();
    }
    else
    {
        /* Process not active on this CPU, nothing to do */
        /* TLB will be flushed when process is switched to */
    }

#ifdef CONFIG_SMP
    /* TODO: Send targeted IPI to CPUs running this process */
#endif
}

/*
 * @implemented
 * @brief Flushes TLB entries for a range of addresses
 * @param StartAddress - Starting virtual address
 * @param EndAddress - Ending virtual address
 * @return None
 */
VOID
NTAPI
MiFlushTlbRange(
    _In_ PVOID StartAddress,
    _In_ PVOID EndAddress)
{
    ULONG_PTR Start, End, Address;
    ULONG NumberOfPages;

    /* Align addresses to page boundaries */
    Start = (ULONG_PTR)StartAddress & ~(PAGE_SIZE - 1);
    End = ((ULONG_PTR)EndAddress + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Calculate number of pages */
    NumberOfPages = (End - Start) / PAGE_SIZE;

    /* If range is too large, flush entire TLB */
    if (NumberOfPages > 64)
    {
        MiFlushEntireTlb();
        return;
    }

    /* Flush each page in the range */
    for (Address = Start; Address < End; Address += PAGE_SIZE)
    {
        MiFlushTlb((PVOID)Address);
    }
}

/*
 * @implemented
 * @brief Performs a lazy TLB flush (deferred until necessary)
 * @param OldProcess - Process being switched from
 * @param NewProcess - Process being switched to
 * @return None
 */
VOID
NTAPI
MiPerformLazyTlbFlush(
    _In_opt_ PEPROCESS OldProcess,
    _In_ PEPROCESS NewProcess)
{
    ULONG_PTR NewCr3;

    /* Get new process CR3 */
    NewCr3 = NewProcess->Pcb.DirectoryTableBase[0];

    /* Check if we're switching to a different address space */
    if (!OldProcess ||
        OldProcess->Pcb.DirectoryTableBase[0] != NewCr3)
    {
        /* Load new CR3, which flushes non-global TLB entries */
        __writecr3(NewCr3);
    }
    /* else: Same address space, no flush needed */
}

/*
 * @implemented
 * @brief Invalidates TLB entries for freed pages
 * @param PageFrameNumbers - Array of PFNs that were freed
 * @param NumberOfPages - Number of pages in array
 * @return None
 */
VOID
NTAPI
MiInvalidateFreedPages(
    _In_ PPFN_NUMBER PageFrameNumbers,
    _In_ ULONG NumberOfPages)
{
    /* For freed pages, we typically need a full TLB flush */
    /* since we don't track all virtual addresses that mapped them */

    if (NumberOfPages > 8)
    {
        /* Many pages freed, flush entire TLB */
        MiFlushEntireTlb();
    }
    else
    {
        /* Few pages, try to find and flush specific mappings */
        /* TODO: Implement reverse mapping lookup */
        MiFlushEntireTlb();
    }
}

/* INITIALIZATION ************************************************************/

/*
 * @implemented
 * @brief Initializes TLB management subsystem
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
MmInitializeTlbManagement(VOID)
{
    /* Detect CPU features */
    MiInitializeTlbFeatures();

    /* Enable global pages if supported */
    if (KeFeatureBits & KF_GLOBAL_PAGE)
    {
        ULONG_PTR Cr4 = __readcr4();
        __writecr4(Cr4 | CR4_PGE);
        DPRINT1("Global pages enabled (CR4.PGE)\n");
    }

    /* Enable PCID if supported */
    if (MiPcidSupported)
    {
        ULONG_PTR Cr4 = __readcr4();
        __writecr4(Cr4 | CR4_PCIDE);
        DPRINT1("PCID enabled (CR4.PCIDE)\n");
    }

    DPRINT1("TLB management initialized\n");
}

/* END OF FILE */