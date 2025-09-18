/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         ARM64 debug assembly support
 * FILE:            lib/rtl/arm64/debug_asm.S
 * PROGRAMMER:      ARM64 Port Team
 */

.text
.p2align 4

/* DbgBreakPoint */
.global DbgBreakPoint
DbgBreakPoint:
    brk #0xF000
    ret

/* DbgBreakPointWithStatus */
.global DbgBreakPointWithStatus
DbgBreakPointWithStatus:
    /* Status parameter in x0 */
    brk #0xF001
    ret

/* DbgUserBreakPoint */
.global DbgUserBreakPoint
DbgUserBreakPoint:
    brk #0xF002
    ret

/* DebugService */
.global DebugService
DebugService:
    /* TODO: Implement debug service for ARM64 */
    brk #0xF003
    ret

/* DebugService2 */
.global DebugService2
DebugService2:
    /* TODO: Implement debug service2 for ARM64 */
    brk #0xF004
    ret

/* EOF */