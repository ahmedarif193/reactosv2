/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     x64 assembly UEFI escape helper functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
 */

#include <asm.inc>
#include <arch/pc/x86common.h>
#include <arch/pc/pcbios.h>

EXTERN UefiServiceStack:QWORD
EXTERN BasicStack:QWORD
EXTERN ExecuteLoaderCleanly:PROC
EXTERN UefiExitBootServices:PROC
/* If needed: */
/* EXTERN EfiImageHandle:QWORD */
/* EXTERN EfiMapKey:QWORD */

.code64

/*
 * void _exituefi(VOID)
 *
 * Exit UEFI boot services.
 *
 * CRITICAL: BasicStack may overlap with the loaded image on AMD64 due to
 * low ImageBase (0x10000). We must switch to UefiServiceStack (the original
 * UEFI-provided stack) for the ExitBootServices call, then restore our stack.
 */
PUBLIC _exituefi
_exituefi:
    /* Save callee-saved registers */
    push    rbp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    /* Save current stack pointer in r13 */
    mov     r13, rsp

    /* Save return address in r12 (at [rsp + 48] after 6 pushes) */
    mov     r12, qword ptr [rsp + 48]

    /* Switch to UefiServiceStack (the valid UEFI-provided stack) */
    mov     rax, qword ptr [rip + UefiServiceStack]
    test    rax, rax
    jz      .use_current_stack

    mov     rsp, rax
    jmp     .stack_ready

.use_current_stack:
    /* If no service stack available, stay on current */

.stack_ready:
    /* Ensure 16-byte alignment */
    and     rsp, -16

    /* Allocate shadow space and call UefiExitBootServices */
    sub     rsp, 0x28          /* 32-byte shadow + 8 alignment */
    call    UefiExitBootServices
    add     rsp, 0x28

    /* Restore original stack pointer */
    mov     rsp, r13

    /* Restore callee-saved registers */
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp

    /* Disable interrupts and load new GDT */
    cli

#ifdef _USE_ML
    lgdt    _gdtptr
#else
    lgdt    [rip + _gdtptr]
#endif

    /* Return using saved address */
    jmp     r12

// void _changestack(VOID)
PUBLIC _changestack
_changestack:
    /* Save current stack pointer and switch to BasicStack */
    mov     rax, rsp
    mov     rsp, qword ptr [rip + BasicStack]
    
    /* Ensure 16-byte alignment (defensive programming) */
    and     rsp, -16

    /* x64 ABI: shadow space + alignment before call. */
    /* Pass old RSP in RCX if ExecuteLoaderCleanly expects it (common pattern). */
    sub     rsp, 0x20                /* 32-byte shadow space */
    mov     rcx, rax                 /* RCX = previous RSP (argument 1) */
    call    ExecuteLoaderCleanly
    add     rsp, 0x20
    
    /* Note: ExecuteLoaderCleanly should not return in normal operation */
    /* If it does return, this is likely an error condition */
    
    ret

.align 8
gdt:
    .word HEX(0000), HEX(0000), HEX(0000), HEX(0000) /* 00: NULL descriptor */
    .word HEX(0000), HEX(0000), HEX(0000), HEX(0000) /* 08: unused/reserved */
    .word HEX(0000), HEX(0000), HEX(9A00), HEX(0020) /* 10: long mode CS (access=0x9A, L-bit=1) */
    .word HEX(FFFF), HEX(0000), HEX(9200), HEX(0000) /* 18: long mode DS (RW, DPL=0) */
    .word HEX(FFFF), HEX(0000), HEX(9E00), HEX(0000) /* 20: 16-bit real mode CS */
    .word HEX(FFFF), HEX(0000), HEX(9200), HEX(0000) /* 28: 16-bit real mode DS */
    .word HEX(FFFF), HEX(0000), HEX(9B00), HEX(00CF) /* 30: 32-bit compat mode CS */

    /* GDT table pointer: 10 bytes (limit:2 + base:8) */
_gdtptr:
    .word HEX(0037)                  /* Limit = 7 entries * 8 - 1 = 0x37 */
    .quad gdt                        /* Base Address */

END