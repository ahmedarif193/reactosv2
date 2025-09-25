/*
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 unwind support
 * FILE:            lib/rtl/arm64/unwind.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* ARM64-specific type definitions for non-volatile context pointers */
#ifdef _M_ARM64

/* ARM64 Non-volatile Context Pointers */
typedef struct _KNONVOLATILE_CONTEXT_POINTERS_ARM64 {
    PULONG64 X19;
    PULONG64 X20;
    PULONG64 X21;
    PULONG64 X22;
    PULONG64 X23;
    PULONG64 X24;
    PULONG64 X25;
    PULONG64 X26;
    PULONG64 X27;
    PULONG64 X28;
    PULONG64 Fp;
    PULONG64 Lr;
    PULONG64 D8;
    PULONG64 D9;
    PULONG64 D10;
    PULONG64 D11;
    PULONG64 D12;
    PULONG64 D13;
    PULONG64 D14;
    PULONG64 D15;
} KNONVOLATILE_CONTEXT_POINTERS_ARM64, *PKNONVOLATILE_CONTEXT_POINTERS_ARM64;

/* Define generic name for ARM64 - conditionally to avoid conflicts */
#ifndef KNONVOLATILE_CONTEXT_POINTERS
typedef KNONVOLATILE_CONTEXT_POINTERS_ARM64 KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;
#endif

/* Unwind History Table Entry */
#ifndef UNWIND_HISTORY_TABLE_ENTRY
typedef struct _UNWIND_HISTORY_TABLE_ENTRY
{
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
} UNWIND_HISTORY_TABLE_ENTRY, *PUNWIND_HISTORY_TABLE_ENTRY;
#endif

/* Unwind History Table */
#ifndef UNWIND_HISTORY_TABLE_SIZE
#define UNWIND_HISTORY_TABLE_SIZE 12
typedef struct _UNWIND_HISTORY_TABLE
{
    ULONG Count;
    UCHAR LocalHint;
    UCHAR GlobalHint;
    UCHAR Search;
    UCHAR Once;
    ULONG64 LowAddress;
    ULONG64 HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[UNWIND_HISTORY_TABLE_SIZE];
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;
#endif

#endif /* _M_ARM64 */

/* ARM64 Exception Flags */
#define UNW_FLAG_NHANDLER       0x00
#define UNW_FLAG_EHANDLER       0x01
#define UNW_FLAG_UHANDLER       0x02
#define UNW_FLAG_CHAININFO      0x04

/* FUNCTIONS *****************************************************************/

/*
 * @brief Locates the RUNTIME_FUNCTION entry for a given code address
 *
 * This function searches the .pdata section of the image containing the
 * specified code address to find the corresponding RUNTIME_FUNCTION entry.
 *
 * @param ControlPc Code address to find function entry for
 * @param ImageBase Receives the base address of the containing image
 * @param HistoryTable Optional unwind history table (not implemented)
 * @return Pointer to RUNTIME_FUNCTION entry or NULL if not found
 */
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_opt_ PVOID HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableLength;
    ULONG IndexLo, IndexHi, IndexMid;
    PVOID Table;
    ULONG Size;

    DPRINT("RtlLookupFunctionEntry: ControlPc = 0x%p\n", (PVOID)ControlPc);

    /* Find corresponding file header from code address */
    if (!RtlPcToFileHeader((PVOID)ControlPc, (PVOID*)ImageBase))
    {
        DPRINT("RtlLookupFunctionEntry: No file header found for PC 0x%p\n", (PVOID)ControlPc);
        return NULL;
    }

    /* Locate the exception directory (.pdata section) */
    Table = RtlImageDirectoryEntryToData((PVOID)*ImageBase,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                         &Size);
    if (!Table || Size == 0)
    {
        DPRINT("RtlLookupFunctionEntry: No exception directory found in image 0x%p\n", (PVOID)*ImageBase);
        return NULL;
    }

    FunctionTable = (PRUNTIME_FUNCTION)Table;
    TableLength = Size / sizeof(RUNTIME_FUNCTION);

    DPRINT("RtlLookupFunctionEntry: Found exception table at 0x%p with %u entries\n", Table, TableLength);

    if (TableLength == 0)
    {
        return NULL;
    }

    /* Use relative virtual address for comparison */
    ControlPc -= *ImageBase;

    /* Perform binary search in the function table */
    IndexLo = 0;
    IndexHi = TableLength;
    while (IndexHi > IndexLo)
    {
        IndexMid = (IndexLo + IndexHi) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        /* ARM64 function entries don't have EndAddress - calculate it */
        ULONG FunctionLength;
        ULONG EndAddress;

        /* Check if this is packed unwind data (bit 0 clear) or pointer to .xdata */
        if ((FunctionEntry->UnwindData & 1) == 0)
        {
            /* Packed unwind data - extract function length from bits 1-11 */
            FunctionLength = (FunctionEntry->UnwindData >> 1) & 0x7FF; /* Function length / 4 */
            EndAddress = FunctionEntry->BeginAddress + (FunctionLength * 4);
        }
        else
        {
            /* Extended unwind data - need to read .xdata section */
            PULONG UnwindInfo = (PULONG)(*ImageBase + (FunctionEntry->UnwindData & ~1));
            if (UnwindInfo != NULL)
            {
                /* Function length is in bits 0-17 of the first DWORD */
                FunctionLength = (*UnwindInfo) & 0x3FFFF; /* Function length / 4 */
                EndAddress = FunctionEntry->BeginAddress + (FunctionLength * 4);
            }
            else
            {
                /* Fallback - assume minimum function size */
                EndAddress = FunctionEntry->BeginAddress + 4;
            }
        }

        if (ControlPc < FunctionEntry->BeginAddress)
        {
            /* Continue search in lower half */
            IndexHi = IndexMid;
        }
        else if (ControlPc >= EndAddress)
        {
            /* Continue search in upper half */
            IndexLo = IndexMid + 1;
        }
        else
        {
            /* ControlPc is within function bounds */
            DPRINT("RtlLookupFunctionEntry: Found function entry at index %u (BeginAddress=0x%x, EndAddress=0x%x)\n",
                   IndexMid, FunctionEntry->BeginAddress, EndAddress);
            return FunctionEntry;
        }
    }

    /* No matching function entry found */
    DPRINT("RtlLookupFunctionEntry: No function entry found for PC 0x%p (RVA 0x%x)\n",
           (PVOID)(ControlPc + *ImageBase), (ULONG)ControlPc);
    return NULL;
}

/*
 * @brief Performs virtual unwinding of a single stack frame
 *
 * This function uses the unwind data associated with a function to virtually
 * unwind one frame from the call stack, updating the processor context to
 * reflect the state at the calling frame.
 *
 * @param HandlerType Type of handlers to consider during unwinding
 * @param ImageBase Base address of the image containing the function
 * @param ControlPc Current instruction pointer
 * @param FunctionEntry Function entry describing the current function
 * @param ContextRecord Processor context to update
 * @param HandlerData Receives handler-specific data
 * @param EstablisherFrame Receives the establisher frame pointer
 * @param ContextPointers Optional context pointers structure
 * @return Exception handler routine or NULL
 */
PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ DWORD64 ImageBase,
    _In_ DWORD64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PDWORD64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    ULONG_PTR ControlRva;
    PUCHAR UnwindCodes;
    ULONG UnwindData;
    BOOLEAN HasHandler = FALSE;
    ULONG HandlerRva = 0;

    DPRINT("RtlVirtualUnwind: HandlerType=%u, ImageBase=0x%p, ControlPc=0x%p\n",
           HandlerType, (PVOID)ImageBase, (PVOID)ControlPc);

    /* Initialize output parameters */
    *HandlerData = NULL;
    *EstablisherFrame = 0;

    /* Validate that ControlPc is within the function bounds */
    ControlRva = ControlPc - ImageBase;

    /* Calculate function end address for ARM64 */
    ULONG FunctionLength;
    ULONG EndAddress;

    /* Check if this is packed unwind data (bit 0 clear) or pointer to .xdata */
    if ((FunctionEntry->UnwindData & 1) == 0)
    {
        /* Packed unwind data - extract function length from bits 1-11 */
        FunctionLength = (FunctionEntry->UnwindData >> 1) & 0x7FF; /* Function length / 4 */
        EndAddress = FunctionEntry->BeginAddress + (FunctionLength * 4);
    }
    else
    {
        /* Extended unwind data - need to read .xdata section */
        PULONG UnwindInfo = (PULONG)(ImageBase + (FunctionEntry->UnwindData & ~1));
        if (UnwindInfo != NULL)
        {
            /* Function length is in bits 0-17 of the first DWORD */
            FunctionLength = (*UnwindInfo) & 0x3FFFF; /* Function length / 4 */
            EndAddress = FunctionEntry->BeginAddress + (FunctionLength * 4);
        }
        else
        {
            /* Fallback - assume minimum function size */
            EndAddress = FunctionEntry->BeginAddress + 4;
        }
    }

    if ((ControlRva < FunctionEntry->BeginAddress) ||
        (ControlRva >= EndAddress))
    {
        DPRINT1("RtlVirtualUnwind: ControlPc 0x%p (RVA 0x%p) is outside function bounds [0x%x, 0x%x)\n",
                (PVOID)ControlPc, (PVOID)ControlRva,
                FunctionEntry->BeginAddress, EndAddress);
        return NULL;
    }

    /* Get unwind data */
    UnwindData = FunctionEntry->UnwindData;

    /* Check if this is a packed unwind info (bit 0 clear means packed) */
    if ((UnwindData & 1) == 0)
    {
        /* This is packed unwind info - decode it directly from the RUNTIME_FUNCTION */
        DPRINT("RtlVirtualUnwind: Using packed unwind data 0x%x\n", UnwindData);

        /* For now, implement minimal packed unwind handling */
        /* ARM64 packed format stores:
         * - Flag (bit 0): 0 for packed
         * - Function Length (bits 1-11): Function length / 4
         * - RegF (bits 12-14): Number of saved FP/SIMD register pairs
         * - RegI (bits 15-17): Number of saved integer register pairs
         * - H (bit 18): 1 if frame pointer (x29) is saved
         * - CR (bits 19-20): Canonical return address location
         * - Frame Size (bits 21-31): Local frame size / 16
         */

        ULONG FunctionLength = (UnwindData >> 1) & 0x7FF;  /* bits 1-11 */
        ULONG RegF = (UnwindData >> 12) & 0x7;              /* bits 12-14 */
        ULONG RegI = (UnwindData >> 15) & 0x7;              /* bits 15-17 */
        ULONG H = (UnwindData >> 18) & 0x1;                 /* bit 18 */
        ULONG CR = (UnwindData >> 19) & 0x3;                /* bits 19-20 */
        ULONG FrameSize = (UnwindData >> 21) & 0x7FF;       /* bits 21-31 */

        DPRINT("RtlVirtualUnwind: Packed data - FuncLen=%u, RegF=%u, RegI=%u, H=%u, CR=%u, FrameSize=%u\n",
               FunctionLength, RegF, RegI, H, CR, FrameSize);

        /* Set establisher frame to current SP + frame size */
        *EstablisherFrame = ContextRecord->Sp + (FrameSize * 16);

        /* Restore integer registers */
        if (RegI > 0)
        {
            PULONG64 StackPtr = (PULONG64)(ContextRecord->Sp + (FrameSize * 16) - (RegI * 16));

            /* Restore register pairs x19/x20, x21/x22, etc. */
            for (ULONG i = 0; i < RegI; i++)
            {
                ULONG RegNum = 19 + (i * 2);
                if (RegNum < 29)
                {
                    ContextRecord->X[RegNum] = *StackPtr++;
                    ContextRecord->X[RegNum + 1] = *StackPtr++;
                }
            }
        }

        /* Restore FP/LR if saved */
        if (H)
        {
            PULONG64 StackPtr = (PULONG64)(ContextRecord->Sp + (FrameSize * 16) - 16);
            ContextRecord->Fp = *StackPtr++;
            ContextRecord->Lr = *StackPtr++;
        }

        /* Update SP */
        ContextRecord->Sp += (FrameSize * 16);

        /* Set return address based on CR field */
        switch (CR)
        {
            case 0: /* Return address is in LR */
                ContextRecord->Pc = ContextRecord->Lr;
                break;
            case 1: /* Return address is at [SP] */
                ContextRecord->Pc = *(PULONG64)ContextRecord->Sp;
                ContextRecord->Sp += 8;
                break;
            case 2: /* Return address is at [SP + 8] */
                ContextRecord->Pc = *(PULONG64)(ContextRecord->Sp + 8);
                ContextRecord->Sp += 16;
                break;
            case 3: /* No epilogue - should not happen during unwinding */
                DPRINT1("RtlVirtualUnwind: Unexpected CR=3 in packed unwind data\n");
                break;
        }

        /* No exception handler for packed format */
        return NULL;
    }
    else
    {
        /* This is extended unwind info - pointer to .xdata section */
        PULONG UnwindInfo = (PULONG)(ImageBase + (UnwindData & ~1));

        DPRINT("RtlVirtualUnwind: Using extended unwind data at 0x%p\n", UnwindInfo);

        /* ARM64 .xdata format:
         * DWORD 0:
         *   Function Length (bits 0-17): Function length / 4
         *   Version (bits 18-19): Must be 0
         *   X (bit 20): Extended unwind codes
         *   E (bit 21): Has exception handler
         *   Epilog Count (bits 22-26): Number of epilog scopes
         *   Code Words (bits 27-31): Number of code words
         */

        ULONG Header = UnwindInfo[0];
        ULONG FunctionLength = Header & 0x3FFFF;           /* bits 0-17 */
        ULONG Version = (Header >> 18) & 0x3;              /* bits 18-19 */
        ULONG X = (Header >> 20) & 0x1;                    /* bit 20 */
        ULONG E = (Header >> 21) & 0x1;                    /* bit 21 */
        ULONG EpilogCount = (Header >> 22) & 0x1F;         /* bits 22-26 */
        ULONG CodeWords = (Header >> 27) & 0x1F;           /* bits 27-31 */

        DPRINT("RtlVirtualUnwind: Extended data - FuncLen=%u, Ver=%u, X=%u, E=%u, EpilogCount=%u, CodeWords=%u\n",
               FunctionLength, Version, X, E, EpilogCount, CodeWords);

        if (Version != 0)
        {
            DPRINT1("RtlVirtualUnwind: Unsupported unwind info version %u\n", Version);
            return NULL;
        }

        /* For now, implement minimal unwinding */
        /* TODO: Implement full ARM64 unwind code processing */

        /* Set establisher frame to current SP (simplified) */
        *EstablisherFrame = ContextRecord->Sp;

        /* Simple leaf function unwinding - restore LR to PC and return */
        ContextRecord->Pc = ContextRecord->Lr;

        /* Check for exception handler */
        if (E)
        {
            HasHandler = TRUE;
            /* Handler RVA follows the unwind codes */
            ULONG HandlerOffset = 1; /* Skip header */

            /* Skip epilog scopes if present */
            if (EpilogCount > 0 && !X)
            {
                HandlerOffset += (EpilogCount + 1) / 2; /* Packed 2 per DWORD */
            }
            else if (X)
            {
                HandlerOffset += 1; /* Extended epilog header */
                /* Additional epilog words would follow */
            }

            /* Skip unwind codes */
            HandlerOffset += CodeWords;

            if (HandlerOffset < 32) /* Sanity check */
            {
                HandlerRva = UnwindInfo[HandlerOffset];
                *HandlerData = &UnwindInfo[HandlerOffset + 1];
            }
        }
    }

    DPRINT("RtlVirtualUnwind: New PC=0x%p, SP=0x%p, EstablisherFrame=0x%p\n",
           (PVOID)ContextRecord->Pc, (PVOID)ContextRecord->Sp, (PVOID)*EstablisherFrame);

    /* Return exception handler if present and requested */
    if (HasHandler && (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
    {
        PEXCEPTION_ROUTINE Handler = (PEXCEPTION_ROUTINE)(ImageBase + HandlerRva);
        DPRINT("RtlVirtualUnwind: Returning exception handler at 0x%p\n", Handler);
        return Handler;
    }

    return NULL;
}

/*
 * @brief Initiates an unwind of the call stack
 *
 * This function performs stack unwinding by walking backwards through the call
 * stack, calling exception handlers along the way, until reaching the target
 * frame or the base of the stack.
 *
 * @param TargetFrame Target frame to unwind to (NULL for full unwind)
 * @param TargetIp Target instruction pointer to continue execution
 * @param ExceptionRecord Exception record describing the exception
 * @param ReturnValue Value to return from the target frame
 * @param ContextRecord Current processor context
 * @param HistoryTable Optional unwind history table
 */
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    EXCEPTION_RECORD LocalExceptionRecord;
    PRUNTIME_FUNCTION FunctionEntry;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    ULONG64 ImageBase, EstablisherFrame;
    PVOID HandlerData;
    ULONG_PTR StackLow, StackHigh;
    CONTEXT UnwindContext;
    ULONG MaxIterations = 256; /* Prevent infinite loops */

    DPRINT("RtlUnwindEx: TargetFrame=0x%p, TargetIp=0x%p\n", TargetFrame, TargetIp);

    /* Get current stack limits from TEB */
    StackLow = (ULONG_PTR)NtCurrentTeb()->NtTib.StackLimit;
    StackHigh = (ULONG_PTR)NtCurrentTeb()->NtTib.StackBase;

    /* If we have a target frame, adjust high limit */
    if (TargetFrame != NULL)
    {
        StackHigh = (ULONG64)TargetFrame + 1;
    }

    /* Set up default exception record if none provided */
    if (ExceptionRecord == NULL)
    {
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)ContextRecord->Pc;
        LocalExceptionRecord.ExceptionRecord = NULL;
        LocalExceptionRecord.NumberParameters = 0;
        LocalExceptionRecord.ExceptionFlags = EXCEPTION_UNWINDING;
        if (TargetFrame == NULL)
        {
            LocalExceptionRecord.ExceptionFlags |= EXCEPTION_EXIT_UNWIND;
        }
        ExceptionRecord = &LocalExceptionRecord;
    }
    else
    {
        /* Set unwind flags */
        ExceptionRecord->ExceptionFlags |= EXCEPTION_UNWINDING;
        if (TargetFrame == NULL)
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;
        }
    }

    /* Copy current context for unwinding */
    UnwindContext = *ContextRecord;

    /* Main unwinding loop */
    while (MaxIterations-- > 0)
    {
        DPRINT("RtlUnwindEx: Unwinding frame at PC=0x%p, SP=0x%p\n",
               (PVOID)UnwindContext.Pc, (PVOID)UnwindContext.Sp);

        /* Validate stack pointer */
        if ((UnwindContext.Sp < StackLow) ||
            (UnwindContext.Sp >= StackHigh) ||
            (UnwindContext.Sp & 0xF)) /* ARM64 stack must be 16-byte aligned */
        {
            DPRINT1("RtlUnwindEx: Invalid stack pointer 0x%p (limits: 0x%p - 0x%p)\n",
                    (PVOID)UnwindContext.Sp, (PVOID)StackLow, (PVOID)StackHigh);
            break;
        }

        /* Look up function entry for current PC */
        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc, &ImageBase, HistoryTable);

        if (FunctionEntry == NULL)
        {
            /* No function entry - assume leaf function, pop return address */
            DPRINT("RtlUnwindEx: Leaf function at PC=0x%p, popping return address\n",
                   (PVOID)UnwindContext.Pc);

            UnwindContext.Pc = *(PULONG64)UnwindContext.Sp;
            UnwindContext.Sp += 8;
            continue;
        }

        /* Perform virtual unwind */
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_UHANDLER,
                                          ImageBase,
                                          UnwindContext.Pc,
                                          FunctionEntry,
                                          &UnwindContext,
                                          &HandlerData,
                                          &EstablisherFrame,
                                          NULL);

        /* Check if we've reached the target frame */
        if (TargetFrame != NULL && EstablisherFrame == (ULONG64)TargetFrame)
        {
            DPRINT("RtlUnwindEx: Reached target frame at 0x%p\n", TargetFrame);
            break;
        }

        /* Call exception handler if present */
        if (ExceptionRoutine != NULL)
        {
            DPRINT("RtlUnwindEx: Calling exception handler at 0x%p\n", ExceptionRoutine);

            /* Set target unwind flag if this is the target frame */
            if (EstablisherFrame == (ULONG64)TargetFrame)
            {
                ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            }

            /* TODO: Call the exception handler properly */
            /* For now, just continue unwinding */
            ExceptionRecord->ExceptionFlags &= ~EXCEPTION_TARGET_UNWIND;
        }

        /* Check for stack exhaustion */
        if (UnwindContext.Sp >= StackHigh)
        {
            DPRINT("RtlUnwindEx: Reached top of stack\n");
            break;
        }
    }

    if (MaxIterations == 0)
    {
        DPRINT1("RtlUnwindEx: Maximum iterations reached - possible infinite loop\n");
    }

    /* Set up final context */
    if (TargetIp != NULL)
    {
        UnwindContext.Pc = (ULONG64)TargetIp;
    }

    /* Set return value in X0 */
    UnwindContext.X[0] = (ULONG64)ReturnValue;

    /* Copy unwound context back */
    *ContextRecord = UnwindContext;

    DPRINT("RtlUnwindEx: Unwind complete. Final PC=0x%p, SP=0x%p, X0=0x%p\n",
           (PVOID)ContextRecord->Pc, (PVOID)ContextRecord->Sp, ReturnValue);

    /* Restore context and continue execution */
    /* Note: This should not return in a real implementation */
    DPRINT1("RtlUnwindEx: TODO - Implement RtlRestoreContext for ARM64\n");
}

/*
 * @brief Restores processor context and continues execution
 *
 * This function is called after unwinding to restore the processor context
 * and continue execution at the target location.
 *
 * @param ContextRecord Context to restore
 * @param ExceptionRecord Optional exception record
 */
VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    DPRINT1("RtlRestoreContext: stub for ARM64\n");
    /* TODO: Implement context restore */
    ASSERT(FALSE);
}