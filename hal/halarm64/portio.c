/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/portio.c
 * PURPOSE:         I/O Functions for access to ports on ARM64
 * PROGRAMMERS:     ReactOS ARM64 Port Team
 */

/* INCLUDES *******************************************************************/

#include <ntifs.h>
#include <ndk/haltypes.h>
#define NDEBUG
#include <debug.h>

/*
 * ARM64 doesn't have traditional x86-style I/O ports. Instead, all device
 * access is memory-mapped. These functions provide compatibility by mapping
 * port operations to memory-mapped register operations with proper barriers.
 */

/* ARM64 Memory Barrier Types */
#ifndef _ARM64_BARRIER_SY
#define _ARM64_BARRIER_SY 0xF
#endif

#undef READ_PORT_UCHAR
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef WRITE_PORT_UCHAR
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG

/* FUNCTIONS ******************************************************************/

/*
 * @brief Read a byte from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @return UCHAR - Value read from port
 */
UCHAR
NTAPI
READ_PORT_UCHAR(IN PUCHAR Port)
{
    UCHAR Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile UCHAR *)Port;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif

    return Value;
}

/*
 * @brief Read a word from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @return USHORT - Value read from port
 */
USHORT
NTAPI
READ_PORT_USHORT(IN PUSHORT Port)
{
    USHORT Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile USHORT *)Port;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif

    return Value;
}

/*
 * @brief Read a double word from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @return ULONG - Value read from port
 */
ULONG
NTAPI
READ_PORT_ULONG(IN PULONG Port)
{
    ULONG Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile ULONG *)Port;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif

    return Value;
}

/*
 * @brief Write a byte to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Value - Byte value to write
 */
VOID
NTAPI
WRITE_PORT_UCHAR(IN PUCHAR Port,
                 IN UCHAR Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    *(volatile UCHAR *)Port = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

/*
 * @brief Write a word to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Value - Word value to write
 */
VOID
NTAPI
WRITE_PORT_USHORT(IN PUSHORT Port,
                  IN USHORT Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    *(volatile USHORT *)Port = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

/*
 * @brief Write a double word to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Value - Double word value to write
 */
VOID
NTAPI
WRITE_PORT_ULONG(IN PULONG Port,
                 IN ULONG Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    *(volatile ULONG *)Port = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

/*
 * @brief Read multiple bytes from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @param Buffer - Output buffer to store data
 * @param Count - Number of bytes to read
 */
VOID
NTAPI
READ_PORT_BUFFER_UCHAR(IN PUCHAR Port,
                       OUT PUCHAR Buffer,
                       IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile UCHAR *)Port;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Read multiple words from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @param Buffer - Output buffer to store data
 * @param Count - Number of words to read
 */
VOID
NTAPI
READ_PORT_BUFFER_USHORT(IN PUSHORT Port,
                        OUT PUSHORT Buffer,
                        IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile USHORT *)Port;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Read multiple double words from a memory-mapped port
 *
 * @param Port - Memory address to read from
 * @param Buffer - Output buffer to store data
 * @param Count - Number of double words to read
 */
VOID
NTAPI
READ_PORT_BUFFER_ULONG(IN PULONG Port,
                       OUT PULONG Buffer,
                       IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile ULONG *)Port;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Write multiple bytes to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Buffer - Input buffer containing data
 * @param Count - Number of bytes to write
 */
VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(IN PUCHAR Port,
                        IN PUCHAR Buffer,
                        IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile UCHAR *)Port = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/*
 * @brief Write multiple words to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Buffer - Input buffer containing data
 * @param Count - Number of words to write
 */
VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(IN PUSHORT Port,
                         IN PUSHORT Buffer,
                         IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile USHORT *)Port = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/*
 * @brief Write multiple double words to a memory-mapped port
 *
 * @param Port - Memory address to write to
 * @param Buffer - Input buffer containing data
 * @param Count - Number of double words to write
 */
VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(IN PULONG Port,
                        IN PULONG Buffer,
                        IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile ULONG *)Port = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/* EOF */