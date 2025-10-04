/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture abstraction wiring for AMD64
 */

#include <freeldr.h>
#include <uefildr.h>
#include <arch/arch_interface.h>
#include <drivers/acpi/acpi.h>
#include <debug.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

DBG_DEFAULT_CHANNEL(WARNING);

extern VOID Amd64InitializeExceptions(VOID);
extern PFREELDR_MEMORY_DESCRIPTOR UefiMemGetMemoryMap(ULONG *MemoryMapSize);
extern VOID _exituefi(VOID);
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_MEMORY_DESCRIPTOR* EfiMemoryMap;
extern UINT32 FreeldrDescCount;

static EFI_STATUS Amd64ArchInitializeTimer(VOID);
static EFI_STATUS Amd64ArchDetectCpu(_Out_ PARCH_CPU_INFO CpuInfo);
static const CHAR* Amd64ArchGetSystemFamilyString(VOID);
static VOID Amd64ArchDetectAcpi(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber);

/* ------------------------------------------------------------------------- */
/* Helper utilities                                                          */
/* ------------------------------------------------------------------------- */

typedef struct _CPUID_INFO
{
    UINT32 Eax;
    UINT32 Ebx;
    UINT32 Ecx;
    UINT32 Edx;
} CPUID_INFO, *PCPUID_INFO;

static inline VOID
CpuidEx(
    _In_ UINT32 Leaf,
    _In_ UINT32 SubLeaf,
    _Out_ PCPUID_INFO Info)
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, Leaf, SubLeaf);
    Info->Eax = (UINT32)regs[0];
    Info->Ebx = (UINT32)regs[1];
    Info->Ecx = (UINT32)regs[2];
    Info->Edx = (UINT32)regs[3];
#else
    UINT32 a, b, c, d;
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(Leaf), "c"(SubLeaf));
    Info->Eax = a;
    Info->Ebx = b;
    Info->Ecx = c;
    Info->Edx = d;
#endif
}

static inline VOID
ArchEnableInterrupts(VOID)
{
#if defined(_MSC_VER)
    _enable();
#else
    __asm__ __volatile__("sti" ::: "memory");
#endif
}

static inline VOID
ArchDisableInterrupts(VOID)
{
#if defined(_MSC_VER)
    _disable();
#else
    __asm__ __volatile__("cli" ::: "memory");
#endif
}

static inline VOID
ArchReloadCr3(VOID)
{
#if defined(_MSC_VER)
    __writecr3(__readcr3());
#else
    UINT64 Cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r" (Cr3));
    __asm__ __volatile__("mov %0, %%cr3" :: "r" (Cr3) : "memory");
#endif
}

static inline VOID
ArchWbInvd(VOID)
{
#if defined(_MSC_VER)
    _mm_mfence();
    __wbinvd();
#else
    __asm__ __volatile__("mfence" ::: "memory");
    __asm__ __volatile__("wbinvd" ::: "memory");
#endif
}

static inline ULONG64
ArchReadTsc(VOID)
{
#if defined(_MSC_VER)
    return __rdtsc();
#else
    UINT32 Lo, Hi;
    __asm__ __volatile__("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((ULONG64)Hi << 32) | Lo;
#endif
}

static KERNEL_PARAMS gKernelParams;
static ULONG64 gTimerFrequency;
static BOOLEAN gBootServicesExited;

/* ------------------------------------------------------------------------- */
/* Architecture interface implementation                                     */
/* ------------------------------------------------------------------------- */

static EFI_STATUS
Amd64ArchInit(VOID)
{
    TRACE("Amd64ArchInit()\n");
    RtlZeroMemory(&gKernelParams, sizeof(gKernelParams));
    gTimerFrequency = 0;
    gBootServicesExited = FALSE;
    /*
     * Do not install custom exception handlers while we still use
     * UEFI Boot Services console paths (ConOut/StreamOut). Some
     * firmwares fault in those paths when a custom IDT is active.
     * We'll install exceptions later, right before ExitBootServices.
     */
    {
        EFI_STATUS t = Amd64ArchInitializeTimer();
        TRACE("[AMD64] Timer init status=%I64x\n", (unsigned long long)(UINTN)t);
    }

    /* Verbose CPU information */
    {
        ARCH_CPU_INFO Ci;
        EFI_STATUS s = Amd64ArchDetectCpu(&Ci);
        if (!EFI_ERROR(s))
        {
            TRACE("[AMD64] CPU Vendor='%s' Type=%lu Revision=0x%lx Features=0x%08lx\n",
                  Ci.VendorString, (unsigned long)Ci.ProcessorType,
                  (unsigned long)Ci.ProcessorRevision,
                  (unsigned long)Ci.ProcessorFeatures);
            TRACE("[AMD64] CacheLine=%u L1D=%lu L1I=%lu L2=%lu KB\n",
                  (unsigned)Ci.CacheLineSize,
                  (unsigned long)Ci.L1DataCacheSize/1024,
                  (unsigned long)Ci.L1InstructionCacheSize/1024,
                  (unsigned long)Ci.L2CacheSize/1024);
        }
        else
        {
            TRACE("[AMD64] Amd64ArchDetectCpu failed: %I64x\n", (unsigned long long)(UINTN)s);
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS
Amd64ArchDetectCpu(
    _Out_ PARCH_CPU_INFO CpuInfo)
{
    CPUID_INFO Info;
    CHAR Vendor[13];

    if (!CpuInfo)
    {
        return EFI_INVALID_PARAMETER;
    }

    RtlZeroMemory(CpuInfo, sizeof(*CpuInfo));

    /* Vendor string and highest leaf */
    CpuidEx(0, 0, &Info);
    *(UINT32 *)&Vendor[0] = Info.Ebx;
    *(UINT32 *)&Vendor[4] = Info.Edx;
    *(UINT32 *)&Vendor[8] = Info.Ecx;
    Vendor[12] = '\0';
    RtlStringCbCopyA(CpuInfo->VendorString, sizeof(CpuInfo->VendorString), Vendor);

    /* Processor signature */
    CpuidEx(1, 0, &Info);
    CpuInfo->ProcessorType = (Info.Eax >> 12) & 0x3;
    CpuInfo->ProcessorRevision = Info.Eax & 0xFFF;
    CpuInfo->ProcessorFeatures = Info.Edx;
    CpuInfo->CacheLineSize = ((Info.Ebx >> 8) & 0xFF) * 8; /* CLFLUSH line size */

    /* Extended cache info (L2/L3) */
    CpuidEx(0x80000006, 0, &Info);
    CpuInfo->L1DataCacheSize = CpuInfo->CacheLineSize * 64; /* heuristic */
    CpuInfo->L1InstructionCacheSize = CpuInfo->L1DataCacheSize;
    CpuInfo->L2CacheSize = (Info.Ecx >> 16) * 1024;

    return EFI_SUCCESS;
}

static EFI_STATUS
Amd64ArchSetupMmu(
    _Out_ PMEMORY_MAP MemoryMap)
{
    EFI_STATUS Status;
    EFI_MEMORY_DESCRIPTOR *Descriptors = NULL;
    UINTN MapSize = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices || !MemoryMap)
    {
        return EFI_INVALID_PARAMETER;
    }

    /* Query required buffer size */
    Status = GlobalSystemTable->BootServices->GetMemoryMap(&MapSize,
                                                           Descriptors,
                                                           &MapKey,
                                                           &DescriptorSize,
                                                           &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL)
    {
        TRACE("GetMemoryMap(size probe) failed: %I64x\n",
              (unsigned long long)(UINTN)Status);
        return Status;
    }

    Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                           MapSize,
                                                           (VOID **)&Descriptors);
    if (EFI_ERROR(Status))
    {
        TRACE("AllocatePool(%I64u) failed: %I64x\n",
              (unsigned long long)(UINTN)MapSize,
              (unsigned long long)(UINTN)Status);
        return Status;
    }

    Status = GlobalSystemTable->BootServices->GetMemoryMap(&MapSize,
                                                           Descriptors,
                                                           &MapKey,
                                                           &DescriptorSize,
                                                           &DescriptorVersion);
    if (EFI_ERROR(Status))
    {
        TRACE("GetMemoryMap(final) failed: %I64x\n",
              (unsigned long long)(UINTN)Status);
        GlobalSystemTable->BootServices->FreePool(Descriptors);
        return Status;
    }

    MemoryMap->MemoryMap = Descriptors;
    MemoryMap->MemoryMapSize = MapSize;
    MemoryMap->DescriptorSize = DescriptorSize;
    MemoryMap->DescriptorVersion = DescriptorVersion;
    return EFI_SUCCESS;
}

static EFI_STATUS
Amd64ArchEnablePaging(VOID)
{
    /* Paging is already enabled in long mode. */
    return EFI_SUCCESS;
}

static VOID
Amd64ArchInvalidateTlb(VOID)
{
    ArchReloadCr3();
}

static VOID
Amd64ArchFlushCache(VOID)
{
    ArchWbInvd();
}

static EFI_STATUS
Amd64ArchSetupExceptions(VOID)
{
    Amd64InitializeExceptions();
    return EFI_SUCCESS;
}

static VOID
Amd64ArchHandleException(
    _In_ PEXCEPTION_FRAME Frame)
{
    UNREFERENCED_PARAMETER(Frame);
    /* TODO: integrate with amd64 trap diagnostics */
}

static EFI_STATUS
Amd64ArchInitializeTimer(VOID)
{
    CPUID_INFO Info;

    gTimerFrequency = 0;

    /* Try CPUID leaf 0x15 for TSC frequency */
    CpuidEx(0, 0, &Info);
    if (Info.Eax >= 0x15)
    {
        CpuidEx(0x15, 0, &Info);
        if (Info.Eax != 0 && Info.Ebx != 0)
        {
            ULONG64 ReferenceHz = Info.Ecx;

            if (ReferenceHz == 0)
            {
                /* Fallback to 24 MHz crystal if firmware did not provide ECX */
                ReferenceHz = 24000000ULL;
            }

            gTimerFrequency = (ReferenceHz * Info.Ebx) / Info.Eax;
        }
    }

    /* Try CPUID leaf 0x16 for nominal core frequency if still unknown */
    if (gTimerFrequency == 0)
    {
        CpuidEx(0, 0, &Info);
        if (Info.Eax >= 0x16)
        {
            CpuidEx(0x16, 0, &Info);
            if (Info.Eax != 0)
            {
                gTimerFrequency = ((ULONG64)Info.Eax) * 1000000ULL;
            }
        }
    }

    return EFI_SUCCESS;
}

static ULONG64
Amd64ArchGetTimerFrequency(VOID)
{
    return gTimerFrequency;
}

static ULONG64
Amd64ArchGetTimerValue(VOID)
{
    return ArchReadTsc();
}

static VOID
Amd64ArchStallExecution(
    _In_ ULONG Microseconds)
{
    if (GlobalSystemTable && GlobalSystemTable->BootServices)
    {
        GlobalSystemTable->BootServices->Stall(Microseconds);
    }
}

static EFI_STATUS
Amd64ArchPrepareForKernel(
    _Inout_ PKERNEL_PARAMS Params)
{
    if (Params == NULL)
    {
        return EFI_INVALID_PARAMETER;
    }

    if (Params->KernelEntry == 0)
    {
        return EFI_INVALID_PARAMETER;
    }

    RtlCopyMemory(&gKernelParams, Params, sizeof(*Params));

    if (gKernelParams.MemoryMap == NULL)
    {
        ULONG Count = 0;
        gKernelParams.MemoryMap = UefiMemGetMemoryMap(&Count);
        gKernelParams.MemoryMapCount = Count;
    }

    if (!gBootServicesExited)
    {
        ArchDisableInterrupts();
        _exituefi();
        gBootServicesExited = TRUE;
    }

    return EFI_SUCCESS;
}

static VOID
Amd64FinalizeKernelParameters(
    _Inout_ PVOID *LoaderBlock,
    _Inout_ PVOID *KernelEntry)
{
    if ((LoaderBlock == NULL) || (KernelEntry == NULL))
    {
        return;
    }

    if (*LoaderBlock == NULL && gKernelParams.LoaderBlock)
    {
        *LoaderBlock = gKernelParams.LoaderBlock;
    }

    if (*KernelEntry == NULL && gKernelParams.KernelEntry)
    {
        *KernelEntry = (PVOID)(ULONG_PTR)gKernelParams.KernelEntry;
    }
}

static VOID
Amd64ArchJumpToKernel(
    _In_ PVOID KernelEntry,
    _In_ PVOID LoaderBlock)
{
    typedef VOID (NTAPI *PKERNEL_ENTRY)(PVOID, PVOID, PVOID, PVOID);
    PVOID LocalLoader = LoaderBlock;
    PVOID LocalEntry = KernelEntry;

    Amd64FinalizeKernelParameters(&LocalLoader, &LocalEntry);

    TRACE("Transferring control to kernel entry %p (LoaderBlock=%p)\n",
          LocalEntry, LocalLoader);

    if (!LocalEntry || !LocalLoader)
    {
        ERR("Amd64ArchJumpToKernel missing entry=%p loader=%p\n",
            LocalEntry, LocalLoader);
        return;
    }

    PKERNEL_ENTRY Entry = (PKERNEL_ENTRY)(ULONG_PTR)LocalEntry;
    Entry(LocalLoader, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------------- */
/* Public interface                                                          */
/* ------------------------------------------------------------------------- */

ARCH_INTERFACE ArchInterface =
{
    .ArchInit = Amd64ArchInit,
    .ArchDetectCPU = Amd64ArchDetectCpu,
    .ArchEnableInterrupts = ArchEnableInterrupts,
    .ArchDisableInterrupts = ArchDisableInterrupts,
    .ArchSetupMMU = Amd64ArchSetupMmu,
    .ArchEnablePaging = Amd64ArchEnablePaging,
    .ArchInvalidateTLB = Amd64ArchInvalidateTlb,
    .ArchFlushCache = Amd64ArchFlushCache,
    .ArchSetupExceptions = Amd64ArchSetupExceptions,
    .ArchHandleException = Amd64ArchHandleException,
    .ArchInitializeTimer = Amd64ArchInitializeTimer,
    .ArchGetTimerFrequency = Amd64ArchGetTimerFrequency,
    .ArchGetTimerValue = Amd64ArchGetTimerValue,
    .ArchStallExecution = Amd64ArchStallExecution,
    .ArchPrepareForKernel = Amd64ArchPrepareForKernel,
    .ArchJumpToKernel = Amd64ArchJumpToKernel,
    .ArchName = (const CHAR8 *)"AMD64",
    .ArchFeatures = 0,
    .ArchCacheLineSize = 64,
    .RequiresIdentityMapping = TRUE,
    .ArchGetSystemFamilyString = Amd64ArchGetSystemFamilyString,
    .ArchDetectAcpi = Amd64ArchDetectAcpi,
};

EFI_STATUS
ArchInitialize(VOID)
{
    return ArchInterface.ArchInit ? ArchInterface.ArchInit() : EFI_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Common hook implementations to keep common code ifdef-free                 */
/* ------------------------------------------------------------------------- */

static const CHAR*
Amd64ArchGetSystemFamilyString(VOID)
{
    return "AT/AT COMPATIBLE";
}

static PRSDP_DESCRIPTOR
Amd64FindAcpiRsdp(VOID)
{
    if (!GlobalSystemTable)
        return NULL;

    EFI_GUID acpi2_guid = EFI_ACPI_20_TABLE_GUID;
    for (UINTN i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (!memcmp(&GlobalSystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi2_guid, sizeof(acpi2_guid)))
        {
            return (PRSDP_DESCRIPTOR)GlobalSystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static VOID
Amd64ArchDetectAcpi(
    PCONFIGURATION_COMPONENT_DATA SystemKey,
    ULONG *BusNumber)
{
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PRSDP_DESCRIPTOR Rsdp;
    PACPI_BIOS_DATA AcpiBiosData;
    ULONG TableSize;

    UNREFERENCED_PARAMETER(SystemKey);
    UNREFERENCED_PARAMETER(BusNumber);

    Rsdp = Amd64FindAcpiRsdp();

    if (Rsdp)
    {
        /* Calculate the table size */
        TableSize = FreeldrDescCount * sizeof(BIOS_MEMORY_MAP) +
            sizeof(ACPI_BIOS_DATA) - sizeof(BIOS_MEMORY_MAP);

        /* Set 'Configuration Data' value */
        PartialResourceList = FrLdrHeapAlloc(sizeof(CM_PARTIAL_RESOURCE_LIST) +
                                             TableSize, TAG_HW_RESOURCE_LIST);
        if (PartialResourceList == NULL)
        {
            ERR("[AMD64] Failed to allocate ACPI resource descriptor\n");
            return;
        }

        RtlZeroMemory(PartialResourceList, sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize);
        PartialResourceList->Version = 0;
        PartialResourceList->Revision = 0;
        PartialResourceList->Count = 1;

        PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
        PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
        PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
        PartialDescriptor->u.DeviceSpecificData.DataSize = TableSize;

        /* Fill the table */
        AcpiBiosData = (PACPI_BIOS_DATA)&PartialResourceList->PartialDescriptors[1];

        if (Rsdp->revision > 0)
        {
            TRACE("[AMD64] ACPI >1.0, using XSDT address\n");
            AcpiBiosData->RSDTAddress.QuadPart = Rsdp->xsdt_physical_address;
        }
        else
        {
            TRACE("[AMD64] ACPI 1.0, using RSDT address\n");
            AcpiBiosData->RSDTAddress.LowPart = Rsdp->rsdt_physical_address;
        }

        AcpiBiosData->Count = FreeldrDescCount;
        RtlCopyMemory(AcpiBiosData->MemoryMap, EfiMemoryMap, FreeldrDescCount * sizeof(BIOS_MEMORY_MAP));

        TRACE("[AMD64] ACPI RSDT %p, data size %x\n", Rsdp->rsdt_physical_address, TableSize);

        /* Create new bus key */
        PCONFIGURATION_COMPONENT_DATA BiosKey;
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0x0,
                               0x0,
                               0xFFFFFFFF,
                               "ACPI BIOS",
                               PartialResourceList,
                               sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize,
                               &BiosKey);

        if (BusNumber)
            (*BusNumber)++;
    }
}
