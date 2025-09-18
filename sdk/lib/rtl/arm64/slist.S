/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 SList assembly support
 * FILE:            lib/rtl/arm64/slist.S
 * PROGRAMMER:      ARM64 Port Team
 */

.text
.p2align 4

/* RtlInterlockedPopEntrySList */
.global RtlInterlockedPopEntrySList
RtlInterlockedPopEntrySList:
    /* Load the SList head */
1:
    ldaxr x1, [x0]          /* Load exclusive with acquire semantics */
    cbz x1, 2f              /* If empty, return NULL */

    /* Load the next pointer */
    ldr x2, [x1]            /* Get next from entry */

    /* Try to update the head */
    stlxr w3, x2, [x0]      /* Store exclusive with release semantics */
    cbnz w3, 1b             /* If failed, retry */

    mov x0, x1              /* Return the popped entry */
    ret

2:
    mov x0, #0              /* Return NULL */
    ret

/* RtlInterlockedPushEntrySList */
.global RtlInterlockedPushEntrySList
RtlInterlockedPushEntrySList:
    /* x0 = SListHead, x1 = SListEntry */
1:
    ldaxr x2, [x0]          /* Load current head exclusive with acquire */
    str x2, [x1]            /* Set entry->Next to current head */
    stlxr w3, x1, [x0]      /* Try to set new head with release */
    cbnz w3, 1b             /* If failed, retry */

    mov x0, x2              /* Return previous first entry */
    ret

/* RtlInterlockedFlushSList */
.global RtlInterlockedFlushSList
RtlInterlockedFlushSList:
    /* Atomically exchange with NULL */
1:
    ldaxr x1, [x0]          /* Load current head */
    mov x2, #0              /* NULL */
    stlxr w3, x2, [x0]      /* Try to set to NULL */
    cbnz w3, 1b             /* If failed, retry */

    mov x0, x1              /* Return the old list */
    ret

/* RtlQueryDepthSList */
.global RtlQueryDepthSList
RtlQueryDepthSList:
    /* TODO: Implement proper depth query */
    /* For ARM64, we need to walk the list to count entries */
    /* This is a simplified version - real implementation would use the header */
    mov x0, #0              /* Return 0 for now */
    ret

/* EOF */