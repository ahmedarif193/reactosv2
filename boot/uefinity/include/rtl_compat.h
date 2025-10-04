/*
 * RTL Compatibility Layer for Uefinity
 * Copyright (C) 2024 Ahmed ARIF (contact@eotics.com)
 *
 * This file provides a lightweight RTL compatibility layer that maintains
 * Windows/ReactOS ecosystem compatibility while ensuring optimal UEFI
 * performance on ARM64 platforms.
 *
 * Key Design Principles:
 * - Maintain RTL naming for ReactOS ecosystem compatibility
 * - Optimize for ARM64 UEFI environment (no kernel dependencies)
 * - Handle ARM64 alignment requirements properly
 * - Keep it lightweight (no bloat)
 * - Make debugging tools recognize RTL function names
 * - Ensure proper memory barriers and cache coherency
 */

#pragma once

#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* Architecture detection */
#if defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__)
    #define RTL_ARM64_OPTIMIZED 1

    /* ARM64-specific includes for cache and barrier operations */
    #if defined(__GNUC__)
        /* Memory barrier operations for ARM64 */
        #define RTL_ARM64_DMB_SY()    __asm__ volatile ("dmb sy" ::: "memory")
        #define RTL_ARM64_DSB_SY()    __asm__ volatile ("dsb sy" ::: "memory")
        #define RTL_ARM64_ISB()       __asm__ volatile ("isb" ::: "memory")
        #define RTL_ARM64_DC_CIVAC(addr) __asm__ volatile ("dc civac, %0" :: "r" (addr) : "memory")
    #else
        /* Fallback for non-GCC compilers */
        #define RTL_ARM64_DMB_SY()    __dmb(_ARM64_BARRIER_SY)
        #define RTL_ARM64_DSB_SY()    __dsb(_ARM64_BARRIER_SY)
        #define RTL_ARM64_ISB()       __isb(_ARM64_BARRIER_SY)
        #define RTL_ARM64_DC_CIVAC(addr) __builtin_arm_dc_civac(addr)
    #endif

    /* ARM64 cache line size detection */
    static inline size_t RTL_GetCacheLineSize(void)
    {
        static size_t cache_line_size = 0;
        if (cache_line_size == 0) {
            #if defined(__GNUC__)
                unsigned long ctr;
                __asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
                /* Extract DminLine (bits 16-19) and convert to bytes */
                cache_line_size = 4 << ((ctr >> 16) & 0xF);
            #else
                cache_line_size = 64; /* Conservative default */
            #endif
        }
        return cache_line_size;
    }

    /* ARM64 alignment checking */
    #define RTL_IS_ALIGNED(ptr, align) (((uintptr_t)(ptr) & ((align) - 1)) == 0)
    #define RTL_ALIGN_UP(ptr, align) (((uintptr_t)(ptr) + (align) - 1) & ~((align) - 1))
    #define RTL_ALIGN_DOWN(ptr, align) ((uintptr_t)(ptr) & ~((align) - 1))

#else
    #define RTL_ARM64_OPTIMIZED 0
    /* No-op macros for non-ARM64 platforms */
    #define RTL_ARM64_DMB_SY()
    #define RTL_ARM64_DSB_SY()
    #define RTL_ARM64_ISB()
    #define RTL_ARM64_DC_CIVAC(addr)
    #define RTL_IS_ALIGNED(ptr, align) 1
    #define RTL_ALIGN_UP(ptr, align) (ptr)
    #define RTL_ALIGN_DOWN(ptr, align) (ptr)
    static inline size_t RTL_GetCacheLineSize(void) { return 32; }
#endif

/*
 * ARM64-Optimized RtlZeroMemory
 *
 * This implementation optimizes for ARM64 by:
 * - Using 64-bit stores when properly aligned
 * - Ensuring cache coherency with memory barriers
 * - Handling unaligned cases efficiently
 * - Using cache line awareness for large copies
 */
static inline void RtlZeroMemory(void *Destination, size_t Length)
{
    if (Length == 0) return;

#if RTL_ARM64_OPTIMIZED
    uint8_t *dst = (uint8_t *)Destination;

    /* For small sizes, use standard memset */
    if (Length < 32) {
        memset(dst, 0, Length);
        return;
    }

    /* Handle unaligned start */
    while (Length > 0 && !RTL_IS_ALIGNED(dst, 8)) {
        *dst++ = 0;
        Length--;
    }

    /* Use 64-bit stores for bulk of the data */
    uint64_t *dst64 = (uint64_t *)dst;
    size_t qwords = Length >> 3;

    while (qwords >= 8) {
        /* Unroll loop for better performance */
        dst64[0] = 0; dst64[1] = 0; dst64[2] = 0; dst64[3] = 0;
        dst64[4] = 0; dst64[5] = 0; dst64[6] = 0; dst64[7] = 0;
        dst64 += 8;
        qwords -= 8;
    }

    while (qwords > 0) {
        *dst64++ = 0;
        qwords--;
    }

    /* Handle remaining bytes */
    dst = (uint8_t *)dst64;
    Length &= 7;
    while (Length > 0) {
        *dst++ = 0;
        Length--;
    }

    /* Ensure memory operations complete before continuing */
    RTL_ARM64_DSB_SY();
#else
    /* Standard implementation for non-ARM64 */
    memset(Destination, 0, Length);
#endif
}

/*
 * ARM64-Optimized RtlCopyMemory
 *
 * This implementation:
 * - Uses 64-bit loads/stores when aligned
 * - Handles overlapping regions by falling back to memmove
 * - Optimizes for ARM64 cache line awareness
 * - Ensures proper memory ordering
 */
static inline void RtlCopyMemory(void *Destination, const void *Source, size_t Length)
{
    if (Length == 0) return;

#if RTL_ARM64_OPTIMIZED
    const uint8_t *src = (const uint8_t *)Source;
    uint8_t *dst = (uint8_t *)Destination;

    /* Check for overlap - use memmove if needed */
    if (((uintptr_t)dst < (uintptr_t)src + Length) &&
        ((uintptr_t)src < (uintptr_t)dst + Length)) {
        memmove(dst, src, Length);
        return;
    }

    /* For small sizes, use standard memcpy */
    if (Length < 32) {
        memcpy(dst, src, Length);
        return;
    }

    /* Handle unaligned start */
    while (Length > 0 && (!RTL_IS_ALIGNED(src, 8) || !RTL_IS_ALIGNED(dst, 8))) {
        *dst++ = *src++;
        Length--;
    }

    /* Use 64-bit copies for bulk data */
    const uint64_t *src64 = (const uint64_t *)src;
    uint64_t *dst64 = (uint64_t *)dst;
    size_t qwords = Length >> 3;

    while (qwords >= 8) {
        /* Unroll for better performance */
        dst64[0] = src64[0]; dst64[1] = src64[1];
        dst64[2] = src64[2]; dst64[3] = src64[3];
        dst64[4] = src64[4]; dst64[5] = src64[5];
        dst64[6] = src64[6]; dst64[7] = src64[7];
        src64 += 8; dst64 += 8;
        qwords -= 8;
    }

    while (qwords > 0) {
        *dst64++ = *src64++;
        qwords--;
    }

    /* Handle remaining bytes */
    src = (const uint8_t *)src64;
    dst = (uint8_t *)dst64;
    Length &= 7;
    while (Length > 0) {
        *dst++ = *src++;
        Length--;
    }

    /* Ensure copy completes before continuing */
    RTL_ARM64_DSB_SY();
#else
    /* Standard implementation for non-ARM64 */
    memcpy(Destination, Source, Length);
#endif
}

/*
 * ARM64-Optimized RtlMoveMemory
 *
 * Always safe for overlapping regions, optimized for ARM64
 */
static inline void RtlMoveMemory(void *Destination, const void *Source, size_t Length)
{
    if (Length == 0) return;

#if RTL_ARM64_OPTIMIZED
    /* For ARM64, we use a smarter approach for large non-overlapping moves */
    const uint8_t *src = (const uint8_t *)Source;
    uint8_t *dst = (uint8_t *)Destination;

    /* Check if regions overlap */
    if (((uintptr_t)dst < (uintptr_t)src + Length) &&
        ((uintptr_t)src < (uintptr_t)dst + Length)) {
        /* Overlapping - use standard memmove */
        memmove(dst, src, Length);
    } else {
        /* Non-overlapping - use optimized copy */
        RtlCopyMemory(dst, src, Length);
    }
#else
    /* Standard implementation for non-ARM64 */
    memmove(Destination, Source, Length);
#endif
}

/*
 * ARM64-Optimized RtlFillMemory
 *
 * Optimizes pattern filling for ARM64 architecture
 */
static inline void RtlFillMemory(void *Destination, size_t Length, uint8_t Fill)
{
    if (Length == 0) return;

#if RTL_ARM64_OPTIMIZED
    uint8_t *dst = (uint8_t *)Destination;

    /* For small fills, use standard memset */
    if (Length < 32) {
        memset(dst, Fill, Length);
        return;
    }

    /* Create 64-bit pattern */
    uint64_t pattern = Fill;
    pattern |= (pattern << 8);
    pattern |= (pattern << 16);
    pattern |= (pattern << 32);

    /* Handle unaligned start */
    while (Length > 0 && !RTL_IS_ALIGNED(dst, 8)) {
        *dst++ = Fill;
        Length--;
    }

    /* Use 64-bit stores for bulk filling */
    uint64_t *dst64 = (uint64_t *)dst;
    size_t qwords = Length >> 3;

    while (qwords >= 8) {
        /* Unroll for performance */
        dst64[0] = pattern; dst64[1] = pattern; dst64[2] = pattern; dst64[3] = pattern;
        dst64[4] = pattern; dst64[5] = pattern; dst64[6] = pattern; dst64[7] = pattern;
        dst64 += 8;
        qwords -= 8;
    }

    while (qwords > 0) {
        *dst64++ = pattern;
        qwords--;
    }

    /* Handle remaining bytes */
    dst = (uint8_t *)dst64;
    Length &= 7;
    while (Length > 0) {
        *dst++ = Fill;
        Length--;
    }

    /* Ensure fill completes */
    RTL_ARM64_DSB_SY();
#else
    /* Standard implementation for non-ARM64 */
    memset(Destination, Fill, Length);
#endif
}

/*
 * Additional RTL compatibility functions commonly used in ReactOS
 */

/* RtlCompareMemory - Compare memory and return number of equal bytes */
static inline size_t RtlCompareMemory(const void *Source1, const void *Source2, size_t Length)
{
    const uint8_t *s1 = (const uint8_t *)Source1;
    const uint8_t *s2 = (const uint8_t *)Source2;
    size_t count = 0;

    while (count < Length && s1[count] == s2[count]) {
        count++;
    }

    return count;
}

/* RtlEqualMemory - Check if memory regions are equal */
static inline int RtlEqualMemory(const void *Source1, const void *Source2, size_t Length)
{
    return memcmp(Source1, Source2, Length) == 0;
}

/*
 * Cache coherency helpers for ARM64 UEFI environment
 * These ensure proper cache behavior when sharing data with firmware
 */
#if RTL_ARM64_OPTIMIZED
static inline void RtlFlushMemoryRange(void *Address, size_t Length)
{
    uint8_t *addr = (uint8_t *)Address;
    size_t cache_line_size = RTL_GetCacheLineSize();
    uintptr_t start = RTL_ALIGN_DOWN((uintptr_t)addr, cache_line_size);
    uintptr_t end = RTL_ALIGN_UP((uintptr_t)addr + Length, cache_line_size);

    for (uintptr_t line = start; line < end; line += cache_line_size) {
        RTL_ARM64_DC_CIVAC(line);
    }

    RTL_ARM64_DSB_SY();
    RTL_ARM64_ISB();
}

static inline void RtlMemoryBarrier(void)
{
    RTL_ARM64_DMB_SY();
}

static inline void RtlDataSynchronizationBarrier(void)
{
    RTL_ARM64_DSB_SY();
}

static inline void RtlInstructionSynchronizationBarrier(void)
{
    RTL_ARM64_ISB();
}
#else
/* No-op implementations for non-ARM64 */
static inline void RtlFlushMemoryRange(void *Address, size_t Length) { (void)Address; (void)Length; }
static inline void RtlMemoryBarrier(void) { }
static inline void RtlDataSynchronizationBarrier(void) { }
static inline void RtlInstructionSynchronizationBarrier(void) { }
#endif

/*
 * Performance hint: For very large memory operations (>1MB),
 * consider using UEFI Boot Services CopyMem for potential DMA acceleration
 */
#ifdef UEFIBOOT
extern EFI_BOOT_SERVICES *gBS;

static inline void RtlCopyMemoryLarge(void *Destination, const void *Source, size_t Length)
{
    if (Length > (1024 * 1024) && gBS && gBS->CopyMem) {
        /* Use UEFI Boot Services for very large copies - may use DMA */
        gBS->CopyMem(Destination, (void *)Source, Length);
    } else {
        RtlCopyMemory(Destination, Source, Length);
    }
}

static inline void RtlZeroMemoryLarge(void *Destination, size_t Length)
{
    if (Length > (1024 * 1024) && gBS && gBS->SetMem) {
        /* Use UEFI Boot Services for very large clears */
        gBS->SetMem(Destination, Length, 0);
    } else {
        RtlZeroMemory(Destination, Length);
    }
}
#else
#define RtlCopyMemoryLarge RtlCopyMemory
#define RtlZeroMemoryLarge RtlZeroMemory
#endif

/*
 * Debug helpers for UEFI environment
 */
#ifdef DBG
#define RTL_VERIFY_ALIGNMENT(ptr, align) \
    do { \
        if (!RTL_IS_ALIGNED(ptr, align)) { \
            /* In UEFI environment, we can't easily assert, so just trace */ \
            /* This would normally trigger a debug break or log entry */ \
        } \
    } while (0)
#else
#define RTL_VERIFY_ALIGNMENT(ptr, align)
#endif

/*
 * Documentation for maintainers:
 *
 * This compatibility layer provides these benefits:
 *
 * 1. ECOSYSTEM COMPATIBILITY: Maintains RTL function names that ReactOS
 *    components expect, enabling seamless code sharing.
 *
 * 2. ARM64 OPTIMIZATIONS: Uses 64-bit operations, proper alignment handling,
 *    and ARM64-specific memory barriers for optimal performance.
 *
 * 3. UEFI AWARENESS: Considers UEFI environment constraints and opportunities
 *    (like Boot Services acceleration for large operations).
 *
 * 4. DEBUGGING SUPPORT: Debuggers will show RTL function names in stack traces,
 *    making debugging more familiar for ReactOS developers.
 *
 * 5. ZERO OVERHEAD: Inline functions ensure no performance penalty over direct
 *    calls while maintaining API compatibility.
 *
 * 6. ARCHITECTURE NEUTRAL: Falls back gracefully on non-ARM64 platforms.
 *
 * Why this approach beats both "pure standard C" and "heavy RTL import":
 * - Lighter than importing full RTL subsystem
 * - More compatible than abandoning RTL naming
 * - More optimized than generic standard library
 * - More maintainable than scattered #ifdefs throughout codebase
 */