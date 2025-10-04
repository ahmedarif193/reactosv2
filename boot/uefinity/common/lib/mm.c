/*
 *  FreeLoader — Page allocator (context friendly)
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

/*---------------- External LUT helpers (from uefimem.c) --------------------*/
extern PVOID       FrLdrMmGetLookupTableAddress(void);
extern PFN_NUMBER  FrLdrMmGetTotalPages(void);

extern PFN_NUMBER  MmGetPageNumberFromAddress(PVOID Address);
extern PFN_NUMBER  MmFindAvailablePages(PVOID PageLookupTable,
                                        PFN_NUMBER TotalPageCount,
                                        PFN_NUMBER PagesNeeded,
                                        BOOLEAN FromEnd);
extern PFN_NUMBER  MmFindAvailablePagesBeforePage(PVOID PageLookupTable,
                                                  PFN_NUMBER TotalPageCount,
                                                  PFN_NUMBER PagesNeeded,
                                                  PFN_NUMBER LastPage);
extern BOOLEAN     MmAreMemoryPagesAvailable(PVOID PageLookupTable,
                                             PFN_NUMBER TotalPageCount,
                                             PVOID PageAddress,
                                             PFN_NUMBER PageCount);
extern VOID        MmAllocatePagesInLookupTable(PVOID PageLookupTable,
                                                PFN_NUMBER StartPage,
                                                PFN_NUMBER PageCount,
                                                TYPE_OF_MEMORY MemoryType);
extern VOID        MmFreePagesInLookupTable(PVOID PageLookupTable,
                                            PFN_NUMBER StartPage,
                                            PFN_NUMBER PageCount);
extern PFN_NUMBER  MmCountFreePagesInLookupTable(PVOID PageLookupTable,
                                                 PFN_NUMBER TotalPageCount);

/*---------------- Local ----------------*/
typedef PPAGE_LOOKUP_TABLE_ITEM LUTPTR;

/* Private tracker; exposed (and shared) via LoaderPagesSpanned symbol */
ULONG LoaderPagesSpanned = 0;

/*---------------- Small utils ----------------*/

FORCEINLINE SIZE_T RoundUp(SIZE_T v, SIZE_T a) { return (v + (a - 1)) & ~(a - 1); }

FORCEINLINE BOOLEAN AddChkUPtr(ULONG_PTR a, ULONG_PTR b, ULONG_PTR* out)
{
    if (!out) return FALSE;
    ULONG_PTR s = a + b;
    if (s < a) return FALSE;
    *out = s; return TRUE;
}

FORCEINLINE BOOLEAN MulChkUPtr(ULONG_PTR a, ULONG_PTR b, ULONG_PTR* out)
{
    if (!out) return FALSE;
#if defined(__x86_64__) || defined(__aarch64__) || defined(_WIN64)
    __uint128_t p = (__uint128_t)a * (__uint128_t)b;
    if ((p >> (sizeof(ULONG_PTR)*8)) != 0) return FALSE;
    *out = (ULONG_PTR)p; return TRUE;
#else
    if (a && b > (~(ULONG_PTR)0)/a) return FALSE;
    *out = a*b; return TRUE;
#endif
}

FORCEINLINE BOOLEAN GetLut(LUTPTR* outTbl, PFN_NUMBER* outTotal)
{
    if (!outTbl || !outTotal) return FALSE;
    *outTbl  = (LUTPTR)FrLdrMmGetLookupTableAddress();
    *outTotal = FrLdrMmGetTotalPages();
    return (*outTbl != NULL) && (*outTotal != 0);
}

/* Free a run whose head is at StartPage (uses LUT head span convention) */
static VOID FreeRunInLookupTable(PVOID Table, PFN_NUMBER StartPage, PFN_NUMBER PageCount)
{
    MmFreePagesInLookupTable(Table, StartPage, PageCount);
}

/*---------------- Public API ----------------*/

PVOID MmAllocateMemoryWithType(SIZE_T MemorySize, TYPE_OF_MEMORY MemoryType)
{
    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total))
    {
        UiMessageBoxCritical("Memory allocation failed: LUT not initialized.");
        return NULL;
    }

    if (MemorySize == 0)
    {
        UiMessageBoxCritical("Memory allocation failed: 0 bytes requested.");
        return NULL;
    }

    MemorySize = RoundUp(MemorySize, 4);
    ULONG_PTR needBytes;
    if (!MulChkUPtr((ULONG_PTR)RoundUp(MemorySize, MM_PAGE_SIZE), 1, &needBytes))
    {
        UiMessageBoxCritical("Memory allocation failed: size overflow.");
        return NULL;
    }

    PFN_NUMBER pagesNeeded = (PFN_NUMBER)(needBytes / MM_PAGE_SIZE);

    PFN_NUMBER freeNow = MmCountFreePagesInLookupTable(tbl, total);
    if (freeNow < pagesNeeded)
    {
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    PFN_NUMBER first = MmFindAvailablePages(tbl, total, pagesNeeded, FALSE);
    if (first == 0)
    {
        UiMessageBoxCritical("Memory allocation failed: out of memory.");
        return NULL;
    }

    MmAllocatePagesInLookupTable(tbl, first, pagesNeeded, MemoryType);

    ULONG_PTR base;
    if (!MulChkUPtr((ULONG_PTR)first, (ULONG_PTR)MM_PAGE_SIZE, &base))
    {
        FreeRunInLookupTable(tbl, first, pagesNeeded);
        UiMessageBoxCritical("Memory allocation failed: address overflow.");
        return NULL;
    }

    PVOID ptr = (PVOID)base;

    TRACE("Alloc %lu bytes (%lu pages) type=%ld PFN=0x%lx -> %p\n",
          (unsigned long)MemorySize, (unsigned long)pagesNeeded,
          (long)MemoryType, (unsigned long)first, ptr);

    ULONG_PTR end;
    if (AddChkUPtr((ULONG_PTR)ptr, (ULONG_PTR)MemorySize, &end))
    {
        PFN_NUMBER endPage = (PFN_NUMBER)((end + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (endPage > LoaderPagesSpanned) LoaderPagesSpanned = (ULONG)endPage;
    }

    return ptr;
}

PVOID MmAllocateMemoryAtAddress(SIZE_T MemorySize, PVOID DesiredAddress, TYPE_OF_MEMORY MemoryType)
{
    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total)) return NULL;
    if (MemorySize == 0) return NULL;

    PFN_NUMBER pagesNeeded = (PFN_NUMBER)(RoundUp(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE);
    PFN_NUMBER startPage   = MmGetPageNumberFromAddress(DesiredAddress);

    PFN_NUMBER freeNow = MmCountFreePagesInLookupTable(tbl, total);
    if (freeNow < pagesNeeded) return NULL;

    if (!MmAreMemoryPagesAvailable(tbl, total, DesiredAddress, pagesNeeded))
        return NULL;

    MmAllocatePagesInLookupTable(tbl, startPage, pagesNeeded, MemoryType);

    PVOID ptr = (PVOID)((ULONG_PTR)startPage * MM_PAGE_SIZE);

    TRACE("AllocAt %lu bytes (%lu pages) type=%ld PFN=0x%lx -> %p\n",
          (unsigned long)MemorySize, (unsigned long)pagesNeeded,
          (long)MemoryType, (unsigned long)startPage, ptr);

    ULONG_PTR end;
    if (AddChkUPtr((ULONG_PTR)ptr, (ULONG_PTR)MemorySize, &end))
    {
        PFN_NUMBER endPage = (PFN_NUMBER)((end + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (endPage > LoaderPagesSpanned) LoaderPagesSpanned = (ULONG)endPage;
    }

    return ptr;
}

VOID MmSetMemoryType(PVOID MemoryAddress, SIZE_T MemorySize, TYPE_OF_MEMORY NewType)
{
    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total)) return;

    PFN_NUMBER pagesNeeded = (PFN_NUMBER)(RoundUp(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE);
    PFN_NUMBER startPage   = MmGetPageNumberFromAddress(MemoryAddress);

    MmAllocatePagesInLookupTable(tbl, startPage, pagesNeeded, NewType);
}

PVOID MmAllocateHighestMemoryBelowAddress(SIZE_T MemorySize, PVOID DesiredAddress, TYPE_OF_MEMORY MemoryType)
{
    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total)) return NULL;
    if (MemorySize == 0) return NULL;

    PFN_NUMBER pagesNeeded = (PFN_NUMBER)(RoundUp(MemorySize, MM_PAGE_SIZE) / MM_PAGE_SIZE);
    PFN_NUMBER desiredPFN  = (PFN_NUMBER)((ULONG_PTR)DesiredAddress / MM_PAGE_SIZE);

    PFN_NUMBER freeNow = MmCountFreePagesInLookupTable(tbl, total);
    if (freeNow < pagesNeeded) return NULL;

    PFN_NUMBER first = MmFindAvailablePagesBeforePage(tbl, total, pagesNeeded, desiredPFN);
    if (first == 0) return NULL;

    MmAllocatePagesInLookupTable(tbl, first, pagesNeeded, MemoryType);

    PVOID ptr = (PVOID)((ULONG_PTR)first * MM_PAGE_SIZE);

    TRACE("AllocBelow %lu bytes (%lu pages) type=%ld PFN=0x%lx -> %p\n",
          (unsigned long)MemorySize, (unsigned long)pagesNeeded,
          (long)MemoryType, (unsigned long)first, ptr);

    ULONG_PTR end;
    if (AddChkUPtr((ULONG_PTR)ptr, (ULONG_PTR)MemorySize, &end))
    {
        PFN_NUMBER endPage = (PFN_NUMBER)((end + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (endPage > LoaderPagesSpanned) LoaderPagesSpanned = (ULONG)endPage;
    }

    return ptr;
}

VOID MmFreeMemory(PVOID MemoryPointer)
{
    if (!MemoryPointer) return;

    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total)) return;

    PFN_NUMBER startPFN = MmGetPageNumberFromAddress(MemoryPointer);
    if (startPFN >= total)
    {
        ERR("MmFreeMemory: PFN 0x%lx out of range\n", (unsigned long)startPFN);
        return;
    }

    PFN_NUMBER span = tbl[startPFN].PageAllocationLength;
    if (span == 0)
    {
        WARN("MmFreeMemory: %p is not an allocation head\n", MemoryPointer);
        return;
    }

    FreeRunInLookupTable(tbl, startPFN, span);
}

/*---------------- Debug ----------------*/

#if DBG
VOID DumpMemoryAllocMap(VOID)
{
    LUTPTR tbl; PFN_NUMBER total;
    if (!GetLut(&tbl, &total)) { DbgPrint("LUT not initialized\n"); return; }

    DbgPrint("----------- Memory Allocation Bitmap -----------\n");

    for (PFN_NUMBER i = 0; i < total; ++i)
    {
        if ((i % 32) == 0)
        {
            DbgPrint("\n");
            DbgPrint("%08x:\t", (unsigned int)(i * MM_PAGE_SIZE));
        }
        else if ((i % 4) == 0)
        {
            DbgPrint(" ");
        }

        switch (tbl[i].PageAllocated)
        {
            case LoaderFree:              DbgPrint("*"); break;
            case LoaderBad:               DbgPrint("-"); break;
            case LoaderLoadedProgram:     DbgPrint("O"); break;
            case LoaderFirmwareTemporary: DbgPrint("T"); break;
            case LoaderFirmwarePermanent: DbgPrint("P"); break;
            case LoaderOsloaderHeap:      DbgPrint("H"); break;
            case LoaderOsloaderStack:     DbgPrint("S"); break;
            case LoaderSystemCode:        DbgPrint("K"); break;
            case LoaderHalCode:           DbgPrint("L"); break;
            case LoaderBootDriver:        DbgPrint("B"); break;
            case LoaderStartupPcrPage:    DbgPrint("G"); break;
            case LoaderRegistryData:      DbgPrint("R"); break;
            case LoaderMemoryData:        DbgPrint("M"); break;
            case LoaderNlsData:           DbgPrint("N"); break;
            case LoaderSpecialMemory:     DbgPrint("C"); break;
            default:                      DbgPrint("?"); break;
        }
    }
    DbgPrint("\n");
}
#endif

/*---------------- Legacy query kept as function ----------------*/

PPAGE_LOOKUP_TABLE_ITEM MmGetMemoryMap(PFN_NUMBER *NoEntries)
{
    if (!NoEntries) return NULL;
    *NoEntries = FrLdrMmGetTotalPages();
    return (PPAGE_LOOKUP_TABLE_ITEM)FrLdrMmGetLookupTableAddress();
}

PFN_NUMBER MmGetLoaderPagesSpanned(VOID) { return (PFN_NUMBER)LoaderPagesSpanned; }
