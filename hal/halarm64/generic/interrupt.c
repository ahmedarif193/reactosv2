/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Interrupt Handling Framework
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Exception Syndrome Register (ESR_EL1) bits */
#define ESR_EL1_EC_MASK         0xFC000000      /* Exception Class */
#define ESR_EL1_EC_SHIFT        26
#define ESR_EL1_ISS_MASK        0x01FFFFFF      /* Instruction Specific Syndrome */

/* ARM64 Exception Classes */
#define ESR_EL1_EC_UNKNOWN      0x00            /* Unknown reason */
#define ESR_EL1_EC_WFI_WFE      0x01            /* WFI or WFE instruction */
#define ESR_EL1_EC_CP15_32      0x03            /* MCR/MRC to CP15 */
#define ESR_EL1_EC_CP15_64      0x04            /* MCRR/MRRC to CP15 */
#define ESR_EL1_EC_CP14_MR      0x05            /* MCR/MRC to CP14 */
#define ESR_EL1_EC_CP14_LS      0x06            /* LDC/STC to CP14 */
#define ESR_EL1_EC_FP_ASIMD     0x07            /* FP/ASIMD */
#define ESR_EL1_EC_CP10_ID      0x08            /* MRC to CP10 */
#define ESR_EL1_EC_CP14_64      0x0C            /* MCRR/MRRC to CP14 */
#define ESR_EL1_EC_SVC64        0x15            /* SVC instruction execution in AArch64 */
#define ESR_EL1_EC_HVC64        0x16            /* HVC instruction execution in AArch64 */
#define ESR_EL1_EC_SMC64        0x17            /* SMC instruction execution in AArch64 */
#define ESR_EL1_EC_SYS64        0x18            /* MSR/MRS/System instruction */
#define ESR_EL1_EC_IABT_LOW     0x20            /* Instruction Abort from lower EL */
#define ESR_EL1_EC_IABT_CUR     0x21            /* Instruction Abort from current EL */
#define ESR_EL1_EC_PC_ALIGN     0x22            /* PC alignment fault */
#define ESR_EL1_EC_DABT_LOW     0x24            /* Data Abort from lower EL */
#define ESR_EL1_EC_DABT_CUR     0x25            /* Data Abort from current EL */
#define ESR_EL1_EC_SP_ALIGN     0x26            /* SP alignment fault */
#define ESR_EL1_EC_FP_EXC32     0x28            /* FP exception (AArch32) */
#define ESR_EL1_EC_FP_EXC64     0x2C            /* FP exception (AArch64) */
#define ESR_EL1_EC_SERROR       0x2F            /* SError interrupt */
#define ESR_EL1_EC_BREAKPT_LOW  0x30            /* Breakpoint from lower EL */
#define ESR_EL1_EC_BREAKPT_CUR  0x31            /* Breakpoint from current EL */
#define ESR_EL1_EC_SOFTSTP_LOW  0x32            /* Software Step from lower EL */
#define ESR_EL1_EC_SOFTSTP_CUR  0x33            /* Software Step from current EL */
#define ESR_EL1_EC_WATCHPT_LOW  0x34            /* Watchpoint from lower EL */
#define ESR_EL1_EC_WATCHPT_CUR  0x35            /* Watchpoint from current EL */
#define ESR_EL1_EC_BKPT32       0x38            /* BKPT instruction (AArch32) */
#define ESR_EL1_EC_BRK64        0x3C            /* BRK instruction (AArch64) */

/* ARM64 Exception Vector Table Offsets */
#define ARM64_VECTOR_SIZE       0x80            /* Each vector is 128 bytes */
#define ARM64_VECTOR_CURR_SP0_SYNC      0x000   /* Current EL with SP_EL0 - Synchronous */
#define ARM64_VECTOR_CURR_SP0_IRQ       0x080   /* Current EL with SP_EL0 - IRQ */
#define ARM64_VECTOR_CURR_SP0_FIQ       0x100   /* Current EL with SP_EL0 - FIQ */
#define ARM64_VECTOR_CURR_SP0_SERROR    0x180   /* Current EL with SP_EL0 - SError */
#define ARM64_VECTOR_CURR_SPX_SYNC      0x200   /* Current EL with SP_ELx - Synchronous */
#define ARM64_VECTOR_CURR_SPX_IRQ       0x280   /* Current EL with SP_ELx - IRQ */
#define ARM64_VECTOR_CURR_SPX_FIQ       0x300   /* Current EL with SP_ELx - FIQ */
#define ARM64_VECTOR_CURR_SPX_SERROR    0x380   /* Current EL with SP_ELx - SError */
#define ARM64_VECTOR_LOWER_AARCH64_SYNC 0x400   /* Lower EL using AArch64 - Synchronous */
#define ARM64_VECTOR_LOWER_AARCH64_IRQ  0x480   /* Lower EL using AArch64 - IRQ */
#define ARM64_VECTOR_LOWER_AARCH64_FIQ  0x500   /* Lower EL using AArch64 - FIQ */
#define ARM64_VECTOR_LOWER_AARCH64_SERROR 0x580 /* Lower EL using AArch64 - SError */
#define ARM64_VECTOR_LOWER_AARCH32_SYNC 0x600   /* Lower EL using AArch32 - Synchronous */
#define ARM64_VECTOR_LOWER_AARCH32_IRQ  0x680   /* Lower EL using AArch32 - IRQ */
#define ARM64_VECTOR_LOWER_AARCH32_FIQ  0x700   /* Lower EL using AArch32 - FIQ */
#define ARM64_VECTOR_LOWER_AARCH32_SERROR 0x780 /* Lower EL using AArch32 - SError */

/* Maximum number of interrupt handlers */
#define MAX_INTERRUPT_HANDLERS  256

/* TYPES ******************************************************************/

/* ARM64 Exception Frame - saved by exception entry code */
typedef struct _ARM64_EXCEPTION_FRAME
{
    ULONG64 X[31];          /* General purpose registers X0-X30 */
    ULONG64 SP;             /* Stack pointer */
    ULONG64 PC;             /* Program counter (ELR_EL1) */
    ULONG64 PSTATE;         /* Processor state (SPSR_EL1) */
    ULONG64 ESR;            /* Exception syndrome register (ESR_EL1) */
    ULONG64 FAR_EL1;        /* Fault address register (FAR_EL1) */
} ARM64_EXCEPTION_FRAME, *PARM64_EXCEPTION_FRAME;

/* Interrupt handler function pointer */
typedef BOOLEAN (*PINTERRUPT_HANDLER)(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG InterruptNumber);

/* Interrupt handler entry */
typedef struct _INTERRUPT_HANDLER_ENTRY
{
    PINTERRUPT_HANDLER Handler;
    PVOID Context;
    BOOLEAN Registered;
    ULONG Usage;
} INTERRUPT_HANDLER_ENTRY, *PINTERRUPT_HANDLER_ENTRY;

/* GLOBALS ********************************************************************/

/* Exception vector table (must be aligned to 2KB boundary) */
static UCHAR ExceptionVectorTable[2048] __attribute__((aligned(2048)));

/* Interrupt handler table */
static INTERRUPT_HANDLER_ENTRY InterruptHandlers[MAX_INTERRUPT_HANDLERS];
static BOOLEAN InterruptSystemInitialized = FALSE;

/* Statistics */
static ULONG64 TotalInterrupts = 0;
static ULONG64 SpuriousInterrupts = 0;
static ULONG64 UnhandledExceptions = 0;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Get exception class from ESR register
 */
FORCEINLINE
ULONG
HalGetExceptionClass(
    IN ULONG64 ExceptionSyndrome)
{
    return (ULONG)((ExceptionSyndrome & ESR_EL1_EC_MASK) >> ESR_EL1_EC_SHIFT);
}

/*
 * @brief Get instruction specific syndrome from ESR register
 */
FORCEINLINE
ULONG
HalGetInstructionSyndrome(
    IN ULONG64 ExceptionSyndrome)
{
    return (ULONG)(ExceptionSyndrome & ESR_EL1_ISS_MASK);
}

/*
 * @brief ARM64 Synchronous Exception Handler
 *
 * This handles synchronous exceptions like system calls, data/instruction aborts,
 * alignment faults, undefined instructions, etc.
 */
VOID
NTAPI
HalSynchronousExceptionHandler(
    IN PARM64_EXCEPTION_FRAME ExceptionFrame)
{
    ULONG ExceptionClass;
    ULONG InstructionSyndrome;

    ExceptionClass = HalGetExceptionClass(ExceptionFrame->ESR);
    InstructionSyndrome = HalGetInstructionSyndrome(ExceptionFrame->ESR);

    DPRINT("ARM64 Synchronous Exception: Class=0x%02lx, ISS=0x%06lx, PC=0x%016llx, FAR=0x%016llx\n",
           ExceptionClass, InstructionSyndrome, ExceptionFrame->PC, ExceptionFrame->FAR_EL1);

    switch (ExceptionClass)
    {
        case ESR_EL1_EC_SVC64:
            /* System call from AArch64 state */
            DPRINT("System call: SVC #%lu\n", InstructionSyndrome & 0xFFFF);
            /* TODO: Handle system call dispatch */
            break;

        case ESR_EL1_EC_DABT_LOW:
        case ESR_EL1_EC_DABT_CUR:
            /* Data abort - memory access violation */
            DPRINT1("Data Abort: PC=0x%016llx, FAR=0x%016llx, ISS=0x%06lx\n",
                   ExceptionFrame->PC, ExceptionFrame->FAR_EL1, InstructionSyndrome);

            /* TODO: Handle data abort - potentially recoverable page fault */
            break;

        case ESR_EL1_EC_IABT_LOW:
        case ESR_EL1_EC_IABT_CUR:
            /* Instruction abort - instruction fetch violation */
            DPRINT1("Instruction Abort: PC=0x%016llx, FAR=0x%016llx, ISS=0x%06lx\n",
                   ExceptionFrame->PC, ExceptionFrame->FAR_EL1, InstructionSyndrome);
            break;

        case ESR_EL1_EC_PC_ALIGN:
            /* PC alignment fault */
            DPRINT1("PC Alignment Fault: PC=0x%016llx\n", ExceptionFrame->PC);
            break;

        case ESR_EL1_EC_SP_ALIGN:
            /* Stack pointer alignment fault */
            DPRINT1("SP Alignment Fault: SP=0x%016llx\n", ExceptionFrame->SP);
            break;

        case ESR_EL1_EC_UNKNOWN:
            /* Unknown exception reason */
            DPRINT1("Unknown Exception: PC=0x%016llx, PSTATE=0x%016llx\n",
                   ExceptionFrame->PC, ExceptionFrame->PSTATE);
            break;

        case ESR_EL1_EC_BRK64:
            /* BRK instruction - software breakpoint */
            DPRINT("Software Breakpoint: BRK #%lu at PC=0x%016llx\n",
                   InstructionSyndrome & 0xFFFF, ExceptionFrame->PC);
            /* TODO: Handle debugger breakpoint */
            break;

        default:
            DPRINT1("Unhandled Synchronous Exception: Class=0x%02lx, PC=0x%016llx\n",
                   ExceptionClass, ExceptionFrame->PC);
            break;
    }

    UnhandledExceptions++;

    /* TODO: For now, halt the system on unhandled exceptions
     * In a full implementation, this would:
     * 1. Try to handle recoverable exceptions (page faults, etc.)
     * 2. Deliver signals to user processes for illegal operations
     * 3. Call kernel debugger for system exceptions
     * 4. Generate blue screen for unrecoverable errors
     */
    HalHaltSystem();
}

/*
 * @brief ARM64 IRQ (Interrupt) Handler
 *
 * This handles external interrupts delivered through the GIC.
 */
VOID
NTAPI
HalIrqHandler(
    IN PARM64_EXCEPTION_FRAME ExceptionFrame)
{
    ULONG InterruptNumber;
    PINTERRUPT_HANDLER_ENTRY HandlerEntry;
    BOOLEAN Handled = FALSE;

    /* Increment total interrupt count */
    TotalInterrupts++;

    /* Acknowledge the interrupt and get the interrupt number from GIC */
    InterruptNumber = HalAcknowledgeInterrupt();

    if (InterruptNumber == GIC_SPURIOUS_INTERRUPT)
    {
        /* Spurious interrupt - no real interrupt pending */
        SpuriousInterrupts++;
        DPRINT("Spurious interrupt detected\n");
        return;
    }

    DPRINT("ARM64 IRQ: Interrupt %lu (Total: %llu)\n", InterruptNumber, TotalInterrupts);

    /* Check if we have a handler for this interrupt */
    if (InterruptNumber < MAX_INTERRUPT_HANDLERS)
    {
        HandlerEntry = &InterruptHandlers[InterruptNumber];

        if (HandlerEntry->Registered && HandlerEntry->Handler)
        {
            /* Create a KTRAP_FRAME from the exception frame for compatibility */
            KTRAP_FRAME TrapFrame = {0};
            /* TODO: Convert ARM64_EXCEPTION_FRAME to KTRAP_FRAME format */

            /* Call the registered interrupt handler */
            Handled = HandlerEntry->Handler(&TrapFrame, InterruptNumber);
            HandlerEntry->Usage++;

            if (Handled)
            {
                DPRINT("Interrupt %lu handled successfully\n", InterruptNumber);
            }
            else
            {
                DPRINT1("Interrupt handler for IRQ %lu returned FALSE\n", InterruptNumber);
            }
        }
        else
        {
            DPRINT1("No handler registered for interrupt %lu\n", InterruptNumber);
        }
    }
    else
    {
        DPRINT1("Invalid interrupt number: %lu (max: %u)\n", InterruptNumber, MAX_INTERRUPT_HANDLERS);
    }

    /* Signal end of interrupt to the GIC */
    HalEndOfInterrupt(InterruptNumber);

    if (!Handled)
    {
        /* TODO: Handle unhandled interrupt - for now just log it */
        DPRINT1("Unhandled interrupt %lu\n", InterruptNumber);
    }
}

/*
 * @brief ARM64 FIQ (Fast Interrupt) Handler
 *
 * This handles fast interrupts, typically used for high-priority,
 * low-latency interrupt processing.
 */
VOID
NTAPI
HalFiqHandler(
    IN PARM64_EXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(ExceptionFrame);

    /* FIQ handling - typically used for secure world or critical interrupts */
    DPRINT("ARM64 FIQ received\n");

    /* TODO: Implement FIQ handling if needed for the platform */
}

/*
 * @brief ARM64 SError (System Error) Handler
 *
 * This handles asynchronous system errors like memory system errors,
 * bus errors, and other system-level faults.
 */
VOID
NTAPI
HalSErrorHandler(
    IN PARM64_EXCEPTION_FRAME ExceptionFrame)
{
    DPRINT1("ARM64 SError: PC=0x%016llx, ESR=0x%016llx, FAR=0x%016llx\n",
           ExceptionFrame->PC, ExceptionFrame->ESR, ExceptionFrame->FAR_EL1);

    /* SError indicates a serious system error */
    UnhandledExceptions++;

    /* TODO: Attempt error recovery or log the error for analysis */
    /* For now, halt the system as SErrors are typically fatal */
    HalHaltSystem();
}

/*
 * @brief Register an interrupt handler
 */
BOOLEAN
NTAPI
HalRegisterInterruptHandler(
    IN ULONG InterruptNumber,
    IN PVOID Handler,
    IN PVOID Context)
{
    PINTERRUPT_HANDLER_ENTRY HandlerEntry;

    if (InterruptNumber >= MAX_INTERRUPT_HANDLERS || !Handler)
    {
        DPRINT1("Invalid interrupt registration: IRQ=%lu, Handler=%p\n", InterruptNumber, Handler);
        return FALSE;
    }

    HandlerEntry = &InterruptHandlers[InterruptNumber];

    if (HandlerEntry->Registered)
    {
        DPRINT1("Interrupt handler already registered for IRQ %lu\n", InterruptNumber);
        return FALSE;
    }

    /* Register the handler */
    HandlerEntry->Handler = (PINTERRUPT_HANDLER)Handler;
    HandlerEntry->Context = Context;
    HandlerEntry->Registered = TRUE;
    HandlerEntry->Usage = 0;

    DPRINT("Registered interrupt handler for IRQ %lu\n", InterruptNumber);
    return TRUE;
}

/*
 * @brief Unregister an interrupt handler
 */
BOOLEAN
NTAPI
HalUnregisterInterruptHandler(
    IN ULONG InterruptNumber)
{
    PINTERRUPT_HANDLER_ENTRY HandlerEntry;

    if (InterruptNumber >= MAX_INTERRUPT_HANDLERS)
    {
        return FALSE;
    }

    HandlerEntry = &InterruptHandlers[InterruptNumber];

    if (!HandlerEntry->Registered)
    {
        return FALSE;
    }

    /* Unregister the handler */
    HandlerEntry->Handler = NULL;
    HandlerEntry->Context = NULL;
    HandlerEntry->Registered = FALSE;

    DPRINT("Unregistered interrupt handler for IRQ %lu (used %lu times)\n",
           InterruptNumber, HandlerEntry->Usage);

    return TRUE;
}

/*
 * @brief Initialize the ARM64 exception vector table
 */
static
VOID
HalInitializeExceptionVectors(VOID)
{
    PVOID VectorTableAddress;

    /* TODO: The actual exception vector table implementation would be in assembly
     * and would save/restore processor state and call the appropriate C handler.
     * For now, we just set up the base address. */

    /* Ensure vector table is properly aligned (2KB boundary) */
    VectorTableAddress = (PVOID)ExceptionVectorTable;

    DPRINT("ARM64 Exception Vector Table at: %p\n", VectorTableAddress);

    /* Set the Vector Base Address Register (VBAR_EL1) */
    __asm__ __volatile__ (
        "msr VBAR_EL1, %0\n"
        "isb\n"
        :: "r"((ULONG64)VectorTableAddress)
    );

    DPRINT("ARM64 exception vectors initialized\n");
}

/*
 * @brief Initialize ARM64 interrupt handling system
 */
BOOLEAN
NTAPI
HalInitializeInterruptSystem(VOID)
{
    ULONG i;

    DPRINT("Initializing ARM64 interrupt handling system\n");

    /* Clear interrupt handler table */
    for (i = 0; i < MAX_INTERRUPT_HANDLERS; i++)
    {
        InterruptHandlers[i].Handler = NULL;
        InterruptHandlers[i].Context = NULL;
        InterruptHandlers[i].Registered = FALSE;
        InterruptHandlers[i].Usage = 0;
    }

    /* Initialize statistics */
    TotalInterrupts = 0;
    SpuriousInterrupts = 0;
    UnhandledExceptions = 0;

    /* Initialize exception vector table */
    HalInitializeExceptionVectors();

    /* Register system timer interrupt handler if timer is initialized */
    if (HalGetTimerFrequency() != 0)
    {
        HalRegisterInterruptHandler(30, HalTimerInterruptHandler, NULL);
        DPRINT("Registered ARM64 timer interrupt handler (IRQ 30)\n");
    }

    InterruptSystemInitialized = TRUE;

    DPRINT("ARM64 interrupt system initialization complete\n");
    return TRUE;
}

/*
 * @brief Get interrupt system statistics
 */
VOID
NTAPI
HalGetInterruptStatistics(
    OUT PULONG64 TotalInterruptCount,
    OUT PULONG64 SpuriousInterruptCount,
    OUT PULONG64 UnhandledExceptionCount)
{
    if (TotalInterruptCount)
        *TotalInterruptCount = TotalInterrupts;

    if (SpuriousInterruptCount)
        *SpuriousInterruptCount = SpuriousInterrupts;

    if (UnhandledExceptionCount)
        *UnhandledExceptionCount = UnhandledExceptions;
}

/* EOF */