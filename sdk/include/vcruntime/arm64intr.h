/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 intriniscs
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _tag_ARM64INTR_BARRIER_TYPE
{
    _ARM64_BARRIER_SY     = 0xF,
    _ARM64_BARRIER_ST     = 0xE,
    _ARM64_BARRIER_LD     = 0xD,
    _ARM64_BARRIER_ISH    = 0xB,
    _ARM64_BARRIER_ISHST  = 0xA,
    _ARM64_BARRIER_ISHLD  = 0x9,
    _ARM64_BARRIER_NSH    = 0x7,
    _ARM64_BARRIER_NSHST  = 0x6,
    _ARM64_BARRIER_NSHLD  = 0x5,
    _ARM64_BARRIER_OSH    = 0x3,
    _ARM64_BARRIER_OSHST  = 0x2,
    _ARM64_BARRIER_OSHLD  = 0x1
} _ARM64INTR_BARRIER_TYPE;
#if defined(__clang__)
#define _rotl8 __reactos___rotl8
#define _rotr8 __reactos___rotr8
#define _rotr16 __reactos___rotr16
#define _InterlockedIncrement __reactos___InterlockedIncrement
#define _InterlockedDecrement __reactos___InterlockedDecrement
#define _InterlockedExchangeAdd __reactos___InterlockedExchangeAdd
#define _InterlockedCompareExchangePointer __reactos___InterlockedCompareExchangePointer
#define _InterlockedExchangePointer __reactos___InterlockedExchangePointer
#define _byteswap_ulong __reactos___byteswap_ulong
#define _byteswap_ushort __reactos___byteswap_ushort
#define _byteswap_uint64 __reactos___byteswap_uint64
#define _InterlockedAnd64 __reactos___InterlockedAnd64
#define _InterlockedAdd64 __reactos___InterlockedAdd64
#define _InterlockedOr64 __reactos___InterlockedOr64
#define _InterlockedCompareExchange64 __reactos___InterlockedCompareExchange64
#define _InterlockedCompareExchange8 __reactos___InterlockedCompareExchange8
#define _disable __reactos___disable
#define _enable __reactos___enable
#define _ReturnAddress __reactos___ReturnAddress
#define __debugbreak __reactos____debugbreak
#define _ReadWriteBarrier __reactos___ReadWriteBarrier
#define _ReadBarrier __reactos___ReadBarrier
#define _WriteBarrier __reactos___WriteBarrier
#ifndef _rotl
#define _rotl __reactos___rotl
#endif
#ifndef _rotr
#define _rotr __reactos___rotr
#endif
#define _InterlockedCompareExchange16 __reactos___InterlockedCompareExchange16
#define _InterlockedCompareExchange __reactos___InterlockedCompareExchange
#define _InterlockedIncrement16 __reactos___InterlockedIncrement16
#define _InterlockedDecrement16 __reactos___InterlockedDecrement16
#define _InterlockedExchange __reactos___InterlockedExchange
#define _InterlockedExchange8 __reactos___InterlockedExchange8
#define _InterlockedExchange16 __reactos___InterlockedExchange16
#define _InterlockedAnd __reactos___InterlockedAnd
#define _InterlockedAnd8 __reactos___InterlockedAnd8
#define _InterlockedAnd16 __reactos___InterlockedAnd16
#define _InterlockedOr __reactos___InterlockedOr
#define _InterlockedOr8 __reactos___InterlockedOr8
#define _InterlockedOr16 __reactos___InterlockedOr16
#define _InterlockedXor __reactos___InterlockedXor
#define _InterlockedXor8 __reactos___InterlockedXor8
#define _InterlockedXor16 __reactos___InterlockedXor16
#endif

void __dmb(unsigned int _Type);
void __dsb(unsigned int _Type);
void __isb(unsigned int _Type);

/* Memory barrier intrinsics */
void _ReadWriteBarrier(void);
void _ReadBarrier(void);
void _WriteBarrier(void);

/* Bit rotation intrinsics */
unsigned char _rotl8(unsigned char value, unsigned char shift);
unsigned char _rotr8(unsigned char value, unsigned char shift);
unsigned short _rotr16(unsigned short value, unsigned char shift);
unsigned int _rotl(unsigned int value, int shift);
unsigned int _rotr(unsigned int value, int shift);

/* Interlocked functions for ARM64 */
long _InterlockedIncrement(long volatile * _Addend);
long _InterlockedDecrement(long volatile * _Addend);
long _InterlockedExchangeAdd(long volatile * _Addend, long _Value);
long long _InterlockedAnd64(long long volatile * _Value, long long _Mask);
long long _InterlockedAdd64(long long volatile * _Addend, long long _Value);
long long _InterlockedOr64(long long volatile * _Value, long long _Mask);
long long _InterlockedCompareExchange64(long long volatile * _Destination, long long _Exchange, long long _Comparand);
char _InterlockedCompareExchange8(char volatile * _Destination, char _Exchange, char _Comparand);
short _InterlockedCompareExchange16(short volatile * _Destination, short _Exchange, short _Comparand);
long _InterlockedCompareExchange(long volatile * _Destination, long _Exchange, long _Comparand);
short _InterlockedIncrement16(short volatile * _Addend);
short _InterlockedDecrement16(short volatile * _Addend);
long _InterlockedExchange(long volatile * _Target, long _Value);
char _InterlockedExchange8(char volatile * _Target, char _Value);
short _InterlockedExchange16(short volatile * _Target, short _Value);
long _InterlockedAnd(long volatile * _Value, long _Mask);
char _InterlockedAnd8(char volatile * _Value, char _Mask);
short _InterlockedAnd16(short volatile * _Value, short _Mask);
long _InterlockedOr(long volatile * _Value, long _Mask);
char _InterlockedOr8(char volatile * _Value, char _Mask);
short _InterlockedOr16(short volatile * _Value, short _Mask);
long _InterlockedXor(long volatile * _Value, long _Mask);
char _InterlockedXor8(char volatile * _Value, char _Mask);
short _InterlockedXor16(short volatile * _Value, short _Mask);
void _disable(void);
void _enable(void);
void* _ReturnAddress(void);
void __debugbreak(void);

#if defined(__GNUC__)
/* GCC/MinGW implementation using built-in atomics and inline functions */

/* Bit rotation inline implementations */
__forceinline unsigned char _rotl8(unsigned char value, unsigned char shift)
{
    shift &= 7;
    return (value << shift) | (value >> (8 - shift));
}

__forceinline unsigned char _rotr8(unsigned char value, unsigned char shift)
{
    shift &= 7;
    return (value >> shift) | (value << (8 - shift));
}

__forceinline unsigned short _rotr16(unsigned short value, unsigned char shift)
{
    shift &= 15;
    return (value >> shift) | (value << (16 - shift));
}
__forceinline long _InterlockedIncrement(long volatile * _Addend)
{
    return __atomic_add_fetch(_Addend, 1, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedDecrement(long volatile * _Addend)
{
    return __atomic_sub_fetch(_Addend, 1, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedExchangeAdd(long volatile * _Addend, long _Value)
{
    return __atomic_fetch_add(_Addend, _Value, __ATOMIC_SEQ_CST);
}



__forceinline void* _InterlockedCompareExchangePointer(void* volatile * _Destination, void* _Exchange, void* _Comparand)
{
    void* expected = _Comparand;
    __atomic_compare_exchange_n(_Destination, &expected, _Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

__forceinline void* _InterlockedExchangePointer(void* volatile * _Target, void* _Value)
{
    return (void*)__atomic_exchange_n(_Target, _Value, __ATOMIC_SEQ_CST);
}

__forceinline unsigned long _byteswap_ulong(unsigned long value)
{
    return __builtin_bswap32(value);
}

__forceinline unsigned short _byteswap_ushort(unsigned short value)
{
    return __builtin_bswap16(value);
}

__forceinline unsigned long long _byteswap_uint64(unsigned long long value)
{
    return __builtin_bswap64(value);
}

__forceinline long long _InterlockedAnd64(long long volatile * _Value, long long _Mask)
{
    return __atomic_fetch_and(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline long long _InterlockedAdd64(long long volatile * _Addend, long long _Value)
{
    return __atomic_fetch_add(_Addend, _Value, __ATOMIC_SEQ_CST);
}

__forceinline long long _InterlockedOr64(long long volatile * _Value, long long _Mask)
{
    return __atomic_fetch_or(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline long long _InterlockedCompareExchange64(long long volatile * _Destination, long long _Exchange, long long _Comparand)
{
    long long expected = _Comparand;
    __atomic_compare_exchange_n(_Destination, &expected, _Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

__forceinline char _InterlockedCompareExchange8(char volatile * _Destination, char _Exchange, char _Comparand)
{
    char expected = _Comparand;
    __atomic_compare_exchange_n(_Destination, &expected, _Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

__forceinline void _disable(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

__forceinline void _enable(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

__forceinline void* _ReturnAddress(void)
{
    return __builtin_return_address(0);
}

__forceinline void __debugbreak(void)
{
    __asm__ volatile("brk #0");
}

/* Memory barrier implementations */
__forceinline void _ReadWriteBarrier(void)
{
    __asm__ volatile("" ::: "memory");
}

__forceinline void _ReadBarrier(void)
{
    __asm__ volatile("" ::: "memory");
}

__forceinline void _WriteBarrier(void)
{
    __asm__ volatile("" ::: "memory");
}

/* Bit rotation implementations */
__forceinline unsigned int _rotl(unsigned int value, int shift)
{
    const unsigned int mask = 31;
    shift &= mask;
    return (value << shift) | (value >> ((32 - shift) & mask));
}

__forceinline unsigned int _rotr(unsigned int value, int shift)
{
    const unsigned int mask = 31;
    shift &= mask;
    return (value >> shift) | (value << ((32 - shift) & mask));
}

/* Additional interlocked implementations */
__forceinline short _InterlockedCompareExchange16(short volatile * _Destination, short _Exchange, short _Comparand)
{
    short expected = _Comparand;
    __atomic_compare_exchange_n(_Destination, &expected, _Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

__forceinline long _InterlockedCompareExchange(long volatile * _Destination, long _Exchange, long _Comparand)
{
    long expected = _Comparand;
    __atomic_compare_exchange_n(_Destination, &expected, _Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

__forceinline short _InterlockedIncrement16(short volatile * _Addend)
{
    return __atomic_add_fetch(_Addend, 1, __ATOMIC_SEQ_CST);
}

__forceinline short _InterlockedDecrement16(short volatile * _Addend)
{
    return __atomic_sub_fetch(_Addend, 1, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedExchange(long volatile * _Target, long _Value)
{
    return __atomic_exchange_n(_Target, _Value, __ATOMIC_SEQ_CST);
}

__forceinline char _InterlockedExchange8(char volatile * _Target, char _Value)
{
    return __atomic_exchange_n(_Target, _Value, __ATOMIC_SEQ_CST);
}

__forceinline short _InterlockedExchange16(short volatile * _Target, short _Value)
{
    return __atomic_exchange_n(_Target, _Value, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedAnd(long volatile * _Value, long _Mask)
{
    return __atomic_fetch_and(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline char _InterlockedAnd8(char volatile * _Value, char _Mask)
{
    return __atomic_fetch_and(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline short _InterlockedAnd16(short volatile * _Value, short _Mask)
{
    return __atomic_fetch_and(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedOr(long volatile * _Value, long _Mask)
{
    return __atomic_fetch_or(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline char _InterlockedOr8(char volatile * _Value, char _Mask)
{
    return __atomic_fetch_or(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline short _InterlockedOr16(short volatile * _Value, short _Mask)
{
    return __atomic_fetch_or(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline long _InterlockedXor(long volatile * _Value, long _Mask)
{
    return __atomic_fetch_xor(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline char _InterlockedXor8(char volatile * _Value, char _Mask)
{
    return __atomic_fetch_xor(_Value, _Mask, __ATOMIC_SEQ_CST);
}

__forceinline short _InterlockedXor16(short volatile * _Value, short _Mask)
{
    return __atomic_fetch_xor(_Value, _Mask, __ATOMIC_SEQ_CST);
}
#else
#pragma intrinsic(__dmb)
#pragma intrinsic(__dsb)
#pragma intrinsic(__isb)
#pragma intrinsic(_ReadWriteBarrier)
#pragma intrinsic(_ReadBarrier)
#pragma intrinsic(_WriteBarrier)
#pragma intrinsic(_rotl)
#pragma intrinsic(_rotr)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedAnd64)
#pragma intrinsic(_InterlockedAdd64)
#pragma intrinsic(_InterlockedOr64)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedAnd)
#pragma intrinsic(_InterlockedAnd8)
#pragma intrinsic(_InterlockedAnd16)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(_InterlockedOr8)
#pragma intrinsic(_InterlockedOr16)
#pragma intrinsic(_InterlockedXor)
#pragma intrinsic(_InterlockedXor8)
#pragma intrinsic(_InterlockedXor16)
#pragma intrinsic(_disable)
#pragma intrinsic(_enable)
#pragma intrinsic(_ReturnAddress)
#pragma intrinsic(__debugbreak)
#endif

#if defined(__cplusplus)
} // extern "C"
#endif
