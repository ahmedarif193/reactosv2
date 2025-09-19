/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            dll/ntdll/dispatch/arm64/stubs_asm.S
 * PURPOSE:         ARM64 User-Mode Dispatchers
 * PROGRAMMER:      ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

/* FUNCTIONS *****************************************************************/

.text

/*
 * NTSTATUS
 * LdrInitializeThunk(
 *     IN PCONTEXT Context,
 *     IN PVOID NormalRoutine,
 *     IN PVOID SystemArgument1,
 *     IN PVOID SystemArgument2);
 */
PUBLIC LdrInitializeThunk
FUNC LdrInitializeThunk
    /* Save the initial context */
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3

    /* Call into C code for initialization */
    bl LdrpInit

    /* Should never return */
1:  b 1b

ENDFUNC

/*
 * VOID
 * KiUserApcDispatcher(
 *     IN PCONTEXT Context,
 *     IN PVOID NormalRoutine,
 *     IN PVOID NormalContext,
 *     IN PVOID SystemArgument1,
 *     IN PVOID SystemArgument2)
 */
PUBLIC KiUserApcDispatcher
FUNC KiUserApcDispatcher
    /* Save stack pointer */
    mov x19, sp

    /* Call the APC routine */
    mov x0, x2  /* NormalContext */
    mov x1, x3  /* SystemArgument1 */
    mov x2, x4  /* SystemArgument2 */
    blr x1      /* Call NormalRoutine */

    /* Restore context and return */
    mov x0, x19
    mov x1, #1  /* TestAlert */
    bl NtContinue

    /* Should never reach here */
1:  b 1b

ENDFUNC

/*
 * VOID
 * KiUserCallbackDispatcher(
 *     IN ULONG ApiNumber,
 *     IN PVOID InputBuffer,
 *     IN ULONG InputLength)
 */
PUBLIC KiUserCallbackDispatcher
FUNC KiUserCallbackDispatcher
    /* Get the callback table from PEB */
    /* In ntdll we access TEB directly via system register */
    mrs x3, tpidr_el0       /* Get TEB pointer from thread register */
    ldr x3, [x3, #0x60]  /* Teb->ProcessEnvironmentBlock */
    ldr x4, [x3, #0x58]  /* Peb->KernelCallbackTable */

    /* Get the callback function */
    lsl x0, x0, #3       /* ApiNumber * 8 (pointer size) */
    ldr x4, [x4, x0]     /* Get callback function pointer */

    /* Call the callback */
    mov x0, x1           /* InputBuffer */
    mov x1, x2           /* InputLength */
    blr x4

    /* Return from callback */
    mov x1, x0           /* Result */
    mov x2, #0           /* ResultLength */
    bl ZwCallbackReturn

    /* Should never return */
1:  b 1b

ENDFUNC

/*
 * VOID
 * KiUserExceptionDispatcher(
 *     IN PEXCEPTION_RECORD ExceptionRecord,
 *     IN PCONTEXT Context)
 */
PUBLIC KiUserExceptionDispatcher
FUNC KiUserExceptionDispatcher
    /* Save parameters */
    mov x19, x0  /* ExceptionRecord */
    mov x20, x1  /* Context */

    /* Call RtlDispatchException */
    bl RtlDispatchException

    /* Check if exception was handled */
    cbz x0, .Lunhandled

    /* Exception was handled, continue execution */
    mov x0, x20  /* Context */
    mov x1, #0   /* TestAlert */
    bl NtContinue
    b .Lterminate

.Lunhandled:
    /* Exception not handled, raise it */
    mov x0, x19  /* ExceptionRecord */
    mov x1, x20  /* Context */
    bl NtRaiseException

.Lterminate:
    /* Should never get here */
    mov x0, x19  /* ExceptionRecord */
    bl RtlRaiseStatus
1:  b 1b

ENDFUNC

/*
 * VOID
 * KiRaiseUserExceptionDispatcher(VOID)
 */
PUBLIC KiRaiseUserExceptionDispatcher
FUNC KiRaiseUserExceptionDispatcher
    /* Get the exception code from TEB */
    mrs x0, tpidr_el0       /* Get TEB pointer from thread register */
    ldr w0, [x0, #0x1A4]  /* Teb->ExceptionCode */

    /* Raise the exception */
    bl RtlRaiseStatus

    /* Should never return */
1:  b 1b

ENDFUNC

END