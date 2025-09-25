/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 HAL Header
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

#ifndef _HAL_H_
#define _HAL_H_

/* INCLUDES *******************************************************************/

/* C Headers */
#define DbgPrint DbgPrintEarly
#include <stdio.h>

/* WDK HAL Compilation hack */
#include <excpt.h>
#include <ntdef.h>
#undef NTSYSAPI
#define NTSYSAPI __declspec(dllimport)

/* IFS/DDK/NDK Headers */
#include <ntifs.h>
#include <ioaccess.h>
#include <bugcodes.h>
#include <ntdddisk.h>
#include <arc/arc.h>
#include <iotypes.h>
#include <kefuncs.h>
#include <intrin.h>
#include <halfuncs.h>
#include <iofuncs.h>
#include <obfuncs.h>
#include <mmfuncs.h>
#include <rtlfuncs.h>
#include <windbgkd.h>
#include <arc/arc.h>

/* NDK Headers */
#include <ndk/asm.h>
#include <ndk/halfuncs.h>
#include <ndk/inbvfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/kdfuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>

/* ARM64-specific HAL Headers */
#include "halarm64.h"

/* DEFINITIONS ****************************************************************/

#undef DbgPrint

/* HAL Debugging */
ULONG DbgPrint(PCH Format, ...);
#if DBG
#define DPRINT1(...) DbgPrint(__VA_ARGS__)
#define DPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define DPRINT1(...)
#define DPRINT(...)
#endif

/* Missing constant */
#ifndef MAXULONG64
#define MAXULONG64  0xFFFFFFFFFFFFFFFFULL
#endif

/* HAL Memory Functions */
#define HalpAllocateMemory(Size) ExAllocatePoolWithTag(NonPagedPool, Size, 'laHE')
#define HalpFreeMemory(Pointer) ExFreePoolWithTag(Pointer, 'laHE')

/* ARM64 HAL Constants */
#define HAL_PLATFORM_ACPI_TABLES_PRESENT    0x00000001
#define HAL_PLATFORM_PCIE_SUPPORT          0x00000002
#define HAL_PLATFORM_MSI_SUPPORT           0x00000004
#define HAL_PLATFORM_GICv3_SUPPORT         0x00000008

/* Machine Types for ARM64 */
#define MACHINE_TYPE_ISA        0x0000
#define MACHINE_TYPE_EISA       0x0001
#define MACHINE_TYPE_MCA        0x0002
#define MACHINE_TYPE_ACPI       0x0003

/* Processor Architecture for ARM64 */
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64   12
#endif

/* ARM64 System Registers */
#define ARM64_SCTLR_EL1_MMU_ENABLE         (1UL << 0)
#define ARM64_SCTLR_EL1_ALIGNMENT_CHECK    (1UL << 1)
#define ARM64_SCTLR_EL1_DATA_CACHE         (1UL << 2)
#define ARM64_SCTLR_EL1_INSTR_CACHE        (1UL << 12)

/* STRUCTURES *****************************************************************/

/* Processor Speed Information */
typedef struct _PROCESSOR_SPEED_INFORMATION
{
    ULONG ProcessorSpeed;
} PROCESSOR_SPEED_INFORMATION, *PPROCESSOR_SPEED_INFORMATION;

/* EXTERNAL VARIABLES *********************************************************/

extern ULONG HalBusType;
extern BOOLEAN HalInitializationStarted;
extern ULONG HalDisableFirmwareMapper;

/* FUNCTION PROTOTYPES ********************************************************/

/* HAL Initialization Functions */
BOOLEAN
NTAPI
HalInitSystem(
    IN ULONG BootPhase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
HalProcessorIdle(VOID);

/* These are HAL functions, not dispatched */
#undef HalHaltSystem
#undef HalQuerySystemInformation
#undef HalSetSystemInformation

VOID
NTAPI
HalHaltSystem(VOID);

VOID
NTAPI
HalEnableTimerInterrupt(VOID);

VOID
NTAPI
HalInitializeDebugSupport(VOID);

/* System Information Functions */
NTSTATUS
NTAPI
HalQuerySystemInformation(
    IN HAL_QUERY_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    OUT PVOID Buffer,
    OUT PULONG ReturnedLength);

NTSTATUS
NTAPI
HalSetSystemInformation(
    IN HAL_SET_INFORMATION_CLASS InformationClass,
    IN ULONG BufferSize,
    IN PVOID Buffer);

BOOLEAN
NTAPI
HalQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceCount,
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL);

/* Debug Support Functions */
VOID
NTAPI
HalDisplayString(
    IN PCH String);

VOID
NTAPI
HalQueryDisplayParameters(
    OUT PULONG DispSizeX,
    OUT PULONG DispSizeY,
    OUT PULONG CursorPosX,
    OUT PULONG CursorPosY);

VOID
NTAPI
HalSetDisplayParameters(
    IN ULONG CursorPosX,
    IN ULONG CursorPosY);

/* Internal HAL Function Declarations */
VOID
NTAPI
HalInitializeSystemInformation(VOID);

VOID
NTAPI
HalDetectProcessorFeatures(VOID);

BOOLEAN
NTAPI
HalInitializeInterruptController(VOID);

BOOLEAN
NTAPI
HalInitializeTimer(VOID);

VOID
NTAPI
HalInitializeCacheManager(VOID);

VOID
NTAPI
HalStallExecution(
    IN ULONG Microseconds);

#endif /* _HAL_H_ */