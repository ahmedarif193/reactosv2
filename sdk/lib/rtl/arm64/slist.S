/*
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 SList assembly support
 * FILE:            lib/rtl/arm64/slist.S
 * PROGRAMMER:      ARM64 Port Team
 */

.text
.p2align 4

/*
 * On ARM64, SLIST_HEADER is 16 bytes with the following layout:
 * - Alignment (8 bytes): Contains depth in lower 16 bits, sequence in next bits, and pointer in upper bits
 * - Region (8 bytes): Used for additional metadata
 *
 * For simplicity on ARM64, we use the Alignment field to store:
 * - Bits [15:0]  : Depth (16 bits)
 * - Bits [31:16] : Sequence (16 bits)
 * - Bits [63:32] : Next Entry Pointer (32 bits, sign extended for ARM64 pointers)
 */

/* RtlInterlockedPopEntrySList */
.global RtlInterlockedPopEntrySList
RtlInterlockedPopEntrySList:
    /* x0 = PSLIST_HEADER */
1:
    /* Load the full 128-bit header atomically using ldaxp */
    ldaxp x1, x2, [x0]      /* x1 = Alignment, x2 = Region */

    /* Extract the next pointer from upper 32 bits of Alignment */
    lsr x3, x1, #32         /* Get upper 32 bits */
    cbz x3, 3f              /* If NULL pointer, list is empty */

    /* Load the next pointer from the entry */
    ldr x4, [x3]            /* x4 = Entry->Next */

    /* Build new header */
    and x5, x1, #0xFFFF     /* Get current depth */
    cbz x5, 3f              /* If depth is 0, list is empty */
    sub x5, x5, #1          /* Decrement depth */

    /* Get sequence and increment it */
    lsr x6, x1, #16
    and x6, x6, #0xFFFF     /* Extract sequence */
    add x6, x6, #1          /* Increment sequence */
    and x6, x6, #0xFFFF     /* Keep 16 bits */

    /* Build new Alignment value */
    orr x5, x5, x6, lsl #16 /* Combine depth and sequence */
    orr x5, x5, x4, lsl #32 /* Add new next pointer */

    /* Try to update the header atomically */
    stlxp w7, x5, x2, [x0]  /* Store exclusive pair */
    cbnz w7, 1b             /* If failed, retry */

    /* Return the popped entry */
    mov x0, x3
    ret

3:  /* Empty list */
    mov x0, #0              /* Return NULL */
    ret

/* RtlInterlockedPushEntrySList */
.global RtlInterlockedPushEntrySList
RtlInterlockedPushEntrySList:
    /* x0 = PSLIST_HEADER, x1 = PSLIST_ENTRY */
1:
    /* Load the full 128-bit header atomically */
    ldaxp x2, x3, [x0]      /* x2 = Alignment, x3 = Region */

    /* Extract current head pointer */
    lsr x4, x2, #32         /* Get current head from upper 32 bits */

    /* Set entry->Next to current head */
    str x4, [x1]

    /* Extract current depth and sequence */
    and x5, x2, #0xFFFF     /* Current depth */
    lsr x6, x2, #16
    and x6, x6, #0xFFFF     /* Current sequence */

    /* Increment depth and sequence */
    add x5, x5, #1          /* Increment depth */
    and x5, x5, #0xFFFF     /* Keep 16 bits */
    add x6, x6, #1          /* Increment sequence */
    and x6, x6, #0xFFFF     /* Keep 16 bits */

    /* Build new Alignment value */
    orr x5, x5, x6, lsl #16 /* Combine depth and sequence */
    orr x5, x5, x1, lsl #32 /* Add new entry as head */

    /* Try to update the header atomically */
    stlxp w7, x5, x3, [x0]  /* Store exclusive pair */
    cbnz w7, 1b             /* If failed, retry */

    /* Return the previous head */
    mov x0, x4
    ret

/* RtlInterlockedFlushSList */
.global RtlInterlockedFlushSList
RtlInterlockedFlushSList:
    /* x0 = PSLIST_HEADER */
1:
    /* Load the full 128-bit header atomically */
    ldaxp x1, x2, [x0]      /* x1 = Alignment, x2 = Region */

    /* Extract current head pointer */
    lsr x3, x1, #32         /* Get current head */

    /* Extract and increment sequence */
    lsr x4, x1, #16
    and x4, x4, #0xFFFF     /* Current sequence */
    add x4, x4, #1          /* Increment sequence */
    and x4, x4, #0xFFFF     /* Keep 16 bits */

    /* Build new header with depth=0, incremented sequence, NULL pointer */
    lsl x4, x4, #16         /* Position sequence */
    /* x4 now has depth=0, new sequence, pointer=0 */

    /* Try to update the header atomically */
    stlxp w5, x4, x2, [x0]  /* Store exclusive pair */
    cbnz w5, 1b             /* If failed, retry */

    /* Return the old head */
    mov x0, x3
    ret

/* Note: RtlQueryDepthSList and RtlInitializeSListHead are implemented in slist.c */

/* EOF */