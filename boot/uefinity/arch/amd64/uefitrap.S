/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 UEFI exception/interrupt stubs and IDT helpers
 */

.intel_syntax noprefix

/* C handler */
.extern Amd64HandleException

.section .text

/* ---------------------------------------------------------------------- */
/* IDT/GDT/TR helpers                                                      */
/* ---------------------------------------------------------------------- */
.globl Amd64SetIdt
Amd64SetIdt:
    lidt [rcx]    /* MS x64 ABI: first parameter in RCX */
    ret

.globl Amd64SetGdt
Amd64SetGdt:
    lgdt [rcx]    /* MS x64 ABI: first parameter in RCX */
    ret

.globl Amd64LoadTr
Amd64LoadTr:
    mov ax, cx    /* MS x64 ABI: first parameter in RCX */
    ltr ax
    ret

/* ---------------------------------------------------------------------- */
/* Exception stub macro                                                    */
/* We always ensure the stack layout (from top) before entering C is:      *
 *   r15..r8, rdi,rsi,rbp,rbx,rdx,rcx,rax, Vector, ErrorCode, CPU frame    *
 * For vectors w/o HW error code, we push a fake 0 so total parity         *
 * matches the “has errcode” case.                                         */
/* ---------------------------------------------------------------------- */
.macro EXC_STUB vec has_err
.globl Amd64Exc\vec
Amd64Exc\vec:
    .if \has_err == 0
        /* No HW error code: synthesize one so layout is uniform */
        push    0
    .endif
    /* Push vector number (common to all) */
    push    \vec
    jmp     Amd64CommonStub
.endm

/* ---------------------------------------------------------------------- */
/* Common stub                                                             */
/* Save registers in the exact order expected by AMD64_TRAP_CONTEXT and   *
 * enter C with RCX = pointer to that saved block. We keep the original   *
 * RSP as-is for the context pointer, but we realign RSP to 16 bytes for  *
 * the call (with 32 bytes shadow space), then restore RSP afterwards.     */
/* ---------------------------------------------------------------------- */
.globl Amd64CommonStub
Amd64CommonStub:
    /* Save GPRs in order: r15..r8, rdi,rsi,rbp,rbx,rdx,rcx,rax */
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax

    /* RCX (first arg) = pointer to trap context (top of stack) */
    mov  rcx, rsp

    /* We must 16-align RSP for the CALL. We can’t just “and rsp,-16” on the
       real frame because we need to preserve Ctx where it is.
       So: save Ctx pointer in RDX, align the *call* stack, call, then restore. */
    mov  rdx, rsp           /* rdx = saved Ctx (top-of-stack pointer) */

    /* Create a temporary aligned call frame */
    mov  rax, rsp
    and  rsp, -16
    sub  rsp, 32            /* shadow space per Win64 ABI */

    /* Place argument back in RCX and call */
    mov  rcx, rdx
    call Amd64HandleException

    /* Tear down temp call frame and restore original RSP (Ctx) */
    add  rsp, 32
    mov  rsp, rdx

    /* Halt forever (debugger-oriented path) */
1:
    cli
    hlt
    jmp 1b

/* ---------------------------------------------------------------------- */
/* Exception vectors 0..31                                                 */
/* “has_err” must be 1 only for vectors with hardware error code.          */
/* ---------------------------------------------------------------------- */
EXC_STUB 0,  0    /* #DE */
EXC_STUB 1,  0    /* #DB */
EXC_STUB 2,  0    /* NMI */
EXC_STUB 3,  0    /* #BP */
EXC_STUB 4,  0    /* #OF */
EXC_STUB 5,  0    /* #BR */
EXC_STUB 6,  0    /* #UD */
EXC_STUB 7,  0    /* #NM */
EXC_STUB 8,  1    /* #DF (has error) */
EXC_STUB 9,  0    /* reserved */
EXC_STUB 10, 1    /* #TS */
EXC_STUB 11, 1    /* #NP */
EXC_STUB 12, 1    /* #SS */
EXC_STUB 13, 1    /* #GP */
EXC_STUB 14, 1    /* #PF */
EXC_STUB 15, 0    /* reserved */
EXC_STUB 16, 0    /* #MF */
EXC_STUB 17, 1    /* #AC */
EXC_STUB 18, 0    /* #MC */
EXC_STUB 19, 0    /* #XM */
EXC_STUB 20, 0    /* #VE */
EXC_STUB 21, 0
EXC_STUB 22, 0
EXC_STUB 23, 0
EXC_STUB 24, 0
EXC_STUB 25, 0
EXC_STUB 26, 0
EXC_STUB 27, 0
EXC_STUB 28, 0
EXC_STUB 29, 0
EXC_STUB 30, 1    /* #SX (has error) */
EXC_STUB 31, 0
