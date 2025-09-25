/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Debug Support Functions
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* ARM64 Debug Architecture Version */
ULONG KeArm64DebugArchVersion = 0;

/* ARM64 Debug Capabilities */
ULONG KeArm64NumBreakpoints = 0;
ULONG KeArm64NumWatchpoints = 0;
ULONG KeArm64NumContextBreakpoints = 0;
BOOLEAN KeArm64DebugArchPresent = FALSE;

/* ARM64 Debug Register State */
typedef struct _ARM64_DEBUG_STATE {
    ULONG64 Dbgbcr[16];    /* Debug Breakpoint Control Registers */
    ULONG64 Dbgbvr[16];    /* Debug Breakpoint Value Registers */
    ULONG64 Dbgwcr[16];    /* Debug Watchpoint Control Registers */
    ULONG64 Dbgwvr[16];    /* Debug Watchpoint Value Registers */
    ULONG64 Mdscr;         /* Monitor Debug System Control Register */
    ULONG64 Mdccsr;        /* Monitor Debug Comms Channel Status Register */
} ARM64_DEBUG_STATE, *PARM64_DEBUG_STATE;

/* ARM64 Debug feature flags */
#define ARM64_DEBUG_ARCH_V6     0x1  /* ARMv6 debug */
#define ARM64_DEBUG_ARCH_V7     0x2  /* ARMv7 debug */
#define ARM64_DEBUG_ARCH_V7_1   0x3  /* ARMv7.1 debug */
#define ARM64_DEBUG_ARCH_V8     0x6  /* ARMv8 debug */
#define ARM64_DEBUG_ARCH_V8_VHE 0x7  /* ARMv8 debug with VHE */
#define ARM64_DEBUG_ARCH_V8_2   0x8  /* ARMv8.2 debug */
#define ARM64_DEBUG_ARCH_V8_4   0x9  /* ARMv8.4 debug */

/* ARM64 MDSCR_EL1 bits */
#define ARM64_MDSCR_SS      (1ULL << 0)  /* Software Step */
#define ARM64_MDSCR_ERR     (1ULL << 6)  /* Error flag */
#define ARM64_MDSCR_TDCC    (1ULL << 12) /* Trap Debug Comms Channel */
#define ARM64_MDSCR_KDE     (1ULL << 13) /* Kernel Debug Enable */
#define ARM64_MDSCR_HDE     (1ULL << 14) /* Halting Debug Enable */
#define ARM64_MDSCR_MDE     (1ULL << 15) /* Monitor Debug Enable */
#define ARM64_MDSCR_RAZ_WI  0xFFFFFFFFFFC0FFE0ULL /* RAZ/WI bits */

/* ARM64 Debug BCR/WCR Control Register bits */
#define ARM64_BCR_E         (1ULL << 0)  /* Breakpoint Enable */
#define ARM64_BCR_PMC       (3ULL << 1)  /* Privilege Mode Control */
#define ARM64_BCR_BAS       (15ULL << 5) /* Byte Address Select */
#define ARM64_BCR_HMC       (1ULL << 13) /* Higher Mode Control */
#define ARM64_BCR_SSC       (3ULL << 14) /* Security State Control */
#define ARM64_BCR_LBN       (15ULL << 16)/* Linked Breakpoint Number */
#define ARM64_BCR_BT        (15ULL << 20)/* Breakpoint Type */

#define ARM64_WCR_E         (1ULL << 0)  /* Watchpoint Enable */
#define ARM64_WCR_PAC       (3ULL << 1)  /* Privilege Access Control */
#define ARM64_WCR_LSC       (3ULL << 3)  /* Load/Store Access Control */
#define ARM64_WCR_BAS       (255ULL << 5)/* Byte Address Select */
#define ARM64_WCR_HMC       (1ULL << 13) /* Higher Mode Control */
#define ARM64_WCR_SSC       (3ULL << 14) /* Security State Control */
#define ARM64_WCR_LBN       (15ULL << 16)/* Linked Breakpoint Number */
#define ARM64_WCR_WT        (1ULL << 20) /* Watchpoint Type */
#define ARM64_WCR_MASK      (31ULL << 24)/* Address Mask */

/* FUNCTIONS ******************************************************************/

/**
 * @brief Read ARM64 Debug ID register
 * @param reg Register name
 * @return Register value
 */
static FORCEINLINE ULONG64
Arm64ReadDebugIdRegister(const char* reg)
{
    ULONG64 value;
#ifdef _M_ARM64
    if (strcmp(reg, "ID_AA64DFR0_EL1") == 0) {
        __asm__ volatile("mrs %0, id_aa64dfr0_el1" : "=r" (value));
    } else if (strcmp(reg, "ID_AA64DFR1_EL1") == 0) {
        __asm__ volatile("mrs %0, id_aa64dfr1_el1" : "=r" (value));
    } else if (strcmp(reg, "MDRAR_EL1") == 0) {
        __asm__ volatile("mrs %0, mdrar_el1" : "=r" (value));
    } else if (strcmp(reg, "OSLSR_EL1") == 0) {
        __asm__ volatile("mrs %0, oslsr_el1" : "=r" (value));
    } else if (strcmp(reg, "MDSCR_EL1") == 0) {
        __asm__ volatile("mrs %0, mdscr_el1" : "=r" (value));
    } else {
        value = 0;
    }
#else
    UNREFERENCED_PARAMETER(reg);
    value = 0;
#endif
    return value;
}

/**
 * @brief Write ARM64 Debug register
 * @param reg Register name
 * @param value Value to write
 */
static FORCEINLINE VOID
Arm64WriteDebugRegister(const char* reg, ULONG64 value)
{
#ifdef _M_ARM64
    if (strcmp(reg, "MDSCR_EL1") == 0) {
        __asm__ volatile("msr mdscr_el1, %0; isb" :: "r" (value));
    } else if (strcmp(reg, "OSLAR_EL1") == 0) {
        __asm__ volatile("msr oslar_el1, %0; isb" :: "r" (value));
    }
    /* Breakpoint and watchpoint registers would be handled separately */
#else
    UNREFERENCED_PARAMETER(reg);
    UNREFERENCED_PARAMETER(value);
#endif
}

/**
 * @brief Initialize ARM64 debug architecture
 */
VOID
NTAPI
KiInitializeDebugArchitecture(VOID)
{
    ULONG64 dfr0, dfr1;
    ULONG DebugVer;

    /* Read Debug Feature Register 0 */
    dfr0 = Arm64ReadDebugIdRegister("ID_AA64DFR0_EL1");
    dfr1 = Arm64ReadDebugIdRegister("ID_AA64DFR1_EL1");

    /* Extract debug architecture version (bits 3:0) */
    DebugVer = (ULONG)(dfr0 & 0xF);
    KeArm64DebugArchVersion = DebugVer;

    /* Check if debug architecture is present */
    if (DebugVer >= ARM64_DEBUG_ARCH_V6) {
        KeArm64DebugArchPresent = TRUE;

        /* Extract number of breakpoints (bits 15:12) */
        KeArm64NumBreakpoints = (ULONG)((dfr0 >> 12) & 0xF) + 1;

        /* Extract number of watchpoints (bits 23:20) */
        KeArm64NumWatchpoints = (ULONG)((dfr0 >> 20) & 0xF) + 1;

        /* Extract number of context-aware breakpoints (bits 31:28) */
        KeArm64NumContextBreakpoints = (ULONG)((dfr0 >> 28) & 0xF);

        DPRINT("ARM64: Debug Architecture v%u detected\n", DebugVer);
        DPRINT("ARM64: %u breakpoints, %u watchpoints, %u context breakpoints\n",
               KeArm64NumBreakpoints, KeArm64NumWatchpoints, KeArm64NumContextBreakpoints);
    } else {
        KeArm64DebugArchPresent = FALSE;
        DPRINT("ARM64: Debug architecture not present or unsupported\n");
        return;
    }

#ifdef _M_ARM64
    /* Initialize Debug System Control Register */
    ULONG64 mdscr = Arm64ReadDebugIdRegister("MDSCR_EL1");

    /* Clear software step and error flags */
    mdscr &= ~(ARM64_MDSCR_SS | ARM64_MDSCR_ERR);

    /* Enable Monitor Debug Events (MDE) for kernel debugging */
    mdscr |= ARM64_MDSCR_MDE;

    /* Disable kernel debug events initially (can be enabled by debugger) */
    mdscr &= ~ARM64_MDSCR_KDE;

    /* Write back MDSCR */
    Arm64WriteDebugRegister("MDSCR_EL1", mdscr);

    /* Unlock OS Lock if locked */
    ULONG64 oslsr = Arm64ReadDebugIdRegister("OSLSR_EL1");
    if (oslsr & (1ULL << 1)) { /* OS Lock is implemented and locked */
        Arm64WriteDebugRegister("OSLAR_EL1", 0); /* Unlock */
        DPRINT("ARM64: Debug OS Lock unlocked\n");
    }

    /* Initialize all breakpoint and watchpoint registers to disabled state */
    for (ULONG i = 0; i < min(KeArm64NumBreakpoints, 16); i++) {
        CHAR regname[20];
        sprintf(regname, "dbgbcr%u_el1", i);
        /* We would need individual handlers for each register */
        /* For now, assume they're disabled by default */
    }

    for (ULONG i = 0; i < min(KeArm64NumWatchpoints, 16); i++) {
        CHAR regname[20];
        sprintf(regname, "dbgwcr%u_el1", i);
        /* We would need individual handlers for each register */
        /* For now, assume they're disabled by default */
    }
#endif

    DPRINT("ARM64: Debug architecture initialized\n");
}

/**
 * @brief Set ARM64 hardware breakpoint
 * @param Index Breakpoint index (0-15)
 * @param Address Virtual address to break on
 * @param Enabled Whether breakpoint should be enabled
 * @return Success status
 */
NTSTATUS
NTAPI
KiSetHardwareBreakpoint(
    IN ULONG Index,
    IN PVOID Address,
    IN BOOLEAN Enabled)
{
    if (!KeArm64DebugArchPresent || Index >= KeArm64NumBreakpoints) {
        return STATUS_NOT_SUPPORTED;
    }

#ifdef _M_ARM64
    /* Set breakpoint value register */
    ULONG64 address = (ULONG64)Address;
    ULONG64 bcr = 0;

    if (Enabled) {
        /* Enable breakpoint with EL1 and EL0 access */
        bcr = ARM64_BCR_E |                    /* Enable */
              (0x3ULL << 1) |                  /* PMC: EL1 and EL0 */
              (0xFULL << 5) |                  /* BAS: All bytes */
              (0x0ULL << 20);                  /* BT: Address match */

        /* For instruction breakpoints on addresses */
        bcr |= (0x0ULL << 20); /* BT = 0 for address match breakpoints */
    }

    /* Write breakpoint registers using index-specific inline assembly */
    switch (Index) {
        case 0:
            if (Enabled) __asm__ volatile("msr dbgbvr0_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgbcr0_el1, %0; isb" :: "r" (bcr));
            break;
        case 1:
            if (Enabled) __asm__ volatile("msr dbgbvr1_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgbcr1_el1, %0; isb" :: "r" (bcr));
            break;
        case 2:
            if (Enabled) __asm__ volatile("msr dbgbvr2_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgbcr2_el1, %0; isb" :: "r" (bcr));
            break;
        case 3:
            if (Enabled) __asm__ volatile("msr dbgbvr3_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgbcr3_el1, %0; isb" :: "r" (bcr));
            break;
        /* Add more cases as needed up to the maximum supported */
        default:
            return STATUS_INVALID_PARAMETER;
    }
#else
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Enabled);
#endif

    return STATUS_SUCCESS;
}

/**
 * @brief Set ARM64 hardware watchpoint
 * @param Index Watchpoint index (0-15)
 * @param Address Virtual address to watch
 * @param Size Size of region to watch
 * @param AccessType Read/Write access type
 * @param Enabled Whether watchpoint should be enabled
 * @return Success status
 */
NTSTATUS
NTAPI
KiSetHardwareWatchpoint(
    IN ULONG Index,
    IN PVOID Address,
    IN ULONG Size,
    IN ULONG AccessType,
    IN BOOLEAN Enabled)
{
    if (!KeArm64DebugArchPresent || Index >= KeArm64NumWatchpoints) {
        return STATUS_NOT_SUPPORTED;
    }

#ifdef _M_ARM64
    ULONG64 address = (ULONG64)Address;
    ULONG64 wcr = 0;

    if (Enabled) {
        /* Calculate byte address select mask based on size and alignment */
        ULONG64 bas = 0;
        ULONG alignedSize = (Size + 7) & ~7; /* Round up to 8-byte boundary */

        if (alignedSize >= 8) {
            bas = 0xFF; /* All 8 bytes */
        } else {
            bas = (1ULL << Size) - 1; /* Size-appropriate mask */
        }

        /* Set up watchpoint control register */
        wcr = ARM64_WCR_E |                    /* Enable */
              (0x3ULL << 1) |                  /* PAC: EL1 and EL0 */
              ((ULONG64)AccessType << 3) |     /* LSC: Load/Store access */
              (bas << 5);                      /* BAS: Byte address select */
    }

    /* Write watchpoint registers using index-specific inline assembly */
    switch (Index) {
        case 0:
            if (Enabled) __asm__ volatile("msr dbgwvr0_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgwcr0_el1, %0; isb" :: "r" (wcr));
            break;
        case 1:
            if (Enabled) __asm__ volatile("msr dbgwvr1_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgwcr1_el1, %0; isb" :: "r" (wcr));
            break;
        case 2:
            if (Enabled) __asm__ volatile("msr dbgwvr2_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgwcr2_el1, %0; isb" :: "r" (wcr));
            break;
        case 3:
            if (Enabled) __asm__ volatile("msr dbgwvr3_el1, %0" :: "r" (address));
            __asm__ volatile("msr dbgwcr3_el1, %0; isb" :: "r" (wcr));
            break;
        /* Add more cases as needed */
        default:
            return STATUS_INVALID_PARAMETER;
    }
#else
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(AccessType);
    UNREFERENCED_PARAMETER(Enabled);
#endif

    return STATUS_SUCCESS;
}

/**
 * @brief Handle ARM64 debug exception
 * @param TrapFrame Trap frame from debug exception
 * @return Whether exception was handled
 */
BOOLEAN
NTAPI
KiHandleDebugException(
    IN PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);

    if (!KeArm64DebugArchPresent) {
        return FALSE;
    }

#ifdef _M_ARM64
    /* Read debug status to determine cause */
    ULONG64 mdscr = Arm64ReadDebugIdRegister("MDSCR_EL1");

    /* Check if this was a software step */
    if (mdscr & ARM64_MDSCR_SS) {
        /* Clear software step flag */
        mdscr &= ~ARM64_MDSCR_SS;
        Arm64WriteDebugRegister("MDSCR_EL1", mdscr);

        /* Handle single step debugging */
        DPRINT("ARM64: Software step debug exception\n");
        return TRUE;
    }

    /* Check debug error flag */
    if (mdscr & ARM64_MDSCR_ERR) {
        DPRINT("ARM64: Debug error exception\n");
        /* Clear error flag */
        mdscr &= ~ARM64_MDSCR_ERR;
        Arm64WriteDebugRegister("MDSCR_EL1", mdscr);
        return TRUE;
    }

    /* Check for breakpoint or watchpoint hits */
    /* This would require reading DBGBCR/DBGWCR registers to find which fired */
    /* For now, assume it's a breakpoint */
    DPRINT("ARM64: Hardware breakpoint/watchpoint exception\n");
    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * @brief Enable ARM64 single step debugging
 */
VOID
NTAPI
KiEnableSingleStep(VOID)
{
    if (!KeArm64DebugArchPresent) {
        return;
    }

#ifdef _M_ARM64
    ULONG64 mdscr = Arm64ReadDebugIdRegister("MDSCR_EL1");

    /* Set software step flag */
    mdscr |= ARM64_MDSCR_SS;

    Arm64WriteDebugRegister("MDSCR_EL1", mdscr);
    DPRINT("ARM64: Single step enabled\n");
#endif
}

/**
 * @brief Disable ARM64 single step debugging
 */
VOID
NTAPI
KiDisableSingleStep(VOID)
{
    if (!KeArm64DebugArchPresent) {
        return;
    }

#ifdef _M_ARM64
    ULONG64 mdscr = Arm64ReadDebugIdRegister("MDSCR_EL1");

    /* Clear software step flag */
    mdscr &= ~ARM64_MDSCR_SS;

    Arm64WriteDebugRegister("MDSCR_EL1", mdscr);
    DPRINT("ARM64: Single step disabled\n");
#endif
}

/* EARLY DEBUG OUTPUT FUNCTIONS ********************************************/

/*
 * @brief Output a string to ARM64 serial port for early debugging
 *
 * This function provides early boot debug output before the full
 * kernel debugger is initialized.
 */
VOID
Arm64SerialPutString(
    _In_ PCHAR String)
{
    volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

    if (!String) return;

    while (*String)
    {
        /* Write character to PL011 UART data register */
        *Pl011Dr = (UCHAR)(*String);

        /* Simple delay for UART transmission */
        for (volatile ULONG Delay = 0; Delay < 1000; Delay++)
        {
            __asm__ __volatile__("nop");
        }

        String++;
    }
}

/*
 * @brief Output a single character to ARM64 serial port
 */
VOID
Arm64SerialPutChar(
    _In_ CHAR Character)
{
    volatile ULONG *const Pl011Dr = (volatile ULONG *)0x09000000;

    /* Write character to PL011 UART data register */
    *Pl011Dr = (UCHAR)Character;

    /* Simple delay for UART transmission */
    for (volatile ULONG Delay = 0; Delay < 1000; Delay++)
    {
        __asm__ __volatile__("nop");
    }
}

/*
 * @brief Output a hex value to ARM64 serial port
 */
VOID
Arm64SerialPutHex64(
    _In_ ULONGLONG Value)
{
    CHAR HexBuffer[20];
    INT Pos = 0;
    INT Shift;

    /* Build hex string */
    HexBuffer[Pos++] = '0';
    HexBuffer[Pos++] = 'x';

    for (Shift = 60; Shift >= 0; Shift -= 4)
    {
        UCHAR Nibble = (UCHAR)((Value >> Shift) & 0xF);
        HexBuffer[Pos++] = (Nibble < 10) ? ('0' + Nibble) : ('A' + Nibble - 10);
    }

    HexBuffer[Pos] = '\0';

    /* Output the hex string */
    Arm64SerialPutString(HexBuffer);
}

/*
 * @brief Output a carriage return and line feed
 */
VOID
Arm64SerialCrLf(VOID)
{
    Arm64SerialPutChar('\r');
    Arm64SerialPutChar('\n');
}

/*
 * @brief ARM64 Debug initialization function
 * Should be called early during kernel initialization
 */
VOID
NTAPI
KiInitializeDebug(VOID)
{
    /* Initialize debug architecture detection */
    KiInitializeDebugArchitecture();

    /* Additional debug subsystem initialization can go here */
    DPRINT("ARM64: Debug subsystem initialized\n");
}