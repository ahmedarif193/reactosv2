/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 exception handling stubs
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* ARM64 assembly stubs for exception handling functions */

.text
.align 4

/* ARM64 calling convention:
 * x0-x7: argument registers
 * x8: indirect result location register
 * x9-x15: temporary registers
 * x16-x17: intra-procedure-call scratch registers
 * x18: platform register (reserved)
 * x19-x28: callee-saved registers
 * x29: frame pointer
 * x30: link register (return address)
 * sp: stack pointer
 */

/* _abnormal_termination - check if in abnormal termination */
.global _abnormal_termination
_abnormal_termination:
    /* For ARM64, we need to check the current exception context
     * This is a simplified implementation that returns 0 (FALSE)
     * In a full implementation, this would check the current unwind state
     */
    mov w0, #0      /* Return FALSE - not in abnormal termination */
    ret

/* _except_handler2 stub - x86 SEH, not used on ARM64 */
.global _except_handler2
_except_handler2:
    mov w0, #1      /* ExceptionContinueSearch */
    ret

/* _except_handler3 stub - x86 SEH, not used on ARM64 */
.global _except_handler3
_except_handler3:
    mov w0, #1      /* ExceptionContinueSearch */
    ret

/* _except_handler4 stub - x86 SEH, not used on ARM64 */
.global _except_handler4
_except_handler4:
    mov w0, #1      /* ExceptionContinueSearch */
    ret

/* _global_unwind2 - global unwind (legacy x86 function) */
.global _global_unwind2
_global_unwind2:
    /* On ARM64, global unwind is handled by RtlUnwindEx
     * This legacy function is a no-op
     */
    ret

/* _local_unwind2 - local unwind (legacy x86 function) */
.global _local_unwind2
_local_unwind2:
    /* On ARM64, local unwind is handled by RtlUnwindEx
     * This legacy function is a no-op
     */
    ret

/* _local_unwind4 - extended local unwind (legacy x86 function) */
.global _local_unwind4
_local_unwind4:
    /* On ARM64, this is also handled by RtlUnwindEx */
    ret

/* _logb - extract exponent from double */
.global _logb
_logb:
    /* Input in d0, return in d0 */
    fmov x0, d0
    lsr x0, x0, #52         /* Extract exponent bits */
    and x0, x0, #0x7FF      /* Mask to get 11 exponent bits */
    cmp x0, #0              /* Check for zero/denormal */
    b.eq .Llogb_special
    cmp x0, #0x7FF          /* Check for infinity/NaN */
    b.eq .Llogb_special
    sub x0, x0, #1023       /* Remove IEEE 754 bias */
    scvtf d0, x0            /* Convert to double */
    ret
.Llogb_special:
    /* Handle special cases (zero, denormal, infinity, NaN) */
    /* For simplicity, return the input for special cases */
    ret

/* fmod - floating point remainder */
.global fmod
fmod:
    /* Inputs in d0 and d1, return in d0 */
    /* Simple implementation using division and truncation */
    fdiv d2, d0, d1         /* d2 = x / y */
    frintz d2, d2           /* Truncate to integer */
    fmul d2, d2, d1         /* d2 = trunc(x/y) * y */
    fsub d0, d0, d2         /* remainder = x - trunc(x/y) * y */
    ret

/* UlongToPtr - convert ULONG to pointer */
.global UlongToPtr
UlongToPtr:
    /* Input: w0 (32-bit), Output: x0 (64-bit pointer) */
    mov x0, x0              /* Zero-extend w0 to x0 */
    ret

/* __security_check_cookie - stack protection check */
.global __security_check_cookie
__security_check_cookie:
    /* Input: x0 = cookie value to check */
    /* For ARM64, implement basic stack cookie validation */
    /* This is a simplified version - full implementation would check against
     * a global security cookie stored in TLS or global data */
    cmp x0, #0
    b.eq .Lsecurity_fail    /* If cookie is 0, fail */
    ret                     /* Cookie is valid, return */
.Lsecurity_fail:
    /* Security cookie validation failed - terminate process */
    brk #0x1                /* Trigger debug break/exception */
    ret                     /* Should not reach here */

/* __report_gsfailure - report stack buffer overrun */
.global __report_gsfailure
__report_gsfailure:
    /* Report stack buffer overrun and terminate */
    /* This should call into the Windows error reporting mechanism */
    brk #0x2                /* Trigger debug break with different code */
    ret                     /* Should not reach here */

/* _CallSettingFrame - call a function with specific frame setup */
.global _CallSettingFrame
_CallSettingFrame:
    /* Inputs:
     * x0 = function to call
     * x1 = frame pointer
     * x2 = argument 1
     * x3 = argument 2
     */
    stp x29, x30, [sp, #-16]!   /* Save frame pointer and link register */
    mov x29, x1                 /* Set new frame pointer */
    mov x0, x2                  /* Move arg1 to x0 */
    mov x1, x3                  /* Move arg2 to x1 */
    blr x0                      /* Call the function */
    ldp x29, x30, [sp], #16     /* Restore frame pointer and link register */
    ret
