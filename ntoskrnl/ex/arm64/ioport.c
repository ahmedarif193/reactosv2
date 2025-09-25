/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ex/arm64/ioport.c
 * PURPOSE:         Register and Port I/O Functions for ARM64
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * ARM64 doesn't have traditional x86-style I/O ports. All device access
 * is memory-mapped. These functions provide proper memory-mapped I/O
 * operations with appropriate memory barriers for device registers.
 */

/* ARM64 Memory Barrier Types */
#ifndef _ARM64_BARRIER_SY
#define _ARM64_BARRIER_SY 0xF
#endif

/* FUNCTIONS ******************************************************************/

/*
 * @brief Read a byte from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @return UCHAR - Value read from register
 */
UCHAR
NTAPI
READ_REGISTER_UCHAR(IN PUCHAR Register)
{
    UCHAR Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile UCHAR *)Register;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif

    return Value;
}

/*
 * @brief Read a word from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @return USHORT - Value read from register
 */
USHORT
NTAPI
READ_REGISTER_USHORT(IN PUSHORT Register)
{
    USHORT Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile USHORT *)Register;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Data Synchronization Barrier - System */

    return Value;
}

/*
 * @brief Read a double word from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @return ULONG - Value read from register
 */
ULONG
NTAPI
READ_REGISTER_ULONG(IN PULONG Register)
{
    ULONG Value;

    /* Read with memory barrier to ensure ordering */
    Value = *(volatile ULONG *)Register;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Data Synchronization Barrier - System */

    return Value;
}

/*
 * @brief Write a byte to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Value - Byte value to write
 */
VOID
NTAPI
WRITE_REGISTER_UCHAR(IN PUCHAR Register,
                     IN UCHAR Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Data Synchronization Barrier - System */
    *(volatile UCHAR *)Register = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Ensure write completes before continuing */
}

/*
 * @brief Write a word to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Value - Word value to write
 */
VOID
NTAPI
WRITE_REGISTER_USHORT(IN PUSHORT Register,
                      IN USHORT Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Data Synchronization Barrier - System */
    *(volatile USHORT *)Register = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Ensure write completes before continuing */
}

/*
 * @brief Write a double word to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Value - Double word value to write
 */
VOID
NTAPI
WRITE_REGISTER_ULONG(IN PULONG Register,
                     IN ULONG Value)
{
    /* Write with memory barrier to ensure ordering */
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Data Synchronization Barrier - System */
    *(volatile ULONG *)Register = Value;
#ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Ensure write completes before continuing */
}

/*
 * @brief Read multiple bytes from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Output buffer to store data
 * @param Count - Number of bytes to read
 */
VOID
NTAPI
READ_REGISTER_BUFFER_UCHAR(IN PUCHAR Register,
                           OUT PUCHAR Buffer,
                           IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile UCHAR *)Register;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Read multiple words from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Output buffer to store data
 * @param Count - Number of words to read
 */
VOID
NTAPI
READ_REGISTER_BUFFER_USHORT(IN PUSHORT Register,
                            OUT PUSHORT Buffer,
                            IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile USHORT *)Register;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Read multiple double words from a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Output buffer to store data
 * @param Count - Number of double words to read
 */
VOID
NTAPI
READ_REGISTER_BUFFER_ULONG(IN PULONG Register,
                           OUT PULONG Buffer,
                           IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Buffer[i] = *(volatile ULONG *)Register;
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each read */
    }
}

/*
 * @brief Write multiple bytes to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Input buffer containing data
 * @param Count - Number of bytes to write
 */
VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(IN PUCHAR Register,
                            IN PUCHAR Buffer,
                            IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile UCHAR *)Register = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/*
 * @brief Write multiple words to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Input buffer containing data
 * @param Count - Number of words to write
 */
VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(IN PUSHORT Register,
                             IN PUSHORT Buffer,
                             IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile USHORT *)Register = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/*
 * @brief Write multiple double words to a memory-mapped register
 *
 * @param Register - Memory address of the register
 * @param Buffer - Input buffer containing data
 * @param Count - Number of double words to write
 */
VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(IN PULONG Register,
                            IN PULONG Buffer,
                            IN ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier before each write */
        *(volatile ULONG *)Register = Buffer[i];
    #ifdef _M_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#endif  /* Barrier after each write */
    }
}

/* EOF */