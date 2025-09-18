/*
 * Minimal ARM64 intrinsics for GCC/Clang to satisfy ReactOS build.
 * Implement only what is currently needed for arm64 build.
 */

#ifndef KJK_INTRIN_ARM64_H_
#define KJK_INTRIN_ARM64_H_

#include <vcruntime.h>

#ifdef __GNUC__

__INTRIN_INLINE unsigned char _BitScanForward(unsigned long* Index, unsigned long Mask)
{
    if (!Mask) return 0;
    *Index = (unsigned long)__builtin_ctz((unsigned int)Mask);
    return 1;
}

__INTRIN_INLINE unsigned char _BitScanReverse(unsigned long* Index, unsigned long Mask)
{
    if (!Mask) return 0;
    *Index = 31u - (unsigned long)__builtin_clz((unsigned int)Mask);
    return 1;
}

__INTRIN_INLINE unsigned char _BitScanForward64(unsigned long* Index, unsigned long long Mask)
{
    if (!Mask) return 0;
    *Index = (unsigned long)__builtin_ctzll((unsigned long long)Mask);
    return 1;
}

__INTRIN_INLINE unsigned char _BitScanReverse64(unsigned long* Index, unsigned long long Mask)
{
    if (!Mask) return 0;
    *Index = 63u - (unsigned long)__builtin_clzll((unsigned long long)Mask);
    return 1;
}

__INTRIN_INLINE unsigned char _interlockedbittestandset64(volatile long long* a, long b)
{
    unsigned long long mask = 1ull << (b & 63);
    unsigned long long prev = __sync_fetch_and_or((volatile unsigned long long*)a, mask);
    return (unsigned char)((prev >> (b & 63)) & 1ull);
}

__INTRIN_INLINE unsigned char _interlockedbittestandreset64(volatile long long* a, long b)
{
    unsigned long long mask = 1ull << (b & 63);
    unsigned long long prev = __sync_fetch_and_and((volatile unsigned long long*)a, ~mask);
    return (unsigned char)((prev >> (b & 63)) & 1ull);
}

__INTRIN_INLINE void __yield(void)
{
    __asm__ __volatile__("yield" ::: "memory");
}

__INTRIN_INLINE long long _InterlockedExchangeAdd64(volatile long long* a, long long b)
{
    return __sync_fetch_and_add((volatile long long*)a, b);
}

#endif /* __GNUC__ */

#endif /* KJK_INTRIN_ARM64_H_ */
