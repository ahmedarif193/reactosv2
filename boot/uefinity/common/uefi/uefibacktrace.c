/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI backtrace helpers (module+offset formatting) for AMD64/ARM64
 */

#include <uefildr.h>
#include <debug.h>


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

static VOID
FormatAddressWithModule(ULONG_PTR Address)
{
    ULONG_PTR base = (ULONG_PTR)OsLoaderBase;
    ULONG_PTR end  = base + (ULONG_PTR)OsLoaderSize;

    if (base != 0 && OsLoaderSize != 0 && Address >= base && Address < end)
    {
        ERR("    %p (freeldr+0x%Ix)\n", (PVOID)Address, (SIZE_T)(Address - base));
        return;
    }

    /* Search other loaded images we captured */
    for (UINTN i = 0; i < gImageCount; ++i)
    {
        ULONG_PTR mbase = gImageList[i].Base;
        ULONG_PTR mend  = mbase + (ULONG_PTR)gImageList[i].Size;
        if (Address >= mbase && Address < mend)
        {
            ERR("    %p (image#%u+0x%Ix)\n", (PVOID)Address, (unsigned)gImageList[i].Index,
                (SIZE_T)(Address - mbase));
            return;
        }
    }

    /* Best-effort: print raw address if module not known */
    ERR("    %p\n", (PVOID)Address);
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

    ERR("Backtrace (AMD64):\n");

    while (frames < max_frames)
    {
        if ((Rbp & 0xF) != 0) break;                      /* 16-byte alignment */
        if (Rbp < StackBottom || Rbp + 16 > StackTop) break;

        ULONG_PTR* slot = (ULONG_PTR*)Rbp;
        ULONG_PTR next_rbp = slot[0];
        ULONG_PTR ret_addr = slot[1];

        if (ret_addr == 0 || next_rbp <= Rbp) break;

        FormatAddressWithModule(ret_addr);
        Rbp = next_rbp;
        frames++;
    }
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

Done:
    if (Handles)
        Bs->FreePool(Handles);
}
