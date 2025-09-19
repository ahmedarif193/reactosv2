/*
 * COPYRIGHT:         BSD - See COPYING.ARM in the top level directory
 * PROJECT:           ReactOS CRT library
 * PURPOSE:           Implementation of _setjmp / longjmp for ARM64
 * PROGRAMMER:        Claude AI (based on ARM32 implementation and AMD64 reference)
 */

/* GAS syntax for ARM64 setjmp/longjmp implementation */
/* Structure layout matches _JUMP_BUFFER in setjmp.h */

.text

.global _setjmpex
_setjmpex:
    /* Save registers according to _JUMP_BUFFER structure layout */

    /* Store Frame (x1 parameter) and Reserved (0) */
    stp x1, xzr, [x0], #16      // Frame, Reserved

    /* Store callee-saved registers x19-x28 */
    stp x19, x20, [x0], #16     // X19, X20
    stp x21, x22, [x0], #16     // X21, X22
    stp x23, x24, [x0], #16     // X23, X24
    stp x25, x26, [x0], #16     // X25, X26
    stp x27, x28, [x0], #16     // X27, X28

    /* Store frame pointer (x29) and link register (x30) */
    stp x29, x30, [x0], #16     // Fp, Lr

    /* Calculate and store stack pointer (as it was before the call) */
    mov x2, sp                  // Current stack pointer
    str x2, [x0], #8            // Sp

    /* Store floating point control registers (32-bit each, but store as 64-bit aligned) */
    mrs x2, fpcr
    str w2, [x0], #4            // Fpcr (32-bit)
    mrs x2, fpsr
    str w2, [x0], #4            // Fpsr (32-bit)

    /* Store NEON/floating point registers d8-d15 (callee-saved) */
    stp d8, d9, [x0], #16       // D[0], D[1]
    stp d10, d11, [x0], #16     // D[2], D[3]
    stp d12, d13, [x0], #16     // D[4], D[5]
    stp d14, d15, [x0], #16     // D[6], D[7]

    /* Return 0 */
    mov x0, #0
    ret

.global _setjmp
_setjmp:
    /* Same as _setjmpex but with Frame = 0 (like AMD64 _setjmp) */

    /* Store Frame (0) and Reserved (0) */
    stp xzr, xzr, [x0], #16     // Frame = 0, Reserved = 0

    /* Store callee-saved registers x19-x28 */
    stp x19, x20, [x0], #16     // X19, X20
    stp x21, x22, [x0], #16     // X21, X22
    stp x23, x24, [x0], #16     // X23, X24
    stp x25, x26, [x0], #16     // X25, X26
    stp x27, x28, [x0], #16     // X27, X28

    /* Store frame pointer (x29) and link register (x30) */
    stp x29, x30, [x0], #16     // Fp, Lr

    /* Store current stack pointer (should be 16-byte aligned per ARM64 ABI) */
    mov x2, sp                  // Current stack pointer
    str x2, [x0], #8            // Sp

    /* Store floating point control registers (32-bit each, but store as 64-bit aligned) */
    mrs x2, fpcr
    str w2, [x0], #4            // Fpcr (32-bit)
    mrs x2, fpsr
    str w2, [x0], #4            // Fpsr (32-bit)

    /* Store NEON/floating point registers d8-d15 (callee-saved) */
    stp d8, d9, [x0], #16       // D[0], D[1]
    stp d10, d11, [x0], #16     // D[2], D[3]
    stp d12, d13, [x0], #16     // D[4], D[5]
    stp d14, d15, [x0], #16     // D[6], D[7]

    /* Return 0 */
    mov x0, #0
    ret

.global setjmp
setjmp = _setjmp

.global __mingw_setjmp
__mingw_setjmp = _setjmp

.global __wine_setjmpex
__wine_setjmpex = _setjmpex

.global longjmp
longjmp:
    /* Restore registers according to _JUMP_BUFFER structure layout */

    /* Load Frame and Reserved (skip Reserved) */
    ldp x2, x3, [x0], #16       // x2 = Frame, x3 = Reserved (ignored)

    /* Restore callee-saved registers x19-x28 */
    ldp x19, x20, [x0], #16     // X19, X20
    ldp x21, x22, [x0], #16     // X21, X22
    ldp x23, x24, [x0], #16     // X23, X24
    ldp x25, x26, [x0], #16     // X25, X26
    ldp x27, x28, [x0], #16     // X27, X28

    /* Restore frame pointer (x29) and link register (x30) */
    ldp x29, x30, [x0], #16     // Fp, Lr

    /* Restore stack pointer (ARM64 ABI guarantees 16-byte alignment) */
    ldr x3, [x0], #8            // Sp
    mov sp, x3

    /* Restore floating point control registers (32-bit each) */
    ldr w3, [x0], #4            // Fpcr (32-bit)
    msr fpcr, x3
    ldr w3, [x0], #4            // Fpsr (32-bit)
    msr fpsr, x3

    /* Restore NEON/floating point registers d8-d15 (callee-saved) */
    ldp d8, d9, [x0], #16       // D[0], D[1]
    ldp d10, d11, [x0], #16     // D[2], D[3]
    ldp d12, d13, [x0], #16     // D[4], D[5]
    ldp d14, d15, [x0], #16     // D[6], D[7]

    /* Return value passed in w1, ensure it's not 0 */
    mov w0, w1
    cmp w0, #0
    cset w3, eq                 // Set w3 to 1 if w0 is 0
    orr w0, w0, w3              // w0 = w0 | w3, ensuring non-zero result

    ret

.global __mingw_longjmp
__mingw_longjmp = longjmp

/* EOF */