/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI backtrace helpers (module+offset formatting) for AMD64/ARM64
 */

#include <uefildr.h>
#include <debug.h>
#include <ntimage.h>


DBG_DEFAULT_CHANNEL(WARNING);

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* From UEFI memory init (for freeldr base/size) */
extern PVOID  OsLoaderBase;
extern SIZE_T OsLoaderSize;

typedef struct _UEFI_IMAGE_ENTRY {
    ULONG_PTR Base;
    SIZE_T    Size;
    UINTN     Index; /* enumeration index */
} UEFI_IMAGE_ENTRY;

static UEFI_IMAGE_ENTRY* gImageList = NULL;
static UINTN             gImageCount = 0;
static const CHAR*       gSymLite    = NULL;
static SIZE_T            gSymLiteSize= 0;

extern char __ImageBase;

static VOID
UefiInitSymLite(VOID)
{
    if (gSymLite) return;
    PIMAGE_NT_HEADERS Nt = RtlImageNtHeader(&__ImageBase);
    if (!Nt) return;
    PIMAGE_SECTION_HEADER Sec = IMAGE_FIRST_SECTION(Nt);
    for (UINT32 i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Sec)
    {
        CHAR name[9]; name[8] = '\0';
        RtlCopyMemory(name, Sec->Name, 8);
        if (strcmp(name, ".symlite") == 0)
        {
            ULONG_PTR base = (ULONG_PTR)&__ImageBase;
            gSymLite = (const CHAR*)(base + Sec->VirtualAddress);
            gSymLiteSize = (SIZE_T)Sec->Misc.VirtualSize;
            break;
        }
    }
}

static BOOLEAN
UefiLookupSymbolName(ULONG_PTR Address, CHAR* Out, SIZE_T OutLen, ULONG_PTR* SymStart)
{
    if (!Out || OutLen == 0) return FALSE;
    *Out = '\0';
    if (!gSymLite || gSymLiteSize == 0) return FALSE;

    /* Compute RVA relative to our loaded base */
    ULONG_PTR base = (ULONG_PTR)OsLoaderBase;
    if (base == 0 || Address < base) return FALSE;
    ULONG_PTR rva = Address - base;

    const CHAR* p = gSymLite;
    const CHAR* end = gSymLite + gSymLiteSize;
    ULONG_PTR last_addr = 0;
    const CHAR* last_name = NULL;
    SIZE_T last_name_len = 0;

    while (p < end)
    {
        /* parse hex address */
        ULONG_PTR val = 0;
        const CHAR* q = p;
        while (q < end)
        {
            CHAR c = *q;
            if (c == ' ' || c == '\t') break;
            if      (c >= '0' && c <= '9') val = (val << 4) | (ULONG_PTR)(c - '0');
            else if (c >= 'a' && c <= 'f') val = (val << 4) | (ULONG_PTR)(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') val = (val << 4) | (ULONG_PTR)(10 + c - 'A');
            else { val = 0; break; }
            ++q;
        }
        /* skip spaces */
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        const CHAR* name = q;
        while (q < end && *q != '\r' && *q != '\n' && *q != '\0') ++q;
        SIZE_T name_len = (SIZE_T)(q - name);

        if (val > rva) break;
        last_addr = val;
        last_name = name;
        last_name_len = name_len;

        /* next line */
        while (q < end && (*q == '\r' || *q == '\n')) ++q;
        p = q;
    }

    if (!last_name || last_name_len == 0) return FALSE;
    if (last_name_len >= OutLen) last_name_len = OutLen - 1;
    RtlCopyMemory(Out, last_name, last_name_len);
    Out[last_name_len] = '\0';
    if (SymStart) *SymStart = base + last_addr;
    return TRUE;
}

static VOID
FormatAddressWithModule(ULONG_PTR Address)
{
    ULONG_PTR base = (ULONG_PTR)OsLoaderBase;
    ULONG_PTR end  = base + (ULONG_PTR)OsLoaderSize;

    if (base != 0 && OsLoaderSize != 0 && Address >= base && Address < end)
    {
        /* Try symbol lookup first */
        UefiInitSymLite();
        CHAR name[96]; ULONG_PTR symStart = 0;
        if (UefiLookupSymbolName(Address, name, sizeof(name), &symStart))
        {
            SIZE_T off = (SIZE_T)(Address - symStart);
            DbgPrint("  %p %s+0x%Ix\n", (PVOID)Address, name, off);
        }
        else
        {
            DbgPrint("  %p freeldr+0x%Ix\n", (PVOID)Address, (SIZE_T)(Address - base));
        }
        return;
    }

    /* Search other loaded images we captured */
    for (UINTN i = 0; i < gImageCount; ++i)
    {
        ULONG_PTR mbase = gImageList[i].Base;
        ULONG_PTR mend  = mbase + (ULONG_PTR)gImageList[i].Size;
        if (Address >= mbase && Address < mend)
        {
            DbgPrint("  %p (image#%u base=%p+0x%Ix)\n", (PVOID)Address,
                     (unsigned)gImageList[i].Index, (PVOID)mbase,
                     (SIZE_T)(Address - mbase));
            return;
        }
    }

    /* Best-effort: print raw address if module not known */
    DbgPrint("  %p\n", (PVOID)Address);
}

/* Print backtrace using ARM64 frame pointer chain (x29) */
VOID
UefiArm64PrintBacktrace(ULONG_PTR FramePointer, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    ULONG frames = 0;
    const ULONG max_frames = 32;

    ERR("Backtrace (ARM64):\n");

    /* Walk [prev_fp, lr] chain while staying within [StackBottom, StackTop) */
    while (frames < max_frames)
    {
        /* Basic sanity and bounds checks */
        if ((FramePointer & 0xF) != 0) break;           /* 16-byte alignment */
        if (FramePointer < StackBottom || FramePointer + 16 > StackTop) break;

        ULONG_PTR* slot = (ULONG_PTR*)FramePointer;
        ULONG_PTR next_fp = slot[0];
        ULONG_PTR lr      = slot[1];

        if (lr == 0 || next_fp <= FramePointer) break;

        FormatAddressWithModule(lr);
        FramePointer = next_fp;
        frames++;
    }
}

/* Print backtrace using AMD64 frame pointer chain (rbp) */
VOID
UefiAmd64PrintBacktrace(ULONG_PTR Rbp, ULONG_PTR StackTop, ULONG_PTR StackBottom)
{
    ULONG frames = 0;
    const ULONG max_frames = 32;

    DbgPrint("Backtrace:\n");

    while (frames < max_frames)
    {
        if ((Rbp & 0xF) != 0) {
            break;
        }
        if (Rbp < StackBottom || Rbp + 16 > StackTop) {
            break;
        }

        ULONG_PTR* slot = (ULONG_PTR*)Rbp;
        ULONG_PTR next_rbp = slot[0];
        ULONG_PTR ret_addr = slot[1];

        if (ret_addr == 0 || next_rbp <= Rbp) {
            break;
        }

        FormatAddressWithModule(ret_addr);
        Rbp = next_rbp;
        frames++;
    }

    (void)frames;
}

/* Optional: initialize any image info. For now rely on OsLoaderBase/Size. */
VOID
UefiInitializeDebugImageInfo(VOID)
{
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return;

    EFI_BOOT_SERVICES* Bs = GlobalSystemTable->BootServices;
    EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_HANDLE* Handles = NULL;
    UINTN HandleCount = 0;

    EFI_STATUS Status = Bs->LocateHandleBuffer(ByProtocol,
                                               &LoadedImageGuid,
                                               NULL,
                                               &HandleCount,
                                               &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return;

    /* Allocate local image list */
    gImageList = (UEFI_IMAGE_ENTRY*)NULL;
    Status = Bs->AllocatePool(EfiLoaderData,
                              HandleCount * sizeof(*gImageList),
                              (VOID**)&gImageList);
    if (EFI_ERROR(Status) || !gImageList)
        goto Done;

    gImageCount = 0;
    for (UINTN i = 0; i < HandleCount; ++i)
    {
        EFI_LOADED_IMAGE_PROTOCOL* Image = NULL;
        Status = Bs->HandleProtocol(Handles[i], &LoadedImageGuid, (VOID**)&Image);
        if (EFI_ERROR(Status) || !Image)
            continue;

        gImageList[gImageCount].Base  = (ULONG_PTR)Image->ImageBase;
        gImageList[gImageCount].Size  = (SIZE_T)Image->ImageSize;
        gImageList[gImageCount].Index = i;
        gImageCount++;
    }

    TRACE("[GDB] Loading gImageList\n");
    
    for (UINTN i = 0; i < gImageCount; ++i)
    {
        TRACE("[GDB] image#%u base=%p size=0x%Ix\n",
                 (unsigned)gImageList[i].Index,
                 (PVOID)gImageList[i].Base,
                 (SIZE_T)gImageList[i].Size);
    }

Done:
    if (Handles)
        Bs->FreePool(Handles);
}
VOID
UefiPrintAddressWithSymbol(ULONG_PTR Address)
{
    FormatAddressWithModule(Address);
}
