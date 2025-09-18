/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 exception assembly support
 * FILE:            lib/rtl/arm64/except_asm.S
 * PROGRAMMER:      ARM64 Port Team
 */

.text
.p2align 4

/* RtlCaptureContext */
.global RtlCaptureContext
RtlCaptureContext:
    /* TODO: Implement proper context capture */
    /* For now, just return - stub implementation */
    ret

/* RtlRestoreContext */
.global RtlRestoreContext
RtlRestoreContext:
    /* TODO: Implement proper context restore */
    /* For now, just break - stub implementation */
    brk #0xF005
    ret

/* EOF */