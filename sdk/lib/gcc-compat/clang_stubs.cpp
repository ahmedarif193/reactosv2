#include <cstdlib>
#include <climits>
#include <unwind.h>

extern "C" _Unwind_Reason_Code __reactos_gxx_personality(int, _Unwind_Action, unsigned long long,
                                                         struct _Unwind_Exception*, struct _Unwind_Context*)
{
    std::abort();
    return _URC_FATAL_PHASE1_ERROR;
}

extern "C" unsigned int __reactos_rotl32(unsigned int value, int shift)
{
    shift &= 31;
    return (value << shift) | (value >> ((32 - shift) & 31));
}

extern "C" unsigned int __reactos_rotr32(unsigned int value, int shift)
{
    shift &= 31;
    return (value >> shift) | (value << ((32 - shift) & 31));
}

extern "C" unsigned long long __reactos_rotl64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value << shift) | (value >> ((64 - shift) & 63));
}

extern "C" unsigned long long __reactos_rotr64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value >> shift) | (value << ((64 - shift) & 63));
}

extern "C" unsigned long __reactos_lrotl(unsigned long value, int shift)
{
#if ULONG_MAX == 0xffffffffUL
    return static_cast<unsigned long>(__reactos_rotl32(static_cast<unsigned int>(value), shift));
#else
    return static_cast<unsigned long>(__reactos_rotl64(static_cast<unsigned long long>(value), shift));
#endif
}

extern "C" unsigned long __reactos_lrotr(unsigned long value, int shift)
{
#if ULONG_MAX == 0xffffffffUL
    return static_cast<unsigned long>(__reactos_rotr32(static_cast<unsigned int>(value), shift));
#else
    return static_cast<unsigned long>(__reactos_rotr64(static_cast<unsigned long long>(value), shift));
#endif
}

extern "C" _Unwind_Reason_Code __gxx_personality_seh0(int, _Unwind_Action, unsigned long long,
                                                      struct _Unwind_Exception*, struct _Unwind_Context*)
    __attribute__((alias("__reactos_gxx_personality")));

extern "C" unsigned int _rotl(unsigned int, int) __attribute__((alias("__reactos_rotl32")));
extern "C" unsigned int _rotr(unsigned int, int) __attribute__((alias("__reactos_rotr32")));
extern "C" unsigned long long _rotl64(unsigned long long, int) __attribute__((alias("__reactos_rotl64")));
extern "C" unsigned long long _rotr64(unsigned long long, int) __attribute__((alias("__reactos_rotr64")));
extern "C" unsigned long _lrotl(unsigned long, int) __attribute__((alias("__reactos_lrotl")));
extern "C" unsigned long _lrotr(unsigned long, int) __attribute__((alias("__reactos_lrotr")));
