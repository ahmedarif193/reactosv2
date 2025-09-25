/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS HAL (ARM64)
 * PURPOSE:         Minimal stub implementations for required HAL exports
 * FILE:            hal/halarm64/stubs.c
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <bugcodes.h>
#include <ndk/haltypes.h>
#include <ndk/ketypes.h>
#include <ndk/arm64/ketypes.h>
#define NDEBUG
#include <debug.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock
#undef KeRaiseIrqlToDpcLevel
#undef KeRaiseIrqlToSynchLevel

/* Simple forward declarations for helpers provided in halarm64.c */
extern KIRQL FASTCALL KfRaiseIrql(IN KIRQL NewIrql);
extern VOID FASTCALL KfLowerIrql(IN KIRQL NewIrql);

/* Prototype used by HalAcquireDisplayOwnership */
typedef BOOLEAN (NTAPI *PHAL_RESET_DISPLAY_PARAMETERS)(ULONG Columns, ULONG Rows);

/* HAL-wide globals */
PUCHAR KdComPortInUse = NULL;

static ULONG HalpTimeIncrement = 100000;
static ULONG_PTR HalpProfileInterval = 0;
static ULONGLONG HalpPerformanceFrequency = 19200000ULL; /* 19.2 MHz default */

static __inline ULONGLONG
HalpReadVirtualCounter(VOID)
{
    ULONGLONG Value = 0;
#if defined(_M_ARM64)
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Value));
#endif
    return Value;
}

static __inline VOID
HalpDataBarrier(VOID)
{
#if defined(_M_ARM64)
    __asm__ volatile("dmb ish" ::: "memory");
#endif
}

VOID
NTAPI
HalAcquireDisplayOwnership(IN PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    UNREFERENCED_PARAMETER(ResetDisplayParameters);
}

NTSTATUS
NTAPI
HalAdjustResourceList(IN OUT PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

/* HalAllocateAdapterChannel removed - implemented in generic/dma.c */

/* HalAllocateCommonBuffer removed - implemented in generic/dma.c */

PVOID
NTAPI
HalAllocateCrashDumpRegisters(IN PADAPTER_OBJECT AdapterObject,
                              IN OUT PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);

    if (NumberOfMapRegisters)
    {
        *NumberOfMapRegisters = 0;
    }

    return NULL;
}

NTSTATUS
NTAPI
HalAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                       IN PUNICODE_STRING DriverClassName,
                       IN PDRIVER_OBJECT DriverObject,
                       IN PDEVICE_OBJECT DeviceObject,
                       IN INTERFACE_TYPE BusType,
                       IN ULONG BusNumber,
                       IN ULONG SlotNumber,
                       IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(AllocatedResources);

    DPRINT1("HalAssignSlotResources stub invoked\n");
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
HalCalibratePerformanceCounter(IN volatile PLONG Count,
                               IN ULONGLONG NewCount)
{
    if (Count)
    {
        *Count = (LONG)NewCount;
    }
}

VOID
FASTCALL
HalClearSoftwareInterrupt(IN KIRQL Request)
{
    UNREFERENCED_PARAMETER(Request);
}

VOID
NTAPI
HalDisableSystemInterrupt(IN ULONG Vector,
                          IN KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Irql);
}

VOID
NTAPI
HalDisplayString(IN PCH String)
{
    if (String != NULL)
    {
        DbgPrint("%s", String);
    }
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(IN ULONG Vector,
                         IN KIRQL Irql,
                         IN KINTERRUPT_MODE InterruptMode)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Irql);
    UNREFERENCED_PARAMETER(InterruptMode);
    return TRUE;
}

/* HalFlushCommonBuffer removed - implemented in generic/dma.c */

/* HalFreeCommonBuffer removed - implemented in generic/dma.c */

/* HalGetAdapter removed - implemented in generic/dma.c */

ULONG
NTAPI
HalGetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              OUT PVOID Buffer,
              IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);

    if (Buffer && Length)
    {
        RtlZeroMemory(Buffer, Length);
    }

    return 0;
}

ULONG
NTAPI
HalGetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      OUT PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Offset);

    if (Buffer && Length)
    {
        RtlZeroMemory(Buffer, Length);
    }

    return 0;
}

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(IN PCH Variable,
                          IN USHORT Length,
                          OUT PCH Buffer)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Buffer);
    return EINVAL;
}

ULONG
NTAPI
HalGetInterruptVector(IN INTERFACE_TYPE InterfaceType,
                      IN ULONG BusNumber,
                      IN ULONG BusInterruptLevel,
                      IN ULONG BusInterruptVector,
                      OUT PKIRQL Irql,
                      OUT PKAFFINITY Affinity)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInterruptLevel);

    if (Irql)
    {
        *Irql = PASSIVE_LEVEL;
    }

    if (Affinity)
    {
        *Affinity = 1;
    }

    return BusInterruptVector;
}

VOID
NTAPI
HalInitializeProcessor(IN ULONG ProcessorNumber,
                       IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(ProcessorNumber);
    UNREFERENCED_PARAMETER(LoaderBlock);
}

BOOLEAN
NTAPI
HalMakeBeep(IN ULONG Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    return FALSE;
}

/* HalProcessorIdle removed - implemented in generic/halinit.c */

VOID
NTAPI
HalQueryDisplayParameters(OUT PULONG DispSizeX,
                          OUT PULONG DispSizeY,
                          OUT PULONG CursorPosX,
                          OUT PULONG CursorPosY)
{
    if (DispSizeX)
    {
        *DispSizeX = 0;
    }

    if (DispSizeY)
    {
        *DispSizeY = 0;
    }

    if (CursorPosX)
    {
        *CursorPosX = 0;
    }

    if (CursorPosY)
    {
        *CursorPosY = 0;
    }
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(IN PTIME_FIELDS RtcTime)
{
    if (RtcTime)
    {
        RtlZeroMemory(RtcTime, sizeof(*RtcTime));
    }

    return TRUE;
}

/* HalReadDmaCounter removed - implemented in generic/dma.c */

VOID
NTAPI
HalReportResourceUsage(VOID)
{
    DPRINT1("HalReportResourceUsage stub invoked\n");
}

VOID
NTAPI
HalRequestIpi(IN KAFFINITY TargetSet)
{
    UNREFERENCED_PARAMETER(TargetSet);
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(IN KIRQL SoftwareInterruptRequested)
{
    UNREFERENCED_PARAMETER(SoftwareInterruptRequested);
}

VOID
NTAPI
HalReturnToFirmware(IN FIRMWARE_REENTRY Action)
{
    /* No firmware to return to, crash so the caller notices */
    KeBugCheckEx(HAL_INITIALIZATION_FAILED, (ULONG_PTR)Action, 0, 0, 0);
}

ULONG
NTAPI
HalSetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              IN PVOID Buffer,
              IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

ULONG
NTAPI
HalSetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

VOID
NTAPI
HalSetDisplayParameters(IN ULONG CursorPosX,
                        IN ULONG CursorPosY)
{
    UNREFERENCED_PARAMETER(CursorPosX);
    UNREFERENCED_PARAMETER(CursorPosY);
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(IN PCH Name,
                          IN PCH Value)
{
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    return EINVAL;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(IN ULONG_PTR Interval)
{
    HalpProfileInterval = Interval;
    return HalpProfileInterval;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(IN PTIME_FIELDS RtcTime)
{
    UNREFERENCED_PARAMETER(RtcTime);
    return FALSE;
}

ULONG
NTAPI
HalSetTimeIncrement(IN ULONG Increment)
{
    if (Increment)
    {
        HalpTimeIncrement = Increment;
    }

    return HalpTimeIncrement;
}

BOOLEAN
NTAPI
HalStartNextProcessor(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(ProcessorState);
    return FALSE;
}

VOID
NTAPI
HalStartProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}

VOID
NTAPI
HalStopProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}

UCHAR
FASTCALL
HalSystemVectorDispatchEntry(IN ULONG Vector,
                             OUT PKINTERRUPT_ROUTINE **FlatDispatch,
                             OUT PKINTERRUPT_ROUTINE *NoConnection)
{
    UNREFERENCED_PARAMETER(Vector);

    if (FlatDispatch)
    {
        *FlatDispatch = NULL;
    }

    if (NoConnection)
    {
        *NoConnection = NULL;
    }

    return 0;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                       IN ULONG BusNumber,
                       IN PHYSICAL_ADDRESS BusAddress,
                       IN OUT PULONG AddressSpace,
                       OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);

    if (AddressSpace)
    {
        *AddressSpace = 0;
    }

    if (TranslatedAddress)
    {
        TranslatedAddress->QuadPart = BusAddress.QuadPart;
    }

    return TRUE;
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(IN PADAPTER_OBJECT AdapterObject,
                      IN PMDL Mdl,
                      IN PVOID MapRegisterBase,
                      IN PVOID CurrentVa,
                      IN ULONG Length,
                      IN BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    return TRUE;
}

VOID
NTAPI
IoFreeAdapterChannel(IN PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
}

VOID
NTAPI
IoFreeMapRegisters(IN PADAPTER_OBJECT AdapterObject,
                   IN PVOID MapRegisterBase,
                   IN ULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(IN PADAPTER_OBJECT AdapterObject,
              IN PMDL Mdl,
              IN PVOID MapRegisterBase,
              IN PVOID CurrentVa,
              IN OUT PULONG Length,
              IN BOOLEAN WriteToDevice)
{
    PHYSICAL_ADDRESS Address = {0};

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);

    return Address;
}

VOID
NTAPI
KeAcquireSpinLock(IN PKSPIN_LOCK SpinLock,
                  OUT PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(SpinLock);
    if (OldIrql)
    {
        *OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    }
    else
    {
        (VOID)KfRaiseIrql(DISPATCH_LEVEL);
    }
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(IN PKSPIN_LOCK SpinLock)
{
    UNREFERENCED_PARAMETER(SpinLock);
    return KfRaiseIrql(SYNCH_LEVEL);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    UNREFERENCED_PARAMETER(LockNumber);
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    UNREFERENCED_PARAMETER(LockNumber);
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(IN PKSPIN_LOCK SpinLock,
                                           IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    UNREFERENCED_PARAMETER(SpinLock);
    if (LockHandle)
    {
        LockHandle->OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    }
    else
    {
        (VOID)KfRaiseIrql(SYNCH_LEVEL);
    }
}

VOID
NTAPI
KeReleaseSpinLock(IN PKSPIN_LOCK SpinLock,
                  IN KIRQL NewIrql)
{
    UNREFERENCED_PARAMETER(SpinLock);
    KfLowerIrql(NewIrql);
}

VOID
FASTCALL
KeReleaseQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                        IN KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(LockNumber);
    KfLowerIrql(OldIrql);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    if (LockHandle)
    {
        KfLowerIrql(LockHandle->OldIrql);
    }
}

VOID
FASTCALL
KfReleaseSpinLock(IN PKSPIN_LOCK SpinLock,
                  IN KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(SpinLock);
    KfLowerIrql(OldIrql);
}

KIRQL
FASTCALL
KfAcquireSpinLock(IN PKSPIN_LOCK SpinLock)
{
    UNREFERENCED_PARAMETER(SpinLock);
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    HalpDataBarrier();
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(OUT PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter;

    if (PerformanceFrequency)
    {
        PerformanceFrequency->QuadPart = HalpPerformanceFrequency;
    }

    Counter.QuadPart = HalpReadVirtualCounter();
    return Counter;
}

VOID
NTAPI
KeStallExecutionProcessor(IN ULONG Microseconds)
{
    const ULONGLONG TicksPerMicrosecond = (HalpPerformanceFrequency ?
                                           HalpPerformanceFrequency / 1000000ULL : 0ULL);
    ULONGLONG Target;

    if (TicksPerMicrosecond == 0)
    {
        return;
    }

    Target = HalpReadVirtualCounter() + (TicksPerMicrosecond * Microseconds);
    while (HalpReadVirtualCounter() < Target)
    {
#if defined(_M_ARM64)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
}

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                             OUT PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(LockNumber);
    if (OldIrql)
    {
        *OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    }
    else
    {
        (VOID)KfRaiseIrql(DISPATCH_LEVEL);
    }

    return TRUE;
}

BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                                         IN PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(LockNumber);
    if (OldIrql)
    {
        *OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    }
    else
    {
        (VOID)KfRaiseIrql(SYNCH_LEVEL);
    }

    return TRUE;
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(IN KIRQL Irql,
                        IN ULONG Vector,
                        OUT PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Vector);

    if (OldIrql)
    {
        *OldIrql = KfRaiseIrql(Irql);
    }
    else
    {
        (VOID)KfRaiseIrql(Irql);
    }

    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(IN KIRQL Irql,
                      IN PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    KfLowerIrql(Irql);
}
