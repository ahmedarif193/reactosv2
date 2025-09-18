/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 exception handling with stack backtrace
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#include <uefildr.h>
#include <debug.h>

/* Default to WARNING channel for trap diagnostics */
DBG_DEFAULT_CHANNEL(WARNING);

/* ARM64 Exception Syndrome Register (ESR) fields */
#define ESR_EC_SHIFT    26
#define ESR_EC_MASK     0x3F
#define ESR_IL          (1U << 25)
#define ESR_ISS_MASK    0x1FFFFFF

/* Exception Class (EC) values */
#define ESR_EC_UNKNOWN      0x00
#define ESR_EC_WFI_WFE     0x01
#define ESR_EC_CP15_32     0x03
#define ESR_EC_CP15_64     0x04
#define ESR_EC_CP14_32     0x05
#define ESR_EC_CP14_LC     0x06
#define ESR_EC_FP_ACCESS   0x07
#define ESR_EC_PC_ALIGN    0x22
#define ESR_EC_SP_ALIGN    0x26
#define ESR_EC_SVC         0x15
#define ESR_EC_HVC         0x16
#define ESR_EC_SMC         0x17
#define ESR_EC_IABT_LOW    0x20
#define ESR_EC_IABT_CUR    0x21
#define ESR_EC_DABT_LOW    0x24
#define ESR_EC_DABT_CUR    0x25
#define ESR_EC_BRK         0x3C

/* ARM64 exception vector table structure */
typedef struct _ARM64_VECTOR_TABLE {
    /* Each entry is 128 bytes (32 instructions) aligned */
    UCHAR SyncSP0[0x80];      /* Synchronous exception from current EL with SP0 */
    UCHAR IrqSP0[0x80];       /* IRQ from current EL with SP0 */
    UCHAR FiqSP0[0x80];       /* FIQ from current EL with SP0 */
    UCHAR ErrorSP0[0x80];     /* System Error from current EL with SP0 */

    UCHAR SyncSPx[0x80];      /* Synchronous exception from current EL with SPx */
    UCHAR IrqSPx[0x80];       /* IRQ from current EL with SPx */
    UCHAR FiqSPx[0x80];       /* FIQ from current EL with SPx */
    UCHAR ErrorSPx[0x80];     /* System Error from current EL with SPx */

    UCHAR SyncLower64[0x80];  /* Synchronous exception from lower EL (AArch64) */
    UCHAR IrqLower64[0x80];   /* IRQ from lower EL (AArch64) */
    UCHAR FiqLower64[0x80];   /* FIQ from lower EL (AArch64) */
    UCHAR ErrorLower64[0x80]; /* System Error from lower EL (AArch64) */

    UCHAR SyncLower32[0x80];  /* Synchronous exception from lower EL (AArch32) */
    UCHAR IrqLower32[0x80];   /* IRQ from lower EL (AArch32) */
    UCHAR FiqLower32[0x80];   /* FIQ from lower EL (AArch32) */
    UCHAR ErrorLower32[0x80]; /* System Error from lower EL (AArch32) */
} ARM64_VECTOR_TABLE;

/* Exception handler prototypes from assembly */
extern VOID Arm64ExcSync(VOID);
extern VOID Arm64ExcIrq(VOID);
extern VOID Arm64ExcFiq(VOID);
extern VOID Arm64ExcError(VOID);

/* ARM64 trap frame structure matching our stub save order */
typedef struct _ARM64_TRAP_CONTEXT {
    /* General purpose registers */
    ULONGLONG X0, X1, X2, X3, X4, X5, X6, X7;
    ULONGLONG X8, X9, X10, X11, X12, X13, X14, X15;
    ULONGLONG X16, X17, X18, X19, X20, X21, X22, X23;
    ULONGLONG X24, X25, X26, X27, X28, X29, X30; /* X30 = LR */

    /* Special registers */
    ULONGLONG SP_EL0;     /* User stack pointer */
    ULONGLONG ELR_EL1;    /* Exception Link Register (return address) */
    ULONGLONG SPSR_EL1;   /* Saved Program Status Register */
    ULONGLONG ESR_EL1;    /* Exception Syndrome Register */
    ULONGLONG FAR_EL1;    /* Fault Address Register */
} ARM64_TRAP_CONTEXT, *PARM64_TRAP_CONTEXT;

/* Vector table aligned to 2KB boundary */
__attribute__((aligned(0x800)))
static ARM64_VECTOR_TABLE Arm64VectorTable;

static const CHAR* GetExceptionClassName(ULONG EC)
{
    switch (EC) {
        case ESR_EC_UNKNOWN:    return "Unknown";
        case ESR_EC_WFI_WFE:   return "WFI/WFE";
        case ESR_EC_CP15_32:    return "CP15 32-bit";
        case ESR_EC_CP15_64:    return "CP15 64-bit";
        case ESR_EC_CP14_32:    return "CP14 32-bit";
        case ESR_EC_FP_ACCESS:  return "FP/SIMD access";
        case ESR_EC_PC_ALIGN:   return "PC alignment fault";
        case ESR_EC_SP_ALIGN:   return "SP alignment fault";
        case ESR_EC_SVC:        return "SVC instruction";
        case ESR_EC_HVC:        return "HVC instruction";
        case ESR_EC_SMC:        return "SMC instruction";
        case ESR_EC_IABT_LOW:   return "Instruction abort (lower EL)";
        case ESR_EC_IABT_CUR:   return "Instruction abort (current EL)";
        case ESR_EC_DABT_LOW:   return "Data abort (lower EL)";
        case ESR_EC_DABT_CUR:   return "Data abort (current EL)";
        case ESR_EC_BRK:        return "BRK instruction";
        default:                return "Reserved/Unknown";
    }
}

VOID Arm64HandleException(PARM64_TRAP_CONTEXT Ctx)
{
    ULONG EC;
    ULONG ISS;

    /* Extract exception class from ESR */
    EC = (Ctx->ESR_EL1 >> ESR_EC_SHIFT) & ESR_EC_MASK;
    ISS = Ctx->ESR_EL1 & ESR_ISS_MASK;

    ERR("\n===============================================================\n");
    ERR("ARM64 Exception: %s (EC=0x%x)\n", GetExceptionClassName(EC), EC);
    ERR("ESR=0x%llx ISS=0x%x IL=%d\n",
        Ctx->ESR_EL1, ISS, (Ctx->ESR_EL1 & ESR_IL) ? 32 : 16);

    /* For data/instruction aborts, show fault address */
    if (EC == ESR_EC_IABT_LOW || EC == ESR_EC_IABT_CUR ||
        EC == ESR_EC_DABT_LOW || EC == ESR_EC_DABT_CUR) {
        ERR("Fault Address: 0x%llx\n", Ctx->FAR_EL1);
    }

    ERR("PC=0x%llx LR=0x%llx SP=0x%llx\n",
        Ctx->ELR_EL1, Ctx->X30, Ctx->X29);
    ERR("SPSR=0x%llx\n", Ctx->SPSR_EL1);

#ifdef UEFIBOOT
    /* Perform stack backtrace if possible */
    {
        extern VOID UefiArm64PrintBacktrace(ULONG_PTR Fp, ULONG_PTR StackTop, ULONG_PTR StackBottom);
        ULONG_PTR StackTop = (Ctx->X29 + 0xFFFF) & ~0xFFFFULL;
        ULONG_PTR StackBottom = StackTop - 0x10000ULL;
        UefiArm64PrintBacktrace(Ctx->X29, StackTop, StackBottom);
    }
#endif

    ERR("===============================================================\n");
}

VOID Arm64InitializeExceptions(VOID)
{
    ULONG_PTR Vbar;

    /* Initialize vector table entries */
    /* Each vector needs to be filled with branch to handler code */
    /* For now, we'll set up basic handlers that capture context */

    /* TODO: Fill vector table with proper exception entry code */
    /* Each entry should:
     * 1. Save all registers to stack
     * 2. Call Arm64HandleException with context
     * 3. Either return or halt depending on exception type
     */

    /* Set VBAR_EL1 to point to our vector table */
    Vbar = (ULONG_PTR)&Arm64VectorTable;
    __asm__ __volatile__("msr VBAR_EL1, %0" : : "r"(Vbar));

    /* Ensure the write completes */
    __asm__ __volatile__("isb");

    TRACE("ARM64: Exception handlers installed at 0x%lx\n", Vbar);
}

/*
 * Helper function to check if exceptions can be safely initialized
 * Under UEFI, we might want to skip this to avoid conflicts
 */
BOOLEAN Arm64CanInitializeExceptions(VOID)
{
    ULONG_PTR CurrentEL;

    /* Read current exception level */
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(CurrentEL));
    CurrentEL = (CurrentEL >> 2) & 0x3;

    /* We can only set VBAR_EL1 if we're at EL1 or higher */
    if (CurrentEL >= 1) {
        return TRUE;
    }

    TRACE("ARM64: Cannot initialize exceptions at EL%lu\n", CurrentEL);
    return FALSE;
}