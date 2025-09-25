/*
 * Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Kernel
 * PURPOSE:         ARM64 Fast Interlocked Operations
 * FILE:            ntoskrnl/ex/arm64/fastinterlck.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/* Note: ExInterlockedIncrementLong is redirected to Exfarm64InterlockedIncrementLong via macro */

/* Note: ExInterlockedDecrementLong is redirected to Exfarm64InterlockedDecrementLong via macro */

/*
 * @brief Atomically exchange two 32-bit values
 *
 * @param Target - Pointer to target value
 * @param Value - New value to set
 * @param Lock - Spinlock for synchronization (ignored on ARM64 with native atomics)
 * @return ULONG - Previous value at target
 */
ULONG
NTAPI
ExInterlockedExchangeUlong(
    IN PULONG Target,
    IN ULONG Value,
    IN PKSPIN_LOCK Lock)
{
    UNREFERENCED_PARAMETER(Lock);

    /* Use compiler intrinsic for atomic exchange */
    return (ULONG)_InterlockedExchange((PLONG)Target, (LONG)Value);
}

/*
 * @brief Atomically add to a 32-bit value
 *
 * @param Addend - Pointer to value to add to
 * @param Increment - Value to add
 * @param Lock - Spinlock for synchronization (ignored on ARM64 with native atomics)
 * @return ULONG - Previous value before addition
 */
ULONG
NTAPI
ExInterlockedAddUlong(
    IN PULONG Addend,
    IN ULONG Increment,
    IN PKSPIN_LOCK Lock)
{
    UNREFERENCED_PARAMETER(Lock);

    /* Use compiler intrinsic for atomic add */
    return (ULONG)_InterlockedExchangeAdd((PLONG)Addend, (LONG)Increment);
}

/* TODO: ExInterlockedCompareExchange64 - temporarily disabled due to macro conflict */

/*
 * @brief ARM64 fast increment function for compatibility
 *
 * This provides the Exfarm64InterlockedIncrementLong function
 * that ARM64 macros redirect to (similar to x86's Exfi386InterlockedIncrementLong)
 *
 * @param Addend - Pointer to value to increment
 * @return INTERLOCKED_RESULT - Sign of result after increment
 */
INTERLOCKED_RESULT
FASTCALL
Exfarm64InterlockedIncrementLong(
    IN OUT LONG volatile *Addend)
{
    LONG Result;

    /* Use compiler intrinsic for atomic increment */
    Result = _InterlockedIncrement(Addend);

    /* Return sign classification */
    return (Result < 0) ? ResultNegative :
           (Result > 0) ? ResultPositive :
           ResultZero;
}

/*
 * @brief ARM64 fast decrement function for compatibility
 *
 * This provides the Exfarm64InterlockedDecrementLong function
 * that ARM64 macros redirect to (similar to x86's Exfi386InterlockedDecrementLong)
 *
 * @param Addend - Pointer to value to decrement
 * @return INTERLOCKED_RESULT - Sign of result after decrement
 */
INTERLOCKED_RESULT
FASTCALL
Exfarm64InterlockedDecrementLong(
    IN OUT PLONG Addend)
{
    LONG Result;

    /* Use compiler intrinsic for atomic decrement */
    Result = _InterlockedDecrement(Addend);

    /* Return sign classification */
    return (Result < 0) ? ResultNegative :
           (Result > 0) ? ResultPositive :
           ResultZero;
}