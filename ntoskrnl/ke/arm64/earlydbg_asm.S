/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Early Boot Debug Assembly Helper
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* PL011 UART Base Address and Register Offsets */
#define UART_BASE       0x09000000
#define UART_DR         0x00    /* Data Register */
#define UART_FR         0x18    /* Flag Register */
#define UART_FR_TXFF    0x20    /* Transmit FIFO Full */
#define UART_FR_BUSY    0x08    /* UART Busy */

.text
.align 4

/**
 * early_uart_putc - Output a single character to UART
 *
 * Input:
 *   w0 = character to output
 *
 * Clobbers: x1, x2, x3
 */
.global early_uart_putc
/* .type early_uart_putc, %function - not supported by MinGW */
early_uart_putc:
    /* Load UART base address */
    mov     x2, #UART_BASE

    /* Load flag register address */
    add     x3, x2, #UART_FR

1:  /* Wait for transmit FIFO to have space */
    ldr     w1, [x3]
    tst     w1, #UART_FR_TXFF
    b.ne    1b

    /* Write character to data register */
    strb    w0, [x2, #UART_DR]

    /* Small delay for transmission */
    mov     x1, #1000
2:  sub     x1, x1, #1
    cbnz    x1, 2b

    ret

/**
 * early_uart_puts - Output a null-terminated string to UART
 *
 * Input:
 *   x0 = pointer to null-terminated string
 *
 * Clobbers: x0, x1, x2, x3, x4
 */
.global early_uart_puts
/* .type early_uart_puts, %function - not supported by MinGW */
early_uart_puts:
    /* Save return address */
    mov     x4, x30

    /* Save string pointer */
    mov     x1, x0

1:  /* Load next character */
    ldrb    w0, [x1], #1

    /* Check for null terminator */
    cbz     w0, 2f

    /* Check for newline and send CR first if needed */
    cmp     w0, #'\n'
    b.ne    3f

    /* Send CR before LF */
    mov     w0, #'\r'
    bl      early_uart_putc
    mov     w0, #'\n'

3:  /* Send the character */
    bl      early_uart_putc

    /* Loop for next character */
    b       1b

2:  /* Restore return address and return */
    mov     x30, x4
    ret

/**
 * early_uart_puthex - Output a hex value to UART
 *
 * Input:
 *   x0 = value to output
 *   w1 = number of nibbles (1-16)
 *
 * Clobbers: x0, x1, x2, x3, x4, x5, x6
 */
.global early_uart_puthex
/* .type early_uart_puthex, %function - not supported by MinGW */
early_uart_puthex:
    /* Save return address */
    mov     x6, x30

    /* Save value and nibble count */
    mov     x4, x0
    mov     w5, w1

    /* Output "0x" prefix */
    mov     w0, #'0'
    bl      early_uart_putc
    mov     w0, #'x'
    bl      early_uart_putc

    /* Clamp nibbles to 1-16 */
    cmp     w5, #16
    b.le    1f
    mov     w5, #16
1:  cmp     w5, #1
    b.ge    2f
    mov     w5, #1

2:  /* Calculate shift amount for first nibble */
    sub     w1, w5, #1
    lsl     w1, w1, #2      /* Multiply by 4 */

3:  /* Extract and print each nibble */
    lsr     x0, x4, x1      /* Shift to get current nibble */
    and     w0, w0, #0xF    /* Mask to get just the nibble */

    /* Convert to hex character */
    cmp     w0, #10
    b.lo    4f
    add     w0, w0, #('A' - 10)
    b       5f
4:  add     w0, w0, #'0'

5:  /* Output the character */
    bl      early_uart_putc

    /* Move to next nibble */
    subs    w1, w1, #4
    b.pl    3b

    /* Restore return address and return */
    mov     x30, x6
    ret

/**
 * early_debug_msg - Output a debug message with location info
 *
 * This macro can be used in assembly code to output debug messages
 *
 * Usage:
 *   early_debug_msg "Boot stage 1 complete"
 */
.macro early_debug_msg message
    .pushsection .rodata, "a"
1:  .asciz "\message\n"
    .popsection

    /* Preserve registers we'll use */
    stp     x0, x30, [sp, #-16]!

    /* Load string address and call puts */
    adr     x0, 1b
    bl      early_uart_puts

    /* Restore registers */
    ldp     x0, x30, [sp], #16
.endm

/**
 * early_debug_hex - Output a register value as hex
 *
 * Usage:
 *   early_debug_hex x0, "Register X0"
 */
.macro early_debug_hex reg, label
    .pushsection .rodata, "a"
1:  .asciz "\label: "
    .popsection

    /* Preserve registers */
    stp     x0, x1, [sp, #-32]!
    stp     x2, x30, [sp, #16]

    /* Output label */
    adr     x0, 1b
    bl      early_uart_puts

    /* Output hex value */
    mov     x0, \reg
    mov     w1, #16         /* 16 nibbles for 64-bit value */
    bl      early_uart_puthex

    /* Output newline */
    mov     w0, #'\n'
    bl      early_uart_putc

    /* Restore registers */
    ldp     x2, x30, [sp, #16]
    ldp     x0, x1, [sp], #32
.endm

/* End of file */