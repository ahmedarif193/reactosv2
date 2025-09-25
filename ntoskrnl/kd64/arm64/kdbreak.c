/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Debugger Breakpoint Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Breakpoint Instruction */
#define ARM64_BREAKPOINT_VALUE      0xD4200000  /* BRK #0 instruction */
#define ARM64_KD_BREAKPOINT_VALUE   0xD4200020  /* BRK #1 for kernel debugger */
#define ARM64_ASSERT_BREAKPOINT     0xD4200040  /* BRK #2 for assertions */
#define ARM64_BREAKPOINT_SIZE       4

/* Maximum number of breakpoints */
#define KD_BREAKPOINT_MAX           32

/* Breakpoint states */
#define KD_BREAKPOINT_FREE          0
#define KD_BREAKPOINT_IN_USE        1
#define KD_BREAKPOINT_SUSPENDED     2
#define KD_BREAKPOINT_EXPIRED       3

/* ARM64 Debug Registers */
#define ARM64_MAX_HW_BREAKPOINTS    16
#define ARM64_MAX_HW_WATCHPOINTS    16

/* Debug Control Register (MDSCR_EL1) bits */
#define MDSCR_SS_BIT                (1 << 0)   /* Software step */
#define MDSCR_ERR_BIT               (1 << 6)   /* External debug request */
#define MDSCR_TDCC_BIT              (1 << 12)  /* Trap debug comms channel */
#define MDSCR_KDE_BIT               (1 << 13)  /* Kernel debug enable */
#define MDSCR_MDE_BIT               (1 << 15)  /* Monitor debug enable */
#define MDSCR_SC2_BIT               (1 << 19)  /* Sample-based profiling */
#define MDSCR_TDA_BIT               (1 << 21)  /* Trap debug access */
#define MDSCR_TXU_BIT               (1 << 26)  /* Trap EL0 access to debug */
#define MDSCR_RXO_BIT               (1 << 27)  /* Reserved */

/* Hardware Breakpoint Control Register bits */
#define DBG_BCR_EN                  (1 << 0)   /* Enable */
#define DBG_BCR_PMC_SHIFT           1           /* Privilege mode control */
#define DBG_BCR_PMC_MASK            0x3
#define DBG_BCR_BAS_SHIFT           5           /* Byte address select */
#define DBG_BCR_BAS_MASK            0xF
#define DBG_BCR_HMC_BIT             (1 << 13)  /* Higher mode control */
#define DBG_BCR_SSC_SHIFT           14          /* Security state control */
#define DBG_BCR_SSC_MASK            0x3
#define DBG_BCR_LBN_SHIFT           16          /* Linked breakpoint number */
#define DBG_BCR_LBN_MASK            0xF
#define DBG_BCR_BT_SHIFT            20          /* Breakpoint type */
#define DBG_BCR_BT_MASK             0xF

/* Breakpoint types */
#define DBG_BCR_BT_ADDR_MATCH       0x0         /* Unlinked address match */
#define DBG_BCR_BT_LINKED           0x1         /* Linked address match */
#define DBG_BCR_BT_CONTEXT          0x2         /* Unlinked context match */
#define DBG_BCR_BT_ADDR_MISMATCH    0x4         /* Unlinked address mismatch */
#define DBG_BCR_BT_LINKED_ADDR      0x8         /* Linked address match */
#define DBG_BCR_BT_LINKED_CONTEXT   0xA         /* Linked context match */
#define DBG_BCR_BT_ADDR_RANGE       0xC         /* Address range */

/* DATA STRUCTURES ************************************************************/

/* Software breakpoint structure */
typedef struct _KD_BREAKPOINT
{
    ULONG State;
    PVOID Address;
    ULONG OriginalInstruction;
    ULONG Flags;
    ULONG PassCount;
    ULONG HitCount;
    BOOLEAN Temporary;
    BOOLEAN Private;
} KD_BREAKPOINT, *PKD_BREAKPOINT;

/* Hardware breakpoint structure */
typedef struct _KD_HW_BREAKPOINT
{
    BOOLEAN Enabled;
    PVOID Address;
    ULONG Type;
    ULONG Length;
    ULONG64 Control;
    ULONG64 Value;
} KD_HW_BREAKPOINT, *PKD_HW_BREAKPOINT;

/* GLOBALS ********************************************************************/

static KD_BREAKPOINT KdBreakpoints[KD_BREAKPOINT_MAX];
static KD_HW_BREAKPOINT KdHwBreakpoints[ARM64_MAX_HW_BREAKPOINTS];
static KD_HW_BREAKPOINT KdHwWatchpoints[ARM64_MAX_HW_WATCHPOINTS];
static KSPIN_LOCK KdBreakpointLock;
static BOOLEAN KdBreakpointsInitialized = FALSE;
static ULONG KdNumberOfHwBreakpoints = 0;
static ULONG KdNumberOfHwWatchpoints = 0;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Read ARM64 debug register
 */
static
ULONG64
KdpReadDebugRegister(
    IN ULONG Register)
{
    ULONG64 Value = 0;

    switch (Register)
    {
        case 0:  /* MDSCR_EL1 */
            __asm__ __volatile__ ("mrs %0, MDSCR_EL1" : "=r"(Value));
            break;
        case 1:  /* ID_AA64DFR0_EL1 - Debug feature register */
            __asm__ __volatile__ ("mrs %0, ID_AA64DFR0_EL1" : "=r"(Value));
            break;
        default:
            DPRINT1("Invalid debug register: %lu\n", Register);
            break;
    }

    return Value;
}

/*
 * @brief Write ARM64 debug register
 */
static
VOID
KdpWriteDebugRegister(
    IN ULONG Register,
    IN ULONG64 Value)
{
    switch (Register)
    {
        case 0:  /* MDSCR_EL1 */
            __asm__ __volatile__ ("msr MDSCR_EL1, %0" :: "r"(Value));
            __asm__ __volatile__ ("isb" ::: "memory");
            break;
        default:
            DPRINT1("Invalid debug register: %lu\n", Register);
            break;
    }
}

/*
 * @brief Get number of hardware breakpoints/watchpoints
 */
static
VOID
KdpGetDebugCapabilities(VOID)
{
    ULONG64 DfrEl1;

    /* Read debug feature register */
    DfrEl1 = KdpReadDebugRegister(1);

    /* Extract breakpoint count (bits 12-15) */
    KdNumberOfHwBreakpoints = ((DfrEl1 >> 12) & 0xF) + 1;
    if (KdNumberOfHwBreakpoints > ARM64_MAX_HW_BREAKPOINTS)
        KdNumberOfHwBreakpoints = ARM64_MAX_HW_BREAKPOINTS;

    /* Extract watchpoint count (bits 20-23) */
    KdNumberOfHwWatchpoints = ((DfrEl1 >> 20) & 0xF) + 1;
    if (KdNumberOfHwWatchpoints > ARM64_MAX_HW_WATCHPOINTS)
        KdNumberOfHwWatchpoints = ARM64_MAX_HW_WATCHPOINTS;

    DPRINT("ARM64 Debug: %lu hardware breakpoints, %lu watchpoints\n",
           KdNumberOfHwBreakpoints, KdNumberOfHwWatchpoints);
}

/*
 * @brief Set hardware breakpoint
 */
static
BOOLEAN
KdpSetHardwareBreakpoint(
    IN ULONG Index,
    IN PVOID Address,
    IN ULONG Type,
    IN ULONG Length)
{
    ULONG64 Control = 0;
    ULONG64 Value;

    if (Index >= KdNumberOfHwBreakpoints)
    {
        DPRINT1("Hardware breakpoint index %lu out of range\n", Index);
        return FALSE;
    }

    /* Build control register value */
    Control = DBG_BCR_EN;                                      /* Enable */
    Control |= (0x3 << DBG_BCR_PMC_SHIFT);                    /* EL1 and EL0 */
    Control |= (0xF << DBG_BCR_BAS_SHIFT);                    /* All bytes */
    Control |= (DBG_BCR_BT_ADDR_MATCH << DBG_BCR_BT_SHIFT);   /* Address match */

    Value = (ULONG64)Address;

    /* Write to hardware registers */
    switch (Index)
    {
        case 0:
            __asm__ __volatile__ ("msr DBGBVR0_EL1, %0" :: "r"(Value));
            __asm__ __volatile__ ("msr DBGBCR0_EL1, %0" :: "r"(Control));
            break;
        case 1:
            __asm__ __volatile__ ("msr DBGBVR1_EL1, %0" :: "r"(Value));
            __asm__ __volatile__ ("msr DBGBCR1_EL1, %0" :: "r"(Control));
            break;
        /* Add more cases as needed */
        default:
            DPRINT1("Hardware breakpoint %lu not implemented\n", Index);
            return FALSE;
    }

    /* Ensure changes take effect */
    __asm__ __volatile__ ("isb" ::: "memory");

    /* Store breakpoint information */
    KdHwBreakpoints[Index].Enabled = TRUE;
    KdHwBreakpoints[Index].Address = Address;
    KdHwBreakpoints[Index].Type = Type;
    KdHwBreakpoints[Index].Length = Length;
    KdHwBreakpoints[Index].Control = Control;
    KdHwBreakpoints[Index].Value = Value;

    DPRINT("Set hardware breakpoint %lu at %p\n", Index, Address);
    return TRUE;
}

/*
 * @brief Clear hardware breakpoint
 */
static
BOOLEAN
KdpClearHardwareBreakpoint(
    IN ULONG Index)
{
    if (Index >= KdNumberOfHwBreakpoints)
    {
        DPRINT1("Hardware breakpoint index %lu out of range\n", Index);
        return FALSE;
    }

    /* Clear hardware registers */
    switch (Index)
    {
        case 0:
            __asm__ __volatile__ ("msr DBGBCR0_EL1, xzr");
            __asm__ __volatile__ ("msr DBGBVR0_EL1, xzr");
            break;
        case 1:
            __asm__ __volatile__ ("msr DBGBCR1_EL1, xzr");
            __asm__ __volatile__ ("msr DBGBVR1_EL1, xzr");
            break;
        /* Add more cases as needed */
        default:
            DPRINT1("Hardware breakpoint %lu not implemented\n", Index);
            return FALSE;
    }

    /* Ensure changes take effect */
    __asm__ __volatile__ ("isb" ::: "memory");

    /* Clear breakpoint information */
    KdHwBreakpoints[Index].Enabled = FALSE;
    KdHwBreakpoints[Index].Address = NULL;
    KdHwBreakpoints[Index].Type = 0;
    KdHwBreakpoints[Index].Length = 0;
    KdHwBreakpoints[Index].Control = 0;
    KdHwBreakpoints[Index].Value = 0;

    DPRINT("Cleared hardware breakpoint %lu\n", Index);
    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize kernel debugger breakpoint support
 * @implemented
 */
VOID
NTAPI
KdInitializeBreakpoints(VOID)
{
    ULONG i;
    ULONG64 MdscrEl1;

    if (KdBreakpointsInitialized)
        return;

    DPRINT("Initializing ARM64 kernel debugger breakpoint support\n");

    /* Initialize breakpoint lock */
    KeInitializeSpinLock(&KdBreakpointLock);

    /* Clear all software breakpoints */
    RtlZeroMemory(KdBreakpoints, sizeof(KdBreakpoints));

    /* Clear all hardware breakpoints */
    RtlZeroMemory(KdHwBreakpoints, sizeof(KdHwBreakpoints));
    RtlZeroMemory(KdHwWatchpoints, sizeof(KdHwWatchpoints));

    /* Get debug capabilities */
    KdpGetDebugCapabilities();

    /* Enable debug exceptions in EL1 */
    MdscrEl1 = KdpReadDebugRegister(0);
    MdscrEl1 |= MDSCR_KDE_BIT | MDSCR_MDE_BIT;
    KdpWriteDebugRegister(0, MdscrEl1);

    /* Clear all hardware breakpoint registers */
    for (i = 0; i < KdNumberOfHwBreakpoints; i++)
    {
        KdpClearHardwareBreakpoint(i);
    }

    KdBreakpointsInitialized = TRUE;
    DPRINT("ARM64 breakpoint support initialized\n");
}

/*
 * @brief Set a software breakpoint
 * @implemented
 */
ULONG
NTAPI
KdSetBreakpoint(
    IN PVOID Address,
    IN BOOLEAN Temporary)
{
    KIRQL OldIrql;
    ULONG Index;
    ULONG OriginalInstruction;
    NTSTATUS Status;

    DPRINT("Setting breakpoint at %p (temporary=%d)\n", Address, Temporary);

    /* Validate address alignment */
    if ((ULONG_PTR)Address & 0x3)
    {
        DPRINT1("Breakpoint address %p not aligned\n", Address);
        return 0;
    }

    KeAcquireSpinLock(&KdBreakpointLock, &OldIrql);

    /* Find a free breakpoint slot */
    for (Index = 0; Index < KD_BREAKPOINT_MAX; Index++)
    {
        if (KdBreakpoints[Index].State == KD_BREAKPOINT_FREE)
            break;
    }

    if (Index >= KD_BREAKPOINT_MAX)
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        DPRINT1("No free breakpoint slots\n");
        return 0;
    }

    /* Read original instruction */
    Status = MmCopyFromCaller(&OriginalInstruction, Address, sizeof(ULONG));
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        DPRINT1("Failed to read original instruction at %p\n", Address);
        return 0;
    }

    /* Write breakpoint instruction */
    Status = MmCopyToCaller(Address, &ARM64_KD_BREAKPOINT_VALUE, sizeof(ULONG));
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        DPRINT1("Failed to write breakpoint at %p\n", Address);
        return 0;
    }

    /* Flush instruction cache for modified location */
    __asm__ __volatile__ (
        "dc cvau, %0\n"     /* Clean D-cache by VA to PoU */
        "dsb ish\n"         /* Ensure completion */
        "ic ivau, %0\n"     /* Invalidate I-cache by VA to PoU */
        "dsb ish\n"         /* Ensure completion */
        "isb\n"             /* Synchronize instruction stream */
        :
        : "r"(Address)
        : "memory"
    );

    /* Store breakpoint information */
    KdBreakpoints[Index].State = KD_BREAKPOINT_IN_USE;
    KdBreakpoints[Index].Address = Address;
    KdBreakpoints[Index].OriginalInstruction = OriginalInstruction;
    KdBreakpoints[Index].Temporary = Temporary;
    KdBreakpoints[Index].HitCount = 0;
    KdBreakpoints[Index].PassCount = 0;

    KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

    DPRINT("Breakpoint %lu set at %p\n", Index + 1, Address);
    return Index + 1;  /* Return 1-based handle */
}

/*
 * @brief Delete a breakpoint
 * @implemented
 */
BOOLEAN
NTAPI
KdDeleteBreakpoint(
    IN ULONG Handle)
{
    KIRQL OldIrql;
    ULONG Index;
    PKD_BREAKPOINT Breakpoint;
    NTSTATUS Status;

    if (Handle == 0 || Handle > KD_BREAKPOINT_MAX)
    {
        DPRINT1("Invalid breakpoint handle %lu\n", Handle);
        return FALSE;
    }

    Index = Handle - 1;

    KeAcquireSpinLock(&KdBreakpointLock, &OldIrql);

    Breakpoint = &KdBreakpoints[Index];
    if (Breakpoint->State == KD_BREAKPOINT_FREE)
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        DPRINT1("Breakpoint %lu already free\n", Handle);
        return FALSE;
    }

    /* Restore original instruction */
    Status = MmCopyToCaller(Breakpoint->Address,
                           &Breakpoint->OriginalInstruction,
                           sizeof(ULONG));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to restore instruction at %p\n", Breakpoint->Address);
        /* Continue anyway to free the slot */
    }
    else
    {
        /* Flush instruction cache */
        __asm__ __volatile__ (
            "dc cvau, %0\n"
            "dsb ish\n"
            "ic ivau, %0\n"
            "dsb ish\n"
            "isb\n"
            :
            : "r"(Breakpoint->Address)
            : "memory"
        );
    }

    /* Free the breakpoint slot */
    RtlZeroMemory(Breakpoint, sizeof(KD_BREAKPOINT));

    KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

    DPRINT("Deleted breakpoint %lu\n", Handle);
    return TRUE;
}

/*
 * @brief Enable a breakpoint
 * @implemented
 */
BOOLEAN
NTAPI
KdEnableBreakpoint(
    IN ULONG Handle)
{
    KIRQL OldIrql;
    ULONG Index;
    PKD_BREAKPOINT Breakpoint;
    NTSTATUS Status;

    if (Handle == 0 || Handle > KD_BREAKPOINT_MAX)
        return FALSE;

    Index = Handle - 1;

    KeAcquireSpinLock(&KdBreakpointLock, &OldIrql);

    Breakpoint = &KdBreakpoints[Index];
    if (Breakpoint->State != KD_BREAKPOINT_SUSPENDED)
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        return FALSE;
    }

    /* Write breakpoint instruction */
    Status = MmCopyToCaller(Breakpoint->Address,
                           &ARM64_KD_BREAKPOINT_VALUE,
                           sizeof(ULONG));
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        return FALSE;
    }

    /* Flush instruction cache */
    __asm__ __volatile__ (
        "dc cvau, %0\n"
        "dsb ish\n"
        "ic ivau, %0\n"
        "dsb ish\n"
        "isb\n"
        :
        : "r"(Breakpoint->Address)
        : "memory"
    );

    Breakpoint->State = KD_BREAKPOINT_IN_USE;

    KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

    DPRINT("Enabled breakpoint %lu\n", Handle);
    return TRUE;
}

/*
 * @brief Disable a breakpoint
 * @implemented
 */
BOOLEAN
NTAPI
KdDisableBreakpoint(
    IN ULONG Handle)
{
    KIRQL OldIrql;
    ULONG Index;
    PKD_BREAKPOINT Breakpoint;
    NTSTATUS Status;

    if (Handle == 0 || Handle > KD_BREAKPOINT_MAX)
        return FALSE;

    Index = Handle - 1;

    KeAcquireSpinLock(&KdBreakpointLock, &OldIrql);

    Breakpoint = &KdBreakpoints[Index];
    if (Breakpoint->State != KD_BREAKPOINT_IN_USE)
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        return FALSE;
    }

    /* Restore original instruction */
    Status = MmCopyToCaller(Breakpoint->Address,
                           &Breakpoint->OriginalInstruction,
                           sizeof(ULONG));
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        return FALSE;
    }

    /* Flush instruction cache */
    __asm__ __volatile__ (
        "dc cvau, %0\n"
        "dsb ish\n"
        "ic ivau, %0\n"
        "dsb ish\n"
        "isb\n"
        :
        : "r"(Breakpoint->Address)
        : "memory"
    );

    Breakpoint->State = KD_BREAKPOINT_SUSPENDED;

    KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

    DPRINT("Disabled breakpoint %lu\n", Handle);
    return TRUE;
}

/*
 * @brief Handle breakpoint exception
 * @implemented
 */
BOOLEAN
NTAPI
KdHandleBreakpointException(
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame)
{
    PVOID BreakpointAddress;
    ULONG i;
    PKD_BREAKPOINT Breakpoint = NULL;
    KIRQL OldIrql;

    /* Get breakpoint address (PC - 4 since PC advances after BRK) */
    BreakpointAddress = (PVOID)(TrapFrame->Pc - ARM64_BREAKPOINT_SIZE);

    DPRINT("Breakpoint exception at %p\n", BreakpointAddress);

    KeAcquireSpinLock(&KdBreakpointLock, &OldIrql);

    /* Find the breakpoint */
    for (i = 0; i < KD_BREAKPOINT_MAX; i++)
    {
        if (KdBreakpoints[i].State == KD_BREAKPOINT_IN_USE &&
            KdBreakpoints[i].Address == BreakpointAddress)
        {
            Breakpoint = &KdBreakpoints[i];
            break;
        }
    }

    if (!Breakpoint)
    {
        /* Check for hardcoded breakpoint (DbgBreakPoint) */
        ULONG Instruction;
        if (NT_SUCCESS(MmCopyFromCaller(&Instruction, BreakpointAddress, sizeof(ULONG))))
        {
            if (Instruction == ARM64_BREAKPOINT_VALUE ||
                Instruction == ARM64_KD_BREAKPOINT_VALUE)
            {
                KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

                /* Enter debugger */
                return KdEnterDebugger(TrapFrame, ExceptionFrame);
            }
        }

        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);
        DPRINT1("Unknown breakpoint at %p\n", BreakpointAddress);
        return FALSE;
    }

    /* Update hit count */
    Breakpoint->HitCount++;

    /* Check pass count */
    if (Breakpoint->PassCount > 0)
    {
        Breakpoint->PassCount--;
        KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

        /* Skip this breakpoint */
        TrapFrame->Pc = (ULONG64)BreakpointAddress;
        return TRUE;
    }

    /* Handle temporary breakpoint */
    if (Breakpoint->Temporary)
    {
        /* Restore original instruction */
        MmCopyToCaller(Breakpoint->Address,
                      &Breakpoint->OriginalInstruction,
                      sizeof(ULONG));

        /* Flush instruction cache */
        __asm__ __volatile__ (
            "dc cvau, %0\n"
            "dsb ish\n"
            "ic ivau, %0\n"
            "dsb ish\n"
            "isb\n"
            :
            : "r"(Breakpoint->Address)
            : "memory"
        );

        /* Free the breakpoint */
        RtlZeroMemory(Breakpoint, sizeof(KD_BREAKPOINT));
    }

    KeReleaseSpinLock(&KdBreakpointLock, OldIrql);

    /* Enter debugger */
    return KdEnterDebugger(TrapFrame, ExceptionFrame);
}

/*
 * @brief Single step support
 * @implemented
 */
BOOLEAN
NTAPI
KdSetSingleStep(
    IN PKTRAP_FRAME TrapFrame,
    IN BOOLEAN Enable)
{
    ULONG64 MdscrEl1;

    /* Read debug control register */
    MdscrEl1 = KdpReadDebugRegister(0);

    if (Enable)
    {
        /* Enable single step */
        MdscrEl1 |= MDSCR_SS_BIT;

        /* Also set PSTATE.SS in SPSR */
        TrapFrame->Spsr |= (1 << 21);  /* SS bit in SPSR */

        DPRINT("Single step enabled\n");
    }
    else
    {
        /* Disable single step */
        MdscrEl1 &= ~MDSCR_SS_BIT;

        /* Clear PSTATE.SS in SPSR */
        TrapFrame->Spsr &= ~(1 << 21);

        DPRINT("Single step disabled\n");
    }

    /* Write back debug control register */
    KdpWriteDebugRegister(0, MdscrEl1);

    return TRUE;
}

/*
 * @brief Break into debugger
 * @implemented
 */
VOID
NTAPI
DbgBreakPoint(VOID)
{
    /* Execute ARM64 breakpoint instruction */
    __asm__ __volatile__ (
        "brk #0\n"
        :
        :
        : "memory"
    );
}

/*
 * @brief Break into debugger with status
 * @implemented
 */
VOID
NTAPI
DbgBreakPointWithStatus(
    IN ULONG Status)
{
    /* Store status in X0 for debugger */
    __asm__ __volatile__ (
        "mov x0, %0\n"
        "brk #1\n"
        :
        : "r"((ULONG64)Status)
        : "x0", "memory"
    );
}

/*
 * @brief User-mode breakpoint service
 * @implemented
 */
VOID
NTAPI
DbgUserBreakPoint(VOID)
{
    /* Execute user breakpoint (different immediate) */
    __asm__ __volatile__ (
        "brk #3\n"
        :
        :
        : "memory"
    );
}

/*
 * @brief Enable/disable hardware breakpoint
 * @implemented
 */
BOOLEAN
NTAPI
KdSetHardwareBreakpoint(
    IN PVOID Address,
    IN ULONG Length,
    IN ULONG Type)
{
    ULONG i;

    /* Find a free hardware breakpoint slot */
    for (i = 0; i < KdNumberOfHwBreakpoints; i++)
    {
        if (!KdHwBreakpoints[i].Enabled)
        {
            return KdpSetHardwareBreakpoint(i, Address, Type, Length);
        }
    }

    DPRINT1("No free hardware breakpoints\n");
    return FALSE;
}

/* EOF */