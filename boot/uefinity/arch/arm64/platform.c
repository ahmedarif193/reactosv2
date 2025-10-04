/*
 * Uefinity - ARM64 Architecture Interface wiring (UEFI-only)
 */

#include <freeldr.h>
#include <uefildr.h>
#include <arch/arch_interface.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

static EFI_STATUS Arm64ArchInit(VOID)
{
    TRACE("Arm64ArchInit()\n");
    return EFI_SUCCESS;
}

static const CHAR*
Arm64ArchGetSystemFamilyString(VOID)
{
    return "ARM processor family";
}

/*
 * For now, rely on common generic ACPI presence detection.
 * If you want ARM64-specific ACPI resource publication, implement:
 * static VOID Arm64ArchDetectAcpi(PCONFIGURATION_COMPONENT_DATA, ULONG*)
 * and wire it below.
 */

ARCH_INTERFACE ArchInterface =
{
    .ArchInit = Arm64ArchInit,
    .ArchDetectCPU = NULL,
    .ArchEnableInterrupts = NULL,
    .ArchDisableInterrupts = NULL,
    .ArchSetupMMU = NULL,
    .ArchEnablePaging = NULL,
    .ArchInvalidateTLB = NULL,
    .ArchFlushCache = NULL,
    .ArchSetupExceptions = NULL,
    .ArchHandleException = NULL,
    .ArchInitializeTimer = NULL,
    .ArchGetTimerFrequency = NULL,
    .ArchGetTimerValue = NULL,
    .ArchStallExecution = NULL,
    .ArchPrepareForKernel = NULL,
    .ArchJumpToKernel = NULL,
    .ArchName = (const CHAR8 *)"ARM64",
    .ArchFeatures = 0,
    .ArchCacheLineSize = 64,
    .RequiresIdentityMapping = TRUE,
    .ArchGetSystemFamilyString = Arm64ArchGetSystemFamilyString,
    .ArchDetectAcpi = NULL,
};

