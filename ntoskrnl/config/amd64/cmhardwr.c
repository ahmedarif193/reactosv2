/*
 * PROJECT:         ReactOS Kernel (amd64)
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/amd64/cmhardwr.c
 * PURPOSE:         Configuration Manager - Hardware-Specific Code (UEFI/SMBIOS-centric)
 * PROGRAMMERS:     Ahmed ARIF, 2025 amd64 adaptation (contact@eotics.com)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* =============================================================================
 * Notes
 * -----
 * - This amd64 version avoids legacy BIOS IVT / option ROM probing.
 * - System BIOS info (date/version) is taken from SMBIOS Type 0.
 * - Video BIOS keys are intentionally skipped on UEFI/GOP without CSM.
 * - No PAE registry toggles on amd64.
 * =============================================================================
 */

/* External strings optionally mirrored for reporting (if available elsewhere) */
extern UNICODE_STRING KeRosProcessorName;
extern UNICODE_STRING KeRosBiosDate;
extern UNICODE_STRING KeRosBiosVersion;

/* Forward declarations */
NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/* ============================ SMBIOS support =============================== */

#pragma pack(push, 1)
typedef struct _SMBIOS_EPS_2X
{
    CHAR  Anchor[4];              /* "_SM_" */
    UCHAR Checksum;
    UCHAR Length;                 /* 0x1F */
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT MaxStructSize;
    UCHAR EntryPointRevision;
    UCHAR FormattedArea[5];
    CHAR  IntermediateAnchor[5];  /* "_DMI_" */
    UCHAR IntermediateChecksum;
    USHORT TableLength;
    ULONG TableAddress;           /* Physical 32-bit */
    USHORT NumberOfStructures;
    UCHAR BCDRevision;
} SMBIOS_EPS_2X;

typedef struct _SMBIOS_EPS_3X
{
    CHAR  Anchor[5];              /* "_SM3_" */
    UCHAR Checksum;
    UCHAR Length;                 /* >= 0x18 */
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    UCHAR DocRev;
    UCHAR EntryPointRevision;
    UCHAR Reserved;
    ULONG TableMaxSize;
    ULONGLONG TableAddress;       /* Physical 64-bit */
} SMBIOS_EPS_3X;

typedef struct _SMBIOS_HEADER
{
    UCHAR Type;
    UCHAR Length;
    USHORT Handle;
} SMBIOS_HEADER;
#pragma pack(pop)

typedef struct _SMBIOS_TABLE_DESC
{
    ULONGLONG PhysicalAddress;
    ULONG Length;            /* For SMBIOS 3.x: TableMaxSize (upper bound). For 2.x: exact length. */
    UCHAR  Major;
    UCHAR  Minor;
} SMBIOS_TABLE_DESC;

static __inline
BOOLEAN
SmbiosChecksumOk(_In_reads_bytes_(Len) const UCHAR* Buf, _In_ SIZE_T Len)
{
    UCHAR sum = 0;
    for (SIZE_T i = 0; i < Len; ++i) sum = (UCHAR)(sum + Buf[i]);
    return (sum == 0);
}

/* Locate SMBIOS EntryPoint in legacy F-segment (0xF0000..0xFFFFF).
 * Tries SMBIOS 3.x first ("_SM3_"), then SMBIOS 2.x ("_SM_").
 */
static
NTSTATUS
CmpLocateSmbiosInFSegment(
    _Out_ SMBIOS_TABLE_DESC* OutDesc
)
{
    RtlZeroMemory(OutDesc, sizeof(*OutDesc));

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = 0x000000000000F0000ULL; /* F-segment */
    SIZE_T len = 0x10000;                 /* 64 KiB */

    PUCHAR view = (PUCHAR)MmMapIoSpace(pa, len, MmNonCached);
    if (!view) return STATUS_INSUFFICIENT_RESOURCES;

    PUCHAR p = view, end = view + len;

    /* Try SMBIOS 3.x ("_SM3_") */
    for (PUCHAR q = p; q + sizeof(SMBIOS_EPS_3X) <= end; q++)
    {
        if (q[0]=='_' && q[1]=='S' && q[2]=='M' && q[3]=='3' && q[4]=='_')
        {
            SMBIOS_EPS_3X* eps3 = (SMBIOS_EPS_3X*)q;
            if (eps3->Length >= sizeof(SMBIOS_EPS_3X) &&
                SmbiosChecksumOk((const UCHAR*)eps3, eps3->Length) &&
                eps3->TableAddress && eps3->TableMaxSize >= 0x100)
            {
                OutDesc->PhysicalAddress = eps3->TableAddress;
                OutDesc->Length = eps3->TableMaxSize; /* upper bound */
                OutDesc->Major = eps3->MajorVersion;
                OutDesc->Minor = eps3->MinorVersion;
                MmUnmapIoSpace(view, len);
                return STATUS_SUCCESS;
            }
        }
    }

    /* Try SMBIOS 2.x ("_SM_") */
    for (PUCHAR q = p; q + sizeof(SMBIOS_EPS_2X) <= end; q++)
    {
        if (q[0]=='_' && q[1]=='S' && q[2]=='M' && q[3]=='_')
        {
            SMBIOS_EPS_2X* eps = (SMBIOS_EPS_2X*)q;
            if (eps->Length >= sizeof(SMBIOS_EPS_2X) &&
                SmbiosChecksumOk((const UCHAR*)eps, eps->Length) &&
                RtlCompareMemory(eps->IntermediateAnchor, "_DMI_", 5) == 5 &&
                SmbiosChecksumOk((const UCHAR*)&eps->IntermediateAnchor, 0x0F) &&
                eps->TableAddress && eps->TableLength >= 0x100)
            {
                OutDesc->PhysicalAddress = (ULONGLONG)eps->TableAddress;
                OutDesc->Length = eps->TableLength;  /* exact */
                OutDesc->Major = eps->MajorVersion;
                OutDesc->Minor = eps->MinorVersion;
                MmUnmapIoSpace(view, len);
                return STATUS_SUCCESS;
            }
        }
    }

    MmUnmapIoSpace(view, len);
    return STATUS_NOT_FOUND;
}


/* Maps the SMBIOS table region described by Desc (read-only). */
static
NTSTATUS
CmpMapSmbiosTable(
    _In_ const SMBIOS_TABLE_DESC* Desc,
    _Out_ PVOID* RawMappedBase,     /* base we pass to MmUnmapIoSpace */
    _Out_ SIZE_T* RawMappedSize,    /* size we pass to MmUnmapIoSpace */
    _Out_ PVOID* AdjustedStart,     /* table-aligned VA = RawBase + delta */
    _Out_ SIZE_T* AdjustedLength    /* RawSize - delta */
)
{
    *RawMappedBase = NULL;
    *RawMappedSize = 0;
    *AdjustedStart = NULL;
    *AdjustedLength = 0;

    if (!Desc->PhysicalAddress || Desc->Length < 0x100)
        return STATUS_INVALID_PARAMETER;

    ULONGLONG phys = Desc->PhysicalAddress;
    ULONGLONG aligned = phys & ~((ULONGLONG)PAGE_SIZE - 1);
    SIZE_T delta = (SIZE_T)(phys - aligned);

    /* Clamp huge SMBIOS3 max sizes to a sane cap */
    SIZE_T want = delta + (Desc->Length > (8*1024*1024) ? (8*1024*1024) : Desc->Length);
    SIZE_T mapSize = (want + PAGE_SIZE - 1) & ~((SIZE_T)PAGE_SIZE - 1);

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = aligned;

    PUCHAR base = (PUCHAR)MmMapIoSpace(pa, mapSize, MmNonCached);
    if (!base) return STATUS_INSUFFICIENT_RESOURCES;

    *RawMappedBase = base;
    *RawMappedSize = mapSize;
    *AdjustedStart = base + delta;
    *AdjustedLength = mapSize - delta;
    return STATUS_SUCCESS;
}

/* Return pointer to Nth string (1-based) after a structure. */
static
PCSTR
SmbiosGetString(
    _In_ const UCHAR* StructStart,
    _In_ UCHAR HeaderLength,
    _In_ UCHAR Index,
    _In_ const UCHAR* TableEnd
)
{
    if (Index == 0) return NULL;

    const UCHAR* p = StructStart + HeaderLength;
    if (p >= TableEnd) return NULL;

    UCHAR current = 1;

    while (p < TableEnd)
    {
        SIZE_T remain = (SIZE_T)(TableEnd - p);
        /* End of structure list: double NUL */
        if (remain >= 2 && p[0] == 0 && p[1] == 0)
            break;

        SIZE_T len = strnlen((const CHAR*)p, remain);
        if (len == remain) /* not terminated inside table */
            return NULL;

        if (current == Index)
            return (PCSTR)p;

        p += len + 1;
        current++;
    }
    return NULL;
}

static
NTSTATUS
CmpExtractType0BiosStrings(
    _In_reads_bytes_(TableLen) const UCHAR* Table,
    _In_ SIZE_T TableLen,
    _Out_opt_ UNICODE_STRING* OutDate,
    _Out_opt_ UNICODE_STRING* OutMultiVersion /* Vendor\0Version\0\0 */
)
{
    const UCHAR* p = Table;
    const UCHAR* end = Table + TableLen;
    const SMBIOS_HEADER* Hdr;

    if (OutDate) RtlZeroMemory(OutDate, sizeof(*OutDate));
    if (OutMultiVersion) RtlZeroMemory(OutMultiVersion, sizeof(*OutMultiVersion));

    while (p + sizeof(SMBIOS_HEADER) <= end)
    {
        Hdr = (const SMBIOS_HEADER*)p;
        if (Hdr->Length < sizeof(SMBIOS_HEADER))
            break; /* corrupted */

        const UCHAR* next = p + Hdr->Length;
        /* Walk strings to find the next structure start */
        const UCHAR* strings = next;
        while (next + 1 < end)
        {
            if (next[0] == 0 && next[1] == 0)
            {
                next += 2;
                break;
            }
            next++;
        }
        if (next > end) break;

        if (Hdr->Type == 0 && Hdr->Length >= 0x12)
        {
            /* Offsets (per SMBIOS Type 0):
             * 0x04 Vendor (STRING)
             * 0x05 BIOS Version (STRING)
             * 0x08 Release Date (STRING)
             */
            UCHAR vendorIdx  = p[0x04];
            UCHAR versionIdx = p[0x05];
            UCHAR dateIdx    = p[0x08];

            PCSTR vendorA  = vendorIdx  ? SmbiosGetString(p, Hdr->Length, vendorIdx,  end) : NULL;
            PCSTR versionA = versionIdx ? SmbiosGetString(p, Hdr->Length, versionIdx, end) : NULL;
            PCSTR dateA    = dateIdx    ? SmbiosGetString(p, Hdr->Length, dateIdx,    end) : NULL;

            /* Build OutDate */
            if (OutDate && dateA && *dateA)
            {
                ANSI_STRING a; RtlInitAnsiString(&a, dateA);
                if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(OutDate, &a, TRUE)))
                    RtlZeroMemory(OutDate, sizeof(*OutDate));
            }

            /* Build OutMultiVersion = Vendor\0Version\0\0 */
            if (OutMultiVersion && (vendorA || versionA))
            {
                ANSI_STRING av = {0}, ver = {0};
                UNICODE_STRING uv = {0}, uver = {0};
                SIZE_T total = sizeof(WCHAR); /* final terminator */
                NTSTATUS st1 = STATUS_UNSUCCESSFUL, st2 = STATUS_UNSUCCESSFUL;

                if (vendorA && *vendorA)
                {
                    RtlInitAnsiString(&av, vendorA);
                    st1 = RtlAnsiStringToUnicodeString(&uv, &av, TRUE);
                    if (NT_SUCCESS(st1)) total += uv.Length + sizeof(WCHAR);
                }
                if (versionA && *versionA)
                {
                    RtlInitAnsiString(&ver, versionA);
                    st2 = RtlAnsiStringToUnicodeString(&uver, &ver, TRUE);
                    if (NT_SUCCESS(st2)) total += uver.Length + sizeof(WCHAR);
                }

                if (total > sizeof(WCHAR))
                {
                    PWSTR buf = (PWSTR)ExAllocatePoolWithTag(PagedPool, total, 'mbSC'); /* "CSbm" */
                    if (buf)
                    {
                        PWSTR w = buf;
                        if (NT_SUCCESS(st1))
                        {
                            RtlCopyMemory(w, uv.Buffer, uv.Length);
                            w += uv.Length / sizeof(WCHAR);
                            *w++ = UNICODE_NULL;
                        }
                        if (NT_SUCCESS(st2))
                        {
                            RtlCopyMemory(w, uver.Buffer, uver.Length);
                            w += uver.Length / sizeof(WCHAR);
                            *w++ = UNICODE_NULL;
                        }
                        *w = UNICODE_NULL;

                        OutMultiVersion->Buffer = buf;
                        OutMultiVersion->Length = (USHORT)(total - sizeof(WCHAR));
                        OutMultiVersion->MaximumLength = (USHORT)total;
                    }
                }

                if (NT_SUCCESS(st1)) RtlFreeUnicodeString(&uv);
                if (NT_SUCCESS(st2)) RtlFreeUnicodeString(&uver);
            }

            return STATUS_SUCCESS;
        }

        p = next;
    }

    return STATUS_NOT_FOUND;
}

static
NTSTATUS
CmpQuerySmbiosStrings(
    _Out_opt_ UNICODE_STRING* BiosDate,
    _Out_opt_ UNICODE_STRING* MultiVersion
)
{
    SMBIOS_TABLE_DESC desc;
    NTSTATUS Status = CmpLocateSmbiosInFSegment(&desc);
    if (!NT_SUCCESS(Status))
        return Status;

    PVOID rawBase = NULL, adj = NULL;
    SIZE_T rawSize = 0, adjLen = 0;

    Status = CmpMapSmbiosTable(&desc, &rawBase, &rawSize, &adj, &adjLen);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = CmpExtractType0BiosStrings((const UCHAR*)adj, adjLen, BiosDate, MultiVersion);

    MmUnmapIoSpace(rawBase, rawSize);
    return Status;
}

/* ========================= CPU key population ============================== */

#ifndef AFFINITY_MASK
#define AFFINITY_MASK(i) ((KAFFINITY)1 << (i))
#endif

/* Common CPU identifier string pattern */
static const CHAR CmpFullCpuID[] = "%s Family %u Model %u Stepping %u";

static
VOID
CmpBuildCpuIdentifierAmd64(
    _In_  const KPRCB* Prcb,
    _Out_writes_(outLen) PCHAR Out,
    _In_  SIZE_T outLen
)
{
    const CHAR* FamilyId;
    if (Prcb->CpuVendor == CPU_VIA)      FamilyId = "VIA64";
    else if (Prcb->CpuVendor == CPU_AMD) FamilyId = "AMD64";
    else                                 FamilyId = "EM64T";

    _snprintf(Out, outLen, CmpFullCpuID,
              FamilyId,
              Prcb->CpuType,
              (Prcb->CpuStep >> 8),
              (Prcb->CpuStep & 0xFF));
}

/* ===================== Entry: main initialization ========================== */

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    UNICODE_STRING KeyName, ValueName, Data;
    OBJECT_ATTRIBUTES oa;
    HANDLE SystemHandle = NULL;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;

    /* Open HKLM\Hardware\Description\System */
    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&oa, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &oa);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Prepare Configuration Data (shared cmconfig.c API) */
    CONFIGURATION_COMPONENT_DATA ConfigData;
    USHORT IndexTable[MaximumType + 1] = {0};

    /* Loop all processors and create CentralProcessor\N */
    for (ULONG i = 0; i < KeNumberProcessors; ++i)
    {
        CHAR idBuf[128] = {0};
        ANSI_STRING aTmp;
        UNICODE_STRING uStr;
        KPRCB* Prcb = KiProcessorBlock[i];

        /* Safety check - should not happen but better safe than sorry */
        if (!Prcb)
        {
            DPRINT1("WARNING: KiProcessorBlock[%lu] is NULL, skipping\n", i);
            continue;
        }

        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type  = CentralProcessor;
        ConfigData.ComponentEntry.Key   = i;
#if defined(_M_AMD64)
        ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(i);
#else
        ConfigData.ComponentEntry.AffinityMask = (KAFFINITY)1; /* not used here */
#endif
        ConfigData.ComponentEntry.Identifier = idBuf;

        CmpBuildCpuIdentifierAmd64(Prcb, idBuf, sizeof(idBuf));
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(idBuf) + 1;

        /* Create the registry node via config manager helper */
        Status = CmpInitializeRegistryNode(&ConfigData,
                                           SystemHandle,
                                           &KeyHandle,
                                           InterfaceTypeUndefined,
                                           0xFFFFFFFF,
                                           IndexTable);
        if (!NT_SUCCESS(Status))
        {
            if (SystemHandle) NtClose(SystemHandle);
            return Status;
        }

        /* Gather CPUID brand string (0x80000002..0x80000004) per CPU */
        CHAR CpuBrand[48] = {0};
        BOOLEAN HaveBrand = FALSE;
        CPU_INFO Info;
        ULONG maxExt;

        /* Stay on this CPU only while querying CPUID */
        KeSetSystemAffinityThread(Prcb->SetMember);
        KiCpuId(&Info, 0x80000000);
        maxExt = Info.Eax;
        if (maxExt >= 0x80000004)
        {
            CHAR* p = CpuBrand;
            for (ULONG leaf = 0x80000002; leaf <= 0x80000004; ++leaf)
            {
                KiCpuId(&Info, leaf);
                ((PULONG)p)[0] = Info.Eax;
                ((PULONG)p)[1] = Info.Ebx;
                ((PULONG)p)[2] = Info.Ecx;
                ((PULONG)p)[3] = Info.Edx;
                p += 16;
            }
            CpuBrand[47] = 0;
            HaveBrand = TRUE;
        }
        KeRevertToUserAffinityThread();

        /* ProcessorNameString */
        if (HaveBrand && CpuBrand[0])
        {
            RtlInitAnsiString(&aTmp, CpuBrand);
            if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uStr, &aTmp, TRUE)))
            {
                RtlInitUnicodeString(&ValueName, L"ProcessorNameString");
                NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ,
                              uStr.Buffer, uStr.Length + sizeof(WCHAR));

                /* Mirror for reporting */
                if (!RtlCreateUnicodeString(&KeRosProcessorName, uStr.Buffer))
                    KeRosProcessorName.Length = 0;

                RtlFreeUnicodeString(&uStr);
            }
        }

        /* VendorIdentifier */
        if (Prcb->VendorString[0])
        {
            RtlInitAnsiString(&aTmp, (PCSZ)Prcb->VendorString);
            if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uStr, &aTmp, TRUE)))
            {
                RtlInitUnicodeString(&ValueName, L"VendorIdentifier");
                NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ,
                              uStr.Buffer, uStr.Length + sizeof(WCHAR));
                RtlFreeUnicodeString(&uStr);
            }
        }

        /* FeatureSet */
        if (Prcb->FeatureBits)
        {
            RtlInitUnicodeString(&ValueName, L"FeatureSet");
            NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD,
                          &Prcb->FeatureBits, sizeof(Prcb->FeatureBits));
        }

        /* ~MHz */
        if (Prcb->MHz)
        {
            RtlInitUnicodeString(&ValueName, L"~MHz");
            NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD,
                          &Prcb->MHz, sizeof(Prcb->MHz));
        }

        /* Update Signature (microcode) */
        if (Prcb->UpdateSignature.QuadPart)
        {
            RtlInitUnicodeString(&ValueName, L"Update Signature");
            NtSetValueKey(KeyHandle, &ValueName, 0, REG_BINARY,
                          &Prcb->UpdateSignature, sizeof(Prcb->UpdateSignature));
        }

        NtClose(KeyHandle);
        KeyHandle = NULL;
    }

    /* BIOS information via SMBIOS Type 0 */
    {
        UNICODE_STRING BiosDate = {0};
        UNICODE_STRING BiosVersionMulti = {0};

        Status = CmpQuerySmbiosStrings(&BiosDate, &BiosVersionMulti);
        if (NT_SUCCESS(Status))
        {
            if (BiosDate.Buffer)
            {
                RtlInitUnicodeString(&ValueName, L"SystemBiosDate");
                NtSetValueKey(SystemHandle, &ValueName, 0, REG_SZ,
                              BiosDate.Buffer, BiosDate.Length + sizeof(WCHAR));

                /* Mirror for reporting */
                if (!RtlCreateUnicodeString(&KeRosBiosDate, BiosDate.Buffer))
                    KeRosBiosDate.Length = 0;

                RtlFreeUnicodeString(&BiosDate);
            }
            if (BiosVersionMulti.Buffer)
            {
                RtlInitUnicodeString(&ValueName, L"SystemBiosVersion");
                NtSetValueKey(SystemHandle, &ValueName, 0, REG_MULTI_SZ,
                              BiosVersionMulti.Buffer, BiosVersionMulti.Length + sizeof(WCHAR));

                /* Mirror for reporting (first string only is fine) */
                if (!RtlCreateUnicodeString(&KeRosBiosVersion, BiosVersionMulti.Buffer))
                    KeRosBiosVersion.Length = 0;

                ExFreePoolWithTag(BiosVersionMulti.Buffer, 'mbSC');
            }
        }
        else
        {
            DPRINT1("SMBIOS Type 0 not found (status 0x%08lx); BIOS keys not set.\n", Status);
        }
    }

    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}
