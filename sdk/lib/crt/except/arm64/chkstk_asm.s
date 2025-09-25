/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 stack probing implementation
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

/* ARM64 stack probing functions */
/* #include <kxarm64.h> */

/* CODE **********************************************************************/
    .text
    .align 4

/* __chkstk - stack overflow checking
 * Input: x15 = number of bytes to allocate
 * Output: stack pointer adjusted, guard pages touched
 * ARM64 calling convention preserves x19-x28, so we can use x16-x17 as scratch
 */
    .global __chkstk
__chkstk:
    /* ARM64 stack grows downward
     * We need to probe every page (4KB on ARM64) to ensure guard pages are touched
     */
    cmp x15, #0x1000            /* Compare with page size (4KB) */
    b.lt .Lchkstk_small         /* If less than page size, no probing needed */

    /* Save registers we'll use */
    stp x16, x17, [sp, #-16]!   /* Save scratch registers */

    mov x16, sp                 /* Current stack pointer */
    sub x17, x16, x15           /* Target stack pointer */

.Lchkstk_probe_loop:
    sub x16, x16, #0x1000       /* Move down one page */
    str xzr, [x16]              /* Touch the page (probe) */
    cmp x16, x17                /* Check if we've reached target */
    b.gt .Lchkstk_probe_loop    /* Continue if more pages to probe */

    /* Restore registers */
    ldp x16, x17, [sp], #16     /* Restore scratch registers */

    /* Adjust stack pointer to final position */
    sub sp, sp, x15
    ret

.Lchkstk_small:
    /* Small allocation, just adjust stack pointer */
    sub sp, sp, x15
    ret

/* __alloca_probe - alloca with stack probing
 * Input: x0 = number of bytes to allocate
 * Output: x0 = pointer to allocated memory
 */
    .global __alloca_probe
__alloca_probe:
    /* Save return address and frame pointer */
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    /* Align allocation to 16-byte boundary (ARM64 requirement) */
    add x0, x0, #15             /* Add 15 to round up */
    and x0, x0, #~15            /* Clear lower 4 bits to align to 16 */

    /* Check if we need stack probing */
    cmp x0, #0x1000             /* Compare with page size */
    b.lt .Lalloca_small         /* If small, no probing needed */

    /* Use __chkstk for large allocations */
    mov x15, x0                 /* Move size to x15 for __chkstk */
    bl __chkstk                 /* Call stack checker */
    mov x0, sp                  /* Return pointer to allocated memory */
    b .Lalloca_done

.Lalloca_small:
    /* Small allocation, just adjust stack */
    sub sp, sp, x0              /* Allocate space */
    mov x0, sp                  /* Return pointer to allocated memory */

.Lalloca_done:
    /* Restore frame pointer and return */
    ldp x29, x30, [sp], #16
    ret

/* __chkstk_ms - Microsoft-style stack checker (compatibility) */
    .global __chkstk_ms
__chkstk_ms:
    /* Microsoft calling convention compatibility */
    /* Input in x0 instead of x15 */
    mov x15, x0                 /* Move size to expected register */
    b __chkstk                  /* Jump to main implementation */

/* EOF */
