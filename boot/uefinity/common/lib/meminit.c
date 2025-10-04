/*
 *  FreeLoader (UEFI memory manager) — context-based, no exported globals
 *  License: GPL-2.0-or-later
 */

#include <freeldr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(MEMORY);

/*---------- Portable always-inline ----------*/
#ifndef FORCEINLINE
#  if defined(__GNUC__)
#    define FORCEINLINE static inline __attribute__((always_inline))
#  elif defined(_MSC_VER)
#    define FORCEINLINE __forceinline
#  else
#    define FORCEINLINE static inline
#  endif
#endif

/*--------------------------------- Config ----------------------------------*/

#if defined(_ARM64_) && !defined(FREELDR_UEFI_ARM64)
#define FREELDR_UEFI_ARM64 1
#endif

#ifndef FREELDR_MM_ALIGN
#define FREELDR_MM_ALIGN 16u
#endif

/*-------------------------------- Context ----------------------------------*/

typedef struct _FREELDR_MM_CONTEXT
{
    PVOID        PageLookupTableAddress;
    PFN_NUMBER   TotalPagesInLookupTable;
    PFN_NUMBER   LowestPhysicalPage;
    PFN_NUMBER   HighestPhysicalPage;

    PFREELDR_MEMORY_DESCRIPTOR BiosMemoryMap;
    ULONG        BiosMemoryMapEntryCount;
    SIZE_T       FrLdrImageSize;
} FREELDR_MM_CONTEXT, *PFREELDR_MM_CONTEXT;

static FREELDR_MM_CONTEXT g_MmCtx = {
    .PageLookupTableAddress   = NULL,
    .TotalPagesInLookupTable  = 0,
    .LowestPhysicalPage       = (PFN_NUMBER)0xFFFFFFFFu,
    .HighestPhysicalPage      = 0,
    .BiosMemoryMap            = NULL,
    .BiosMemoryMapEntryCount  = 0,
    .FrLdrImageSize           = 0,
};

/*------------------------------ Small helpers ------------------------------*/

FORCEINLINE ULONG_PTR AlignUp(ULONG_PTR v, ULONG_PTR a)
{
    return (v + (a - 1)) & ~(a - 1);
}

FORCEINLINE PFN_NUMBER PageFromAddr(PVOID Address)
{
    return ((ULONG_PTR)Address) / MM_PAGE_SIZE;
}

FORCEINLINE BOOLEAN Mul64(ULONGLONG a, ULONGLONG b, ULONGLONG* out)
{
    if (!out) return FALSE;
    if (a == 0 || b == 0) { *out = 0; return TRUE; }
    if (a > 0xFFFFFFFFFFFFFFFFull / b) return FALSE;
    *out = a * b; return TRUE;
}

/*------------------------------ Public getters -----------------------------*/
/* These replace all old globals; other modules should only use these.       */

PVOID FrLdrMmGetLookupTableAddress(void)     { return g_MmCtx.PageLookupTableAddress; }
PFN_NUMBER FrLdrMmGetTotalPages(void)        { return g_MmCtx.TotalPagesInLookupTable; }

/*------------------------------ Debug helpers ------------------------------*/

#if DBG
typedef struct { TYPE_OF_MEMORY Type; PCSTR Name; } MTBL;
static const MTBL sMemNames[] = {
    { LoaderMaximum, "Unknown" }, { LoaderFree, "Free" }, { LoaderBad, "Bad" },
    { LoaderLoadedProgram, "LoadedProgram" }, { LoaderFirmwareTemporary, "FwTemp" },
    { LoaderFirmwarePermanent, "FwPerm" }, { LoaderOsloaderHeap, "OsHeap" },
    { LoaderOsloaderStack, "OsStack" }, { LoaderSystemCode, "SystemCode" },
    { LoaderHalCode, "HalCode" }, { LoaderBootDriver, "BootDriver" },
    { LoaderRegistryData, "RegistryData" }, { LoaderMemoryData, "MemoryData" },
    { LoaderNlsData, "NlsData" }, { LoaderSpecialMemory, "Special" },
    { LoaderReserve, "Reserve" },
};
PCSTR MmGetSystemMemoryMapTypeString(TYPE_OF_MEMORY T)
{
    for (unsigned i = 1; i < sizeof(sMemNames)/sizeof(sMemNames[0]); ++i)
        if (sMemNames[i].Type == T) return sMemNames[i].Name;
    return sMemNames[0].Name;
}
#else
PCSTR MmGetSystemMemoryMapTypeString(TYPE_OF_MEMORY T) { UNREFERENCED_PARAMETER(T); return "-"; }
#endif

/*------------------------------ API preserved ------------------------------*/

ULONG
MmGetBiosMemoryMap(_Out_ PFREELDR_MEMORY_DESCRIPTOR *MemoryMap)
{
    *MemoryMap = g_MmCtx.BiosMemoryMap;
    return g_MmCtx.BiosMemoryMapEntryCount;
}

PFN_NUMBER MmGetPageNumberFromAddress(PVOID Address) { return PageFromAddr(Address); }
PFN_NUMBER MmGetHighestPhysicalPage(VOID)            { return g_MmCtx.HighestPhysicalPage; }

/*------------------------------- Internals ---------------------------------*/

/* Iterate using the entry count we own instead of relying on a sentinel. */
static inline const FREELDR_MEMORY_DESCRIPTOR*
MapAt(ULONG idx)
{
    if (!g_MmCtx.BiosMemoryMap) return NULL;
    if (idx >= g_MmCtx.BiosMemoryMapEntryCount) return NULL;
    return &g_MmCtx.BiosMemoryMap[idx];
}

static VOID
MmCheckFreeldrImageFile(VOID)
{
#if FREELDR_UEFI_ARM64
    g_MmCtx.FrLdrImageSize = 0x100000; /* 1 MiB guard */
#else
#ifndef UEFIBOOT
    PIMAGE_NT_HEADERS Nt = RtlImageNtHeader(&__ImageBase);
    if (!Nt) FrLdrBugCheckWithMessage(FREELDR_IMAGE_CORRUPTION, __FILE__, __LINE__,"No NtHeaders");
    PIMAGE_FILE_HEADER FH = &Nt->FileHeader;
    PIMAGE_OPTIONAL_HEADER OH = &Nt->OptionalHeader;
    if ((FH->Machine != IMAGE_FILE_MACHINE_NATIVE) ||
        (FH->NumberOfSections != FREELDR_SECTION_COUNT) ||
        (FH->PointerToSymbolTable != 0) || (FH->NumberOfSymbols != 0) ||
        (FH->SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER)))
        FrLdrBugCheckWithMessage(FREELDR_IMAGE_CORRUPTION, __FILE__, __LINE__,"Bad FileHeader");

    if ((OH->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) ||
        (OH->Subsystem != IMAGE_SUBSYSTEM_NATIVE) ||
        (OH->ImageBase != FREELDR_PE_BASE) ||
        (OH->SizeOfImage > MAX_FREELDR_PE_SIZE) ||
        (OH->SectionAlignment != OH->FileAlignment))
        FrLdrBugCheckWithMessage(FREELDR_IMAGE_CORRUPTION, __FILE__, __LINE__,"Bad OptionalHeader");

    g_MmCtx.FrLdrImageSize = (SIZE_T)((ULONG_PTR)&__ImageBase + OH->SizeOfImage - FREELDR_BASE);
#endif
#endif
}

static PFN_NUMBER
MmAddressablePagesIncludingHoles(VOID)
{
    for (ULONG i = 0; i < g_MmCtx.BiosMemoryMapEntryCount; ++i)
    {
        const FREELDR_MEMORY_DESCRIPTOR* md = MapAt(i);
        if (!md || md->PageCount == 0) continue;
        PFN_NUMBER end = md->BasePage + md->PageCount;
        if ((md->MemoryType == LoaderFree) && (end > g_MmCtx.HighestPhysicalPage))
            g_MmCtx.HighestPhysicalPage = end;
        if (md->BasePage < g_MmCtx.LowestPhysicalPage)
            g_MmCtx.LowestPhysicalPage = md->BasePage;
    }
    if (g_MmCtx.HighestPhysicalPage <= g_MmCtx.LowestPhysicalPage)
        FrLdrBugCheckWithMessage(MEMORY_INIT_FAILURE, __FILE__, __LINE__, "Bad phys bounds");
    return g_MmCtx.HighestPhysicalPage - g_MmCtx.LowestPhysicalPage;
}

static PVOID
MmFindLookupPlacement(PFN_NUMBER TotalPageCount)
{
    const FREELDR_MEMORY_DESCRIPTOR* md = NULL;
    SIZE_T tableBytes = (SIZE_T)TotalPageCount * sizeof(PAGE_LOOKUP_TABLE_ITEM);
    PFN_NUMBER needPages = (PFN_NUMBER)((tableBytes + MM_PAGE_SIZE - 1) / MM_PAGE_SIZE);
    PFN_NUMBER candBase = 0, candCount = 0;

    for (ULONG i = 0; i < g_MmCtx.BiosMemoryMapEntryCount; ++i)
    {
        md = MapAt(i);
        if (!md) continue;
        if (md->MemoryType != LoaderFree) continue;
        if (md->PageCount < needPages)    continue;
        if (md->BasePage < candBase)      continue;
        //TODO this line is a 64bit only
        if (md->BasePage + needPages >= MM_MAX_PAGE_LOADER) continue;

        candBase  = md->BasePage;
        candCount = md->PageCount;
    }

    if (candCount == 0) return (PVOID)-1;

    PFN_NUMBER endPage =
#if FREELDR_UEFI_ARM64
        candBase + candCount;
#else
        min(candBase + candCount, MM_MAX_PAGE_LOADER);
#endif

    ULONGLONG endAddr = 0, tableAddr = 0;
    if (!Mul64((ULONGLONG)endPage, (ULONGLONG)PAGE_SIZE, &endAddr)) return (PVOID)-1;
    if (endAddr < (ULONGLONG)tableBytes) return (PVOID)-1;
    tableAddr = endAddr - (ULONGLONG)tableBytes;

    ULONG_PTR v = (ULONG_PTR)tableAddr;
    v = AlignUp(v, FREELDR_MM_ALIGN);
    return (PVOID)v;
}

/*--------------------------- Lookup table plumbing --------------------------*/

VOID MmMarkPagesInLookupTable(PVOID Table, PFN_NUMBER StartPage, PFN_NUMBER PageCount, TYPE_OF_MEMORY Type)
{
    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;

    if ((StartPage < g_MmCtx.LowestPhysicalPage) ||
        ((StartPage + PageCount - 1) > g_MmCtx.HighestPhysicalPage))
    {
        ERR("Mark out-of-bounds %I64x:%I64x [lo=%I64x hi=%I64x]\n",
            (unsigned long long)(ULONG_PTR)StartPage,
            (unsigned long long)(ULONG_PTR)PageCount,
            (unsigned long long)(ULONG_PTR)g_MmCtx.LowestPhysicalPage,
            (unsigned long long)(ULONG_PTR)g_MmCtx.HighestPhysicalPage);
        return;
    }

    PFN_NUMBER rel = StartPage - g_MmCtx.LowestPhysicalPage;
    for (PFN_NUMBER i = 0; i < PageCount; ++i)
    {
        PFN_NUMBER idx = rel + i;
        t[idx].PageAllocated        = Type;
        t[idx].PageAllocationLength = (Type != LoaderFree) ? 1 : 0;
    }
}

VOID MmAllocatePagesInLookupTable(PVOID Table, PFN_NUMBER StartPage, PFN_NUMBER PageCount, TYPE_OF_MEMORY Type)
{
    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    PFN_NUMBER rel = StartPage - g_MmCtx.LowestPhysicalPage;
    for (PFN_NUMBER i = 0; i < PageCount; ++i)
    {
        PFN_NUMBER idx = rel + i;
        t[idx].PageAllocated        = Type;
        t[idx].PageAllocationLength = (i == 0) ? PageCount : 0;
    }
}

VOID MmFreePagesInLookupTable(PVOID Table, PFN_NUMBER StartPage, PFN_NUMBER PageCount)
{
    if (!Table || PageCount == 0) return;

    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    PFN_NUMBER rel = StartPage - g_MmCtx.LowestPhysicalPage;
    for (PFN_NUMBER i = 0; i < PageCount; ++i)
    {
        PFN_NUMBER idx = rel + i;
        t[idx].PageAllocated        = LoaderFree;
        t[idx].PageAllocationLength = 0;
    }
}

PFN_NUMBER MmCountFreePagesInLookupTable(PVOID Table, PFN_NUMBER Total)
{
    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    PFN_NUMBER freec = 0;
    for (PFN_NUMBER i = 0; i < Total; ++i)
        if (t[i].PageAllocated == LoaderFree) ++freec;
    return freec;
}

PFN_NUMBER MmFindAvailablePages(PVOID Table, PFN_NUMBER Total, PFN_NUMBER PagesNeeded, BOOLEAN FromEnd)
{
    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    PFN_NUMBER have = 0;

    if (FromEnd)
    {
        for (PFN_NUMBER i = Total - 1; i > 0; --i)
        {
            if (t[i].PageAllocated != LoaderFree) { have = 0; continue; }
            if (++have >= PagesNeeded)
                return i + g_MmCtx.LowestPhysicalPage;
        }
    }
    else
    {
        for (PFN_NUMBER i = 1; i < Total; ++i)
        {
            if (t[i].PageAllocated != LoaderFree) { have = 0; continue; }
            if (++have >= PagesNeeded)
                return i - have + 1 + g_MmCtx.LowestPhysicalPage;
        }
    }
    return 0;
}

PFN_NUMBER MmFindAvailablePagesBeforePage(PVOID Table, PFN_NUMBER Total, PFN_NUMBER PagesNeeded, PFN_NUMBER LastPage)
{
    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    if (LastPage > Total)
        return MmFindAvailablePages(Table, Total, PagesNeeded, TRUE);

    PFN_NUMBER have = 0;
    for (PFN_NUMBER i = LastPage - 1; i > 0; --i)
    {
        if (t[i].PageAllocated != LoaderFree) { have = 0; continue; }
        if (++have >= PagesNeeded)
            return i + g_MmCtx.LowestPhysicalPage;
    }
    return 0;
}

BOOLEAN MmAreMemoryPagesAvailable(PVOID Table, PFN_NUMBER Total, PVOID PageAddress, PFN_NUMBER PageCount)
{
    if (!Table || PageCount == 0) return TRUE;

    PPAGE_LOOKUP_TABLE_ITEM t = (PPAGE_LOOKUP_TABLE_ITEM)Table;
    PFN_NUMBER start = MmGetPageNumberFromAddress(PageAddress);

    if (start < g_MmCtx.LowestPhysicalPage) return FALSE;

    PFN_NUMBER rel = start - g_MmCtx.LowestPhysicalPage;

    if ((rel + PageCount) > Total) return FALSE;

    for (PFN_NUMBER i = 0; i < PageCount; ++i)
    {
        if (t[rel + i].PageAllocated != LoaderFree)
            return FALSE;
    }

    return TRUE;
}

/*------------------------------- Initialization -----------------------------*/

BOOLEAN MmInitializeMemoryManager(VOID)
{
    TRACE("MM init\n");

    /* Reset physical bounds each init */
    g_MmCtx.LowestPhysicalPage  = (PFN_NUMBER)0xFFFFFFFFu;
    g_MmCtx.HighestPhysicalPage = 0;

    MmCheckFreeldrImageFile();

    TRACE("MM: calling GetMemoryMap...\n");
    g_MmCtx.BiosMemoryMap = MachVtbl.GetMemoryMap(&g_MmCtx.BiosMemoryMapEntryCount);
    TRACE("MM: map @%p entries=%lu\n", g_MmCtx.BiosMemoryMap, (unsigned long)g_MmCtx.BiosMemoryMapEntryCount);
    if (!g_MmCtx.BiosMemoryMap || g_MmCtx.BiosMemoryMapEntryCount == 0)
    {
        UiMessageBoxCritical("Unable to fetch firmware memory map");
        return FALSE;
    }

    g_MmCtx.TotalPagesInLookupTable = MmAddressablePagesIncludingHoles();

    g_MmCtx.PageLookupTableAddress = MmFindLookupPlacement(g_MmCtx.TotalPagesInLookupTable);
    if (!g_MmCtx.PageLookupTableAddress || g_MmCtx.PageLookupTableAddress == (PVOID)-1)
    {
        printf("Error: cannot find memory for LUT\n");
        return FALSE;
    }

    /* Initialize LUT: pessimistically reserve everything, then replay map */
    {
        PPAGE_LOOKUP_TABLE_ITEM tbl = (PPAGE_LOOKUP_TABLE_ITEM)g_MmCtx.PageLookupTableAddress;
        PFN_NUMBER total = g_MmCtx.TotalPagesInLookupTable;

        MmMarkPagesInLookupTable(tbl, g_MmCtx.LowestPhysicalPage, total, LoaderFirmwarePermanent);

        for (ULONG i = 0; i < g_MmCtx.BiosMemoryMapEntryCount; ++i)
        {
            const FREELDR_MEMORY_DESCRIPTOR* md = MapAt(i);
            if (!md || md->PageCount == 0) continue;
            PFN_NUMBER end = md->BasePage + md->PageCount;
            if (end > g_MmCtx.HighestPhysicalPage) continue;
            MmMarkPagesInLookupTable(tbl, md->BasePage, md->PageCount, md->MemoryType);
        }

        /* Reserve the LUT itself */
        PFN_NUMBER start = PageFromAddr(tbl);
        ULONG_PTR  bytes = AlignUp((ULONG_PTR)total * sizeof(PAGE_LOOKUP_TABLE_ITEM), MM_PAGE_SIZE);
        PFN_NUMBER pages = PageFromAddr((PVOID)((ULONG_PTR)tbl + bytes)) - start;
        MmMarkPagesInLookupTable(tbl, start, pages, LoaderFirmwareTemporary);
    }

    return TRUE;
}

/*------------------------ Legacy descriptor helpers ------------------------*/

ULONG
AddMemoryDescriptor(
    IN OUT PFREELDR_MEMORY_DESCRIPTOR List,
    IN ULONG MaxCount,
    IN PFN_NUMBER BasePage,
    IN PFN_NUMBER PageCount,
    IN TYPE_OF_MEMORY MemoryType)
{
    ULONG Index = 0, DescriptCount;
    PFN_NUMBER EndPage = BasePage + PageCount;

    TRACE("AddMemoryDescriptor(0x%I64x, 0x%I64x, %u)\n",
          (unsigned long long)BasePage, (unsigned long long)PageCount, (unsigned)MemoryType);

    while ((List[Index].PageCount != 0) &&
           ((List[Index].BasePage + List[Index].PageCount) <= BasePage))
    {
        Index++;
    }

    DescriptCount = Index;
    while (List[DescriptCount].PageCount != 0)
        DescriptCount++;

    while ((List[Index].PageCount != 0) &&
           (List[Index].BasePage < EndPage))
    {
        TRACE("AddMemoryDescriptor conflict @%u: new=[%I64x:%I64x], existing=[%I64x,%I64x]\n",
              (unsigned)Index, (unsigned long long)BasePage, (unsigned long long)PageCount, (unsigned long long)List[Index].BasePage, (unsigned long long)List[Index].PageCount);

        if (List[Index].BasePage < BasePage)
        {
            if (List[Index].BasePage + List[Index].PageCount > EndPage)
            {
                RtlMoveMemory(&List[Index + 1],
                              &List[Index],
                              (DescriptCount - Index) * sizeof(List[0]));
                List[Index + 1].BasePage = EndPage;
                List[Index + 1].PageCount = List[Index].BasePage +
                                            List[Index].PageCount -
                                            List[Index + 1].BasePage;
                List[Index].PageCount = BasePage - List[Index].BasePage;
                Index++;
                DescriptCount++;
                break;
            }
            else
            {
                List[Index].PageCount = BasePage - List[Index].BasePage;
                Index++;
            }
        }
        else if ((List[Index].BasePage + List[Index].PageCount) <= EndPage)
        {
            RtlMoveMemory(&List[Index],
                          &List[Index + 1],
                          (DescriptCount - Index) * sizeof(List[0]));
            DescriptCount--;
        }
        else
        {
            List[Index].PageCount -= EndPage - List[Index].BasePage;
            List[Index].BasePage = EndPage;
            break;
        }
    }

    if (DescriptCount >= MaxCount)
    {
        FrLdrBugCheckWithMessage(
            MEMORY_INIT_FAILURE,
            __FILE__,
            __LINE__,
            "Ran out of static memory descriptors!");
    }

    if (Index < DescriptCount)
    {
        RtlMoveMemory(&List[Index + 1],
                      &List[Index],
                      (DescriptCount - Index) * sizeof(List[0]));
    }

    List[Index].BasePage  = BasePage;
    List[Index].PageCount = PageCount;
    List[Index].MemoryType = MemoryType;
    DescriptCount++;

    return DescriptCount;
}
