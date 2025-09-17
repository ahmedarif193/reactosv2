/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 UEFI exception/interrupt stubs and IDT helpers
 */

.intel_syntax noprefix

.extern Amd64HandleException

.section .text
.globl Amd64SetIdt
Amd64SetIdt:
    lidt [rdi]
    ret

/* Macro to declare an exception stub that normalizes arguments:
   - Push error code (0 if none)
   - Push vector number
   - Jump to common handler that saves regs and calls C. */

.macro EXC_STUB vec has_err
.globl Amd64Exc\vec
Amd64Exc\vec:
    .if \has_err == 0
        push    0              /* fake error code (Intel syntax: no $) */
    .endif
    push    \vec             /* vector number */
    jmp     Amd64CommonStub
.endm

/* Save state and call Amd64HandleException */
.globl Amd64CommonStub
Amd64CommonStub:
    /* Save general-purpose registers */
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    /* Stack layout now (top -> bottom):
       r15..rax, vector, errcode, RIP, CS, RFLAGS, RSP, SS (if from lower priv) */
    mov  rcx, rsp           /* RCX = pointer to saved regs */
    sub  rsp, 32            /* shadow space for Win64 ABI safety */
    call Amd64HandleException

    add  rsp, 32
    /* Halt forever */
1:  cli
    hlt
    jmp 1b

/* Exceptions 0..31: indicate which push error code */
EXC_STUB 0, 0    /* #DE */
EXC_STUB 1, 0    /* #DB */
EXC_STUB 2, 0    /* NMI */
EXC_STUB 3, 0    /* #BP */
EXC_STUB 4, 0    /* #OF */
EXC_STUB 5, 0    /* #BR */
EXC_STUB 6, 0    /* #UD */
EXC_STUB 7, 0    /* #NM */
EXC_STUB 8, 1    /* #DF has error */
EXC_STUB 9, 0    /* reserved */
EXC_STUB 10, 1   /* #TS */
EXC_STUB 11, 1   /* #NP */
EXC_STUB 12, 1   /* #SS */
EXC_STUB 13, 1   /* #GP */
EXC_STUB 14, 1   /* #PF */
EXC_STUB 15, 0   /* reserved */
EXC_STUB 16, 0   /* #MF */
EXC_STUB 17, 1   /* #AC */
EXC_STUB 18, 0   /* #MC */
EXC_STUB 19, 0   /* #XM */
EXC_STUB 20, 0   /* #VE */
EXC_STUB 21, 0
EXC_STUB 22, 0
EXC_STUB 23, 0
EXC_STUB 24, 0
EXC_STUB 25, 0
EXC_STUB 26, 0
EXC_STUB 27, 0
EXC_STUB 28, 0
EXC_STUB 29, 0
EXC_STUB 30, 1   /* #SX */
EXC_STUB 31, 0
