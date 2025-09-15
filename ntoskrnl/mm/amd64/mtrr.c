/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/mm/amd64/mtrr.c
 * PURPOSE:         AMD64 MTRR (Memory Type Range Register) Support
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

/* MTRR MSR addresses */
#define MSR_MTRR_CAPABILITIES   0x000000FE
#define MSR_MTRR_DEF_TYPE      0x000002FF
#define MSR_MTRR_PHYSBASE0     0x00000200
#define MSR_MTRR_PHYSMASK0     0x00000201
#define MSR_MTRR_FIX64K_00000  0x00000250
#define MSR_MTRR_FIX16K_80000  0x00000258
#define MSR_MTRR_FIX16K_A0000  0x00000259
#define MSR_MTRR_FIX4K_C0000   0x00000268
#define MSR_MTRR_FIX4K_C8000   0x00000269
#define MSR_MTRR_FIX4K_D0000   0x0000026A
#define MSR_MTRR_FIX4K_D8000   0x0000026B
#define MSR_MTRR_FIX4K_E0000   0x0000026C
#define MSR_MTRR_FIX4K_E8000   0x0000026D
#define MSR_MTRR_FIX4K_F0000   0x0000026E
#define MSR_MTRR_FIX4K_F8000   0x0000026F

/* Memory types */
#define MTRR_TYPE_UNCACHEABLE   0x00
#define MTRR_TYPE_WRITECOMBINE  0x01
#define MTRR_TYPE_WRITETHROUGH  0x04
#define MTRR_TYPE_WRITEPROTECT  0x05
#define MTRR_TYPE_WRITEBACK     0x06
#define MTRR_TYPE_INVALID       0xFF

/* MTRR capability bits */
#define MTRR_CAP_VCNT_MASK      0x000000FF  /* Variable MTRR count */
#define MTRR_CAP_FIX_SUPPORTED  0x00000100  /* Fixed MTRRs supported */
#define MTRR_CAP_WC_SUPPORTED   0x00000400  /* Write-combining supported */

/* MTRR default type bits */
#define MTRR_DEF_TYPE_MASK      0x000000FF  /* Default memory type */
#define MTRR_DEF_FIXED_ENABLE   0x00000400  /* Fixed MTRRs enabled */
#define MTRR_DEF_ENABLE         0x00000800  /* MTRRs enabled */

/* MTRR physmask bits */
#define MTRR_PHYSMASK_VALID     0x00000800  /* MTRR is valid */

/* STRUCTURES ****************************************************************/

typedef struct _MTRR_RANGE
{
    PHYSICAL_ADDRESS BaseAddress;
    ULONGLONG Size;
    UCHAR MemoryType;
    BOOLEAN Valid;
} MTRR_RANGE, *PMTRR_RANGE;

typedef struct _MTRR_STATE
{
    ULONGLONG Capabilities;
    ULONGLONG DefaultType;
    ULONG VariableCount;
    BOOLEAN FixedSupported;
    BOOLEAN WriteCombiningsupported;
    BOOLEAN Enabled;
    MTRR_RANGE Variable[64];  /* Max 64 variable MTRRs */
} MTRR_STATE, *PMTRR_STATE;

/* GLOBALS *******************************************************************/

static MTRR_STATE MtrrState;
static BOOLEAN MtrrInitialized = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Reads an MSR (Model Specific Register)
 * @param Msr - MSR address
 * @return MSR value
 */
static ULONGLONG
MiReadMsr(ULONG Msr)
{
    return __readmsr(Msr);
}

/*
 * @brief Writes an MSR (Model Specific Register)
 * @param Msr - MSR address
 * @param Value - Value to write
 * @return None
 */
static VOID
MiWriteMsr(ULONG Msr, ULONGLONG Value)
{
    __writemsr(Msr, Value);
}

/*
 * @brief Disables MTRRs temporarily
 * @return Previous CR0 value
 */
static ULONG_PTR
MiDisableMtrrs(VOID)
{
    ULONG_PTR Cr0;
    ULONGLONG DefType;

    /* Disable interrupts */
    _disable();

    /* Save and modify CR0 to disable caching */
    Cr0 = __readcr0();
    __writecr0(Cr0 | CR0_CD | CR0_NW);

    /* Flush caches */
    __wbinvd();

    /* Disable MTRRs */
    DefType = MiReadMsr(MSR_MTRR_DEF_TYPE);
    MiWriteMsr(MSR_MTRR_DEF_TYPE, DefType & ~MTRR_DEF_ENABLE);

    return Cr0;
}

/*
 * @brief Re-enables MTRRs
 * @param Cr0 - Previous CR0 value
 * @return None
 */
static VOID
MiEnableMtrrs(ULONG_PTR Cr0)
{
    ULONGLONG DefType;

    /* Re-enable MTRRs */
    DefType = MiReadMsr(MSR_MTRR_DEF_TYPE);
    MiWriteMsr(MSR_MTRR_DEF_TYPE, DefType | MTRR_DEF_ENABLE);

    /* Flush caches */
    __wbinvd();

    /* Restore CR0 */
    __writecr0(Cr0);

    /* Re-enable interrupts */
    _enable();
}

/*
 * @brief Reads current MTRR configuration
 * @return None
 */
static VOID
MiReadMtrrConfiguration(VOID)
{
    ULONG i;
    ULONGLONG Base, Mask;

    /* Read capabilities */
    MtrrState.Capabilities = MiReadMsr(MSR_MTRR_CAPABILITIES);
    MtrrState.VariableCount = MtrrState.Capabilities & MTRR_CAP_VCNT_MASK;
    MtrrState.FixedSupported = (MtrrState.Capabilities & MTRR_CAP_FIX_SUPPORTED) != 0;
    MtrrState.WriteCombiningsupported = (MtrrState.Capabilities & MTRR_CAP_WC_SUPPORTED) != 0;

    /* Read default type */
    MtrrState.DefaultType = MiReadMsr(MSR_MTRR_DEF_TYPE);
    MtrrState.Enabled = (MtrrState.DefaultType & MTRR_DEF_ENABLE) != 0;

    /* Read variable MTRRs */
    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        Base = MiReadMsr(MSR_MTRR_PHYSBASE0 + i * 2);
        Mask = MiReadMsr(MSR_MTRR_PHYSMASK0 + i * 2);

        if (Mask & MTRR_PHYSMASK_VALID)
        {
            MtrrState.Variable[i].Valid = TRUE;
            MtrrState.Variable[i].BaseAddress.QuadPart = Base & ~0xFFF;
            MtrrState.Variable[i].MemoryType = Base & 0xFF;

            /* Calculate size from mask */
            ULONGLONG MaskBits = Mask & ~0xFFF;
            ULONGLONG Size = 1;
            while ((MaskBits & Size) == 0)
            {
                Size <<= 1;
            }
            MtrrState.Variable[i].Size = Size;
        }
        else
        {
            MtrrState.Variable[i].Valid = FALSE;
        }
    }
}

/*
 * @brief Finds an available variable MTRR
 * @return MTRR index, or -1 if none available
 */
static INT
MiFindAvailableMtrr(VOID)
{
    ULONG i;

    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        if (!MtrrState.Variable[i].Valid)
        {
            return i;
        }
    }

    return -1;
}

/*
 * @brief Validates if a size is a power of 2
 * @param Size - Size to validate
 * @return TRUE if power of 2, FALSE otherwise
 */
static BOOLEAN
MiIsPowerOfTwo(ULONGLONG Size)
{
    return (Size != 0) && ((Size & (Size - 1)) == 0);
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Initializes MTRR support
 * @param FinalCpu - TRUE if this is the last CPU to initialize
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMTRR(
    _In_ BOOLEAN FinalCpu)
{
    ULONG_PTR Cr0;

    DPRINT("Initializing MTRR support (FinalCpu=%d)\n", FinalCpu);

    /* Check if CPU supports MTRRs */
    if (!(KeFeatureBits & KF_MTRR))
    {
        DPRINT1("CPU does not support MTRRs\n");
        return;
    }

    /* Read current configuration */
    MiReadMtrrConfiguration();

    DPRINT("MTRR: %lu variable ranges, Fixed=%d, WC=%d, Enabled=%d\n",
           MtrrState.VariableCount,
           MtrrState.FixedSupported,
           MtrrState.WriteCombiningsupported,
           MtrrState.Enabled);

    /* If MTRRs are not enabled, enable them */
    if (!MtrrState.Enabled)
    {
        Cr0 = MiDisableMtrrs();

        /* Set default type to Write-Back */
        MiWriteMsr(MSR_MTRR_DEF_TYPE,
                   MTRR_TYPE_WRITEBACK | MTRR_DEF_ENABLE |
                   (MtrrState.FixedSupported ? MTRR_DEF_FIXED_ENABLE : 0));

        MiEnableMtrrs(Cr0);

        DPRINT1("MTRRs enabled with Write-Back default\n");
    }

    /* Log current MTRR ranges */
    if (FinalCpu)
    {
        ULONG i;
        for (i = 0; i < MtrrState.VariableCount; i++)
        {
            if (MtrrState.Variable[i].Valid)
            {
                DPRINT("MTRR[%lu]: Base=%llx, Size=%llx, Type=%d\n",
                       i,
                       MtrrState.Variable[i].BaseAddress.QuadPart,
                       MtrrState.Variable[i].Size,
                       MtrrState.Variable[i].MemoryType);
            }
        }
    }

    MtrrInitialized = TRUE;
}

/*
 * @implemented
 * @brief Sets an MTRR range for a physical memory region
 * @param BaseAddress - Physical base address
 * @param Size - Size of region
 * @param MemoryType - Memory type (UC, WC, WT, WP, WB)
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmSetMTRRRange(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONGLONG Size,
    _In_ ULONG MemoryType)
{
    INT MtrrIndex;
    ULONG_PTR Cr0;
    ULONGLONG Base, Mask;

    DPRINT("MmSetMTRRRange: Base=%llx, Size=%llx, Type=%lu\n",
           BaseAddress.QuadPart, Size, MemoryType);

    /* Validate parameters */
    if (!MtrrInitialized)
    {
        DPRINT1("MTRRs not initialized\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Check memory type */
    if (MemoryType > MTRR_TYPE_WRITEBACK)
    {
        DPRINT1("Invalid memory type %lu\n", MemoryType);
        return STATUS_INVALID_PARAMETER;
    }

    /* Size must be power of 2 and at least 4KB */
    if (!MiIsPowerOfTwo(Size) || Size < PAGE_SIZE)
    {
        DPRINT1("Invalid size %llx (must be power of 2 >= 4KB)\n", Size);
        return STATUS_INVALID_PARAMETER;
    }

    /* Base must be aligned to size */
    if (BaseAddress.QuadPart & (Size - 1))
    {
        DPRINT1("Base %llx not aligned to size %llx\n",
                BaseAddress.QuadPart, Size);
        return STATUS_INVALID_PARAMETER;
    }

    /* Find available MTRR */
    MtrrIndex = MiFindAvailableMtrr();
    if (MtrrIndex < 0)
    {
        DPRINT1("No available MTRRs\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Prepare base and mask values */
    Base = BaseAddress.QuadPart | MemoryType;
    Mask = (~(Size - 1)) & ((1ULL << 52) - PAGE_SIZE);
    Mask |= MTRR_PHYSMASK_VALID;

    /* Disable MTRRs while modifying */
    Cr0 = MiDisableMtrrs();

    /* Write MTRR */
    MiWriteMsr(MSR_MTRR_PHYSBASE0 + MtrrIndex * 2, Base);
    MiWriteMsr(MSR_MTRR_PHYSMASK0 + MtrrIndex * 2, Mask);

    /* Update state */
    MtrrState.Variable[MtrrIndex].Valid = TRUE;
    MtrrState.Variable[MtrrIndex].BaseAddress = BaseAddress;
    MtrrState.Variable[MtrrIndex].Size = Size;
    MtrrState.Variable[MtrrIndex].MemoryType = MemoryType;

    /* Re-enable MTRRs */
    MiEnableMtrrs(Cr0);

    DPRINT("MTRR[%d] configured: Base=%llx, Size=%llx, Type=%lu\n",
           MtrrIndex, BaseAddress.QuadPart, Size, MemoryType);

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Queries current MTRR configuration
 * @param Buffer - Buffer to receive MTRR information
 * @param BufferSize - Size of buffer
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmQueryMTRR(
    _Out_ PVOID Buffer,
    _In_ ULONG BufferSize)
{
    PMTRR_RANGE Ranges;
    ULONG RequiredSize;
    ULONG i, Count = 0;

    /* Check if initialized */
    if (!MtrrInitialized)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Count valid ranges */
    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        if (MtrrState.Variable[i].Valid)
            Count++;
    }

    /* Calculate required size */
    RequiredSize = Count * sizeof(MTRR_RANGE);
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Copy ranges to buffer */
    Ranges = (PMTRR_RANGE)Buffer;
    Count = 0;
    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        if (MtrrState.Variable[i].Valid)
        {
            Ranges[Count++] = MtrrState.Variable[i];
        }
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Clears an MTRR range
 * @param BaseAddress - Physical base address of range to clear
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
MmClearMTRRRange(
    _In_ PHYSICAL_ADDRESS BaseAddress)
{
    ULONG i;
    ULONG_PTR Cr0;

    /* Find matching MTRR */
    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        if (MtrrState.Variable[i].Valid &&
            MtrrState.Variable[i].BaseAddress.QuadPart == BaseAddress.QuadPart)
        {
            /* Disable MTRRs while modifying */
            Cr0 = MiDisableMtrrs();

            /* Clear MTRR */
            MiWriteMsr(MSR_MTRR_PHYSMASK0 + i * 2, 0);

            /* Update state */
            MtrrState.Variable[i].Valid = FALSE;

            /* Re-enable MTRRs */
            MiEnableMtrrs(Cr0);

            DPRINT("MTRR[%lu] cleared\n", i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/*
 * @implemented
 * @brief Gets the memory type for a physical address
 * @param PhysicalAddress - Physical address to query
 * @return Memory type
 */
UCHAR
NTAPI
MmGetMTRRMemoryType(
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    ULONG i;
    UCHAR MemoryType = MTRR_TYPE_INVALID;

    if (!MtrrInitialized)
        return MTRR_TYPE_UNCACHEABLE;

    /* Check variable MTRRs */
    for (i = 0; i < MtrrState.VariableCount; i++)
    {
        if (MtrrState.Variable[i].Valid)
        {
            PHYSICAL_ADDRESS Start = MtrrState.Variable[i].BaseAddress;
            PHYSICAL_ADDRESS End;
            End.QuadPart = Start.QuadPart + MtrrState.Variable[i].Size;

            if (PhysicalAddress.QuadPart >= Start.QuadPart &&
                PhysicalAddress.QuadPart < End.QuadPart)
            {
                MemoryType = MtrrState.Variable[i].MemoryType;
                break;
            }
        }
    }

    /* If no match, use default type */
    if (MemoryType == MTRR_TYPE_INVALID)
    {
        MemoryType = MtrrState.DefaultType & MTRR_DEF_TYPE_MASK;
    }

    return MemoryType;
}

/* END OF FILE */