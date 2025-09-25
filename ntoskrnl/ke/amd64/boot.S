/*
 * FILE:            ntoskrnl/ke/amd64/boot.S
 * COPYRIGHT:       See COPYING in the top level directory
 * PURPOSE:         FreeLDR Wrapper Bootstrap Code and Bootstrap Trampoline
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <ksamd64.inc>

EXTERN KiSystemStartupBootStack:PROC

/* GLOBALS *******************************************************************/


/* FUNCTIONS *****************************************************************/

.code64

/**
 * VOID
 * KiSwitchToBootStack(
 *     IN ULONG_PTR InitialStack<rcx>)
 */

.equ BOOT_STACK_FRAME_RESERVE, (KTRAP_FRAME_LENGTH + KEXCEPTION_FRAME_LENGTH + KSTART_FRAME_LENGTH)
.equ MISSING_RET_ADDRESS_SIZE, 8

PUBLIC KiSwitchToBootStack
.PROC KiSwitchToBootStack

    /* Setup the new stack */
    mov ax, HEX(18)
    mov ss, ax
    mov rsp, rcx

    /* Reserve space for the bootstrap trap/start frames and keep ABI alignment */
    sub rsp, BOOT_STACK_FRAME_RESERVE

    /* Account for the missing return address so the callee sees rsp%16 == 8 */
    sub rsp, MISSING_RET_ADDRESS_SIZE
    .ENDPROLOG

    jmp KiSystemStartupBootStack

.ENDP

END
