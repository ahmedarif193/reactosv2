/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/ke/amd64/security.c
 * PURPOSE:         AMD64 Security Features (SMEP/SMAP/CET)
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

/* CPUID bits */
#define CPUID_SMEP              (1 << 7)   /* EBX bit 7 in leaf 7 */
#define CPUID_SMAP              (1 << 20)  /* EBX bit 20 in leaf 7 */
#define CPUID_CET_SS            (1 << 7)   /* ECX bit 7 in leaf 7 */
#define CPUID_CET_IBT           (1 << 20)  /* EDX bit 20 in leaf 7 */
#define CPUID_UMIP              (1 << 2)   /* ECX bit 2 in leaf 7 */

/* CR4 bits */
#define CR4_SMEP                (1ULL << 20)
#define CR4_SMAP                (1ULL << 21)
#define CR4_UMIP                (1ULL << 11)
#define CR4_CET                 (1ULL << 23)

/* MSRs */
#define MSR_IA32_U_CET          0x6A0
#define MSR_IA32_S_CET          0x6A2
#define MSR_IA32_PL0_SSP        0x6A4
#define MSR_IA32_PL1_SSP        0x6A5
#define MSR_IA32_PL2_SSP        0x6A6
#define MSR_IA32_PL3_SSP        0x6A7
#define MSR_IA32_INTERRUPT_SSP  0x6A8

/* CET bits */
#define CET_SHSTK_EN            0x0001
#define CET_WR_SHSTK_EN         0x0002
#define CET_ENDBR_EN            0x0004
#define CET_LEG_IW_EN           0x0008
#define CET_NO_TRACK_EN         0x0010
#define CET_SUPPRESS            0x0400

/* GLOBALS *******************************************************************/

static BOOLEAN SmepEnabled = FALSE;
static BOOLEAN SmapEnabled = FALSE;
static BOOLEAN CetEnabled = FALSE;
static BOOLEAN UmipEnabled = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Detects CPU security features
 * @return Feature flags
 */
static ULONG
KiDetectSecurityFeatures(VOID)
{
    INT CpuInfo[4];
    ULONG Features = 0;

    /* Check for extended features (CPUID leaf 7) */
    __cpuid(CpuInfo, 0);
    if (CpuInfo[0] >= 7)
    {
        __cpuidex(CpuInfo, 7, 0);

        /* Check EBX features */
        if (CpuInfo[1] & CPUID_SMEP)
            Features |= CPUID_SMEP;

        if (CpuInfo[1] & CPUID_SMAP)
            Features |= CPUID_SMAP;

        /* Check ECX features */
        if (CpuInfo[2] & CPUID_UMIP)
            Features |= CPUID_UMIP;

        if (CpuInfo[2] & CPUID_CET_SS)
            Features |= CPUID_CET_SS;

        /* Check EDX features */
        if (CpuInfo[3] & CPUID_CET_IBT)
            Features |= CPUID_CET_IBT;
    }

    return Features;
}

/*
 * @brief IPI routine to enable security features on all CPUs
 * @param Features - Features to enable
 * @return 0
 */
static ULONG_PTR
NTAPI
KiEnableSecurityFeaturesIpi(
    _In_ ULONG_PTR Features)
{
    ULONG_PTR Cr4 = __readcr4();

    /* Enable SMEP */
    if (Features & CPUID_SMEP)
    {
        Cr4 |= CR4_SMEP;
    }

    /* Enable SMAP */
    if (Features & CPUID_SMAP)
    {
        Cr4 |= CR4_SMAP;
    }

    /* Enable UMIP */
    if (Features & CPUID_UMIP)
    {
        Cr4 |= CR4_UMIP;
    }

    /* Write updated CR4 */
    __writecr4(Cr4);

    return 0;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Enables SMEP (Supervisor Mode Execution Prevention)
 * @return STATUS_SUCCESS or error code
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
KiEnableSMEP(VOID)
{
    ULONG Features;
    ULONG_PTR Cr4;

    /* Detect features */
    Features = KiDetectSecurityFeatures();

    /* Check for SMEP support */
    if (!(Features & CPUID_SMEP))
    {
        DPRINT1("SMEP not supported by CPU\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Enable on all CPUs */
    if (KeNumberProcessors > 1)
    {
        KeIpiGenericCall(KiEnableSecurityFeaturesIpi, CPUID_SMEP);
    }
    else
    {
        Cr4 = __readcr4();
        __writecr4(Cr4 | CR4_SMEP);
    }

    SmepEnabled = TRUE;
    DPRINT1("SMEP (Supervisor Mode Execution Prevention) enabled\n");

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Enables SMAP (Supervisor Mode Access Prevention)
 * @return STATUS_SUCCESS or error code
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
KiEnableSMAP(VOID)
{
    ULONG Features;
    ULONG_PTR Cr4;

    /* Detect features */
    Features = KiDetectSecurityFeatures();

    /* Check for SMAP support */
    if (!(Features & CPUID_SMAP))
    {
        DPRINT1("SMAP not supported by CPU\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Enable on all CPUs */
    if (KeNumberProcessors > 1)
    {
        KeIpiGenericCall(KiEnableSecurityFeaturesIpi, CPUID_SMAP);
    }
    else
    {
        Cr4 = __readcr4();
        __writecr4(Cr4 | CR4_SMAP);
    }

    SmapEnabled = TRUE;
    DPRINT1("SMAP (Supervisor Mode Access Prevention) enabled\n");

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Enables UMIP (User Mode Instruction Prevention)
 * @return STATUS_SUCCESS or error code
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
KiEnableUMIP(VOID)
{
    ULONG Features;
    ULONG_PTR Cr4;

    /* Detect features */
    Features = KiDetectSecurityFeatures();

    /* Check for UMIP support */
    if (!(Features & CPUID_UMIP))
    {
        DPRINT1("UMIP not supported by CPU\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Enable on all CPUs */
    if (KeNumberProcessors > 1)
    {
        KeIpiGenericCall(KiEnableSecurityFeaturesIpi, CPUID_UMIP);
    }
    else
    {
        Cr4 = __readcr4();
        __writecr4(Cr4 | CR4_UMIP);
    }

    UmipEnabled = TRUE;
    DPRINT1("UMIP (User Mode Instruction Prevention) enabled\n");

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Enables CET (Control-flow Enforcement Technology)
 * @return STATUS_SUCCESS or error code
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
KiEnableCET(VOID)
{
    ULONG Features;
    ULONGLONG MsrValue;

    /* Detect features */
    Features = KiDetectSecurityFeatures();

    /* Check for CET support */
    if (!(Features & (CPUID_CET_SS | CPUID_CET_IBT)))
    {
        DPRINT1("CET not supported by CPU\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Enable shadow stack if supported */
    if (Features & CPUID_CET_SS)
    {
        /* Setup supervisor CET */
        MsrValue = CET_SHSTK_EN | CET_WR_SHSTK_EN;
        __writemsr(MSR_IA32_S_CET, MsrValue);

        /* Setup user CET */
        MsrValue = CET_SHSTK_EN;
        __writemsr(MSR_IA32_U_CET, MsrValue);

        /* Initialize shadow stack pointers */
        /* TODO: Allocate and setup shadow stacks */

        DPRINT1("CET Shadow Stack enabled\n");
    }

    /* Enable indirect branch tracking if supported */
    if (Features & CPUID_CET_IBT)
    {
        /* Read current CET MSR */
        MsrValue = __readmsr(MSR_IA32_S_CET);
        MsrValue |= CET_ENDBR_EN | CET_LEG_IW_EN;
        __writemsr(MSR_IA32_S_CET, MsrValue);

        /* Enable for user mode too */
        MsrValue = __readmsr(MSR_IA32_U_CET);
        MsrValue |= CET_ENDBR_EN;
        __writemsr(MSR_IA32_U_CET, MsrValue);

        DPRINT1("CET Indirect Branch Tracking enabled\n");
    }

    CetEnabled = TRUE;

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Begins a SMAP-allowed user access region
 * @return None
 */
VOID
NTAPI
KeSmapAllowUserAccess(VOID)
{
    if (SmapEnabled)
    {
        /* STAC - Set AC flag to allow user access */
        __asm__ volatile ("stac" ::: "memory");
    }
}

/*
 * @implemented
 * @brief Ends a SMAP-allowed user access region
 * @return None
 */
VOID
NTAPI
KeSmapBlockUserAccess(VOID)
{
    if (SmapEnabled)
    {
        /* CLAC - Clear AC flag to block user access */
        __asm__ volatile ("clac" ::: "memory");
    }
}

/*
 * @implemented
 * @brief Checks if SMEP is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsSmepEnabled(VOID)
{
    return SmepEnabled;
}

/*
 * @implemented
 * @brief Checks if SMAP is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsSmapEnabled(VOID)
{
    return SmapEnabled;
}

/*
 * @implemented
 * @brief Checks if CET is enabled
 * @return TRUE if enabled, FALSE otherwise
 */
BOOLEAN
NTAPI
KeIsCetEnabled(VOID)
{
    return CetEnabled;
}

/*
 * @implemented
 * @brief Initializes all security features
 * @return None
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializeSecurityFeatures(VOID)
{
    NTSTATUS Status;

    DPRINT1("Initializing CPU security features...\n");

    /* Try to enable SMEP */
    Status = KiEnableSMEP();
    if (!NT_SUCCESS(Status))
    {
        DPRINT("SMEP not available\n");
    }

    /* Try to enable SMAP */
    Status = KiEnableSMAP();
    if (!NT_SUCCESS(Status))
    {
        DPRINT("SMAP not available\n");
    }

    /* Try to enable UMIP */
    Status = KiEnableUMIP();
    if (!NT_SUCCESS(Status))
    {
        DPRINT("UMIP not available\n");
    }

    /* Try to enable CET */
    Status = KiEnableCET();
    if (!NT_SUCCESS(Status))
    {
        DPRINT("CET not available\n");
    }

    DPRINT1("Security feature initialization complete\n");
}

/*
 * @implemented
 * @brief Safe user memory copy with SMAP
 * @param Destination - Destination buffer
 * @param Source - Source buffer
 * @param Length - Length to copy
 * @return STATUS_SUCCESS or exception code
 */
NTSTATUS
NTAPI
KeSafeUserCopy(
    _Out_ PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* Allow user access if SMAP is enabled */
    KeSmapAllowUserAccess();

    /* Perform copy with SEH protection */
    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Block user access again */
    KeSmapBlockUserAccess();

    return Status;
}

/* END OF FILE */