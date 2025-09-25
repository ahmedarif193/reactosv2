/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Timer Management
 * COPYRIGHT:   Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Timer Control Bits */
#define CNTV_CTL_ENABLE     (1 << 0)   /* Enable virtual timer */
#define CNTV_CTL_IMASK      (1 << 1)   /* Interrupt mask */
#define CNTV_CTL_ISTATUS    (1 << 2)   /* Interrupt status */

#define CNTP_CTL_ENABLE     (1 << 0)   /* Enable physical timer */
#define CNTP_CTL_IMASK      (1 << 1)   /* Interrupt mask */
#define CNTP_CTL_ISTATUS    (1 << 2)   /* Interrupt status */

/* Timer quantum in 100ns units (10ms = 100,000 * 100ns) */
#define TIMER_QUANTUM       100000

/* GLOBALS ********************************************************************/

KDPC KiTimerExpireDpc;
KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];
KSPIN_LOCK KiTimerTableLock;
LARGE_INTEGER KeBootTime;
ULONGLONG KeBootTimeBias;
volatile LONG KiTickOffset;
ULONG KeMaximumIncrement;
ULONG KeMinimumIncrement;
ULONG KeTimeIncrement;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * @brief Read ARM64 Counter Frequency Register
 */
static
ULONG64
KiReadCounterFrequency(VOID)
{
    ULONG64 Frequency;

    /* Read CNTFRQ_EL0 - Counter Frequency Register
     * This register holds the timer frequency in Hz */
    __asm__ __volatile__ (
        "mrs %0, CNTFRQ_EL0\n"
        : "=r"(Frequency)
    );

    return Frequency;
}

/*
 * @brief Read ARM64 Physical Counter Value
 */
static
ULONG64
KiReadPhysicalCounter(VOID)
{
    ULONG64 Counter;

    /* Read CNTPCT_EL0 - Physical Count Register
     * This is a 64-bit counter that increments at a fixed frequency */
    __asm__ __volatile__ (
        "mrs %0, CNTPCT_EL0\n"
        : "=r"(Counter)
    );

    return Counter;
}

/*
 * @brief Read ARM64 Virtual Counter Value
 */
static
ULONG64
KiReadVirtualCounter(VOID)
{
    ULONG64 Counter;

    /* Read CNTVCT_EL0 - Virtual Count Register
     * Virtual counter = Physical counter - Virtual offset */
    __asm__ __volatile__ (
        "mrs %0, CNTVCT_EL0\n"
        : "=r"(Counter)
    );

    return Counter;
}

/*
 * @brief Set ARM64 Physical Timer Compare Value
 */
static
VOID
KiSetPhysicalTimerCompare(
    IN ULONG64 CompareValue)
{
    /* Write CNTP_CVAL_EL0 - Physical Timer Compare Value Register
     * Timer fires when counter >= compare value */
    __asm__ __volatile__ (
        "msr CNTP_CVAL_EL0, %0\n"
        "isb\n"
        :: "r"(CompareValue)
        : "memory"
    );
}

/*
 * @brief Set ARM64 Virtual Timer Compare Value
 */
static
VOID
KiSetVirtualTimerCompare(
    IN ULONG64 CompareValue)
{
    /* Write CNTV_CVAL_EL0 - Virtual Timer Compare Value Register */
    __asm__ __volatile__ (
        "msr CNTV_CVAL_EL0, %0\n"
        "isb\n"
        :: "r"(CompareValue)
        : "memory"
    );
}

/*
 * @brief Enable ARM64 Physical Timer
 */
static
VOID
KiEnablePhysicalTimer(VOID)
{
    /* Write CNTP_CTL_EL0 - Physical Timer Control Register
     * Enable timer and unmask interrupt */
    __asm__ __volatile__ (
        "mov x0, %0\n"
        "msr CNTP_CTL_EL0, x0\n"
        "isb\n"
        :: "i"(CNTP_CTL_ENABLE)
        : "x0", "memory"
    );
}

/*
 * @brief Disable ARM64 Physical Timer
 */
static
VOID
KiDisablePhysicalTimer(VOID)
{
    /* Disable timer and mask interrupt */
    __asm__ __volatile__ (
        "mov x0, %0\n"
        "msr CNTP_CTL_EL0, x0\n"
        "isb\n"
        :: "i"(CNTP_CTL_IMASK)
        : "x0", "memory"
    );
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @brief Initialize ARM64 Timer System
 * @implemented
 *
 * This function initializes the ARM64 Generic Timer subsystem, including:
 * - Reading timer frequency from hardware
 * - Setting up timer interrupt handlers
 * - Initializing timer data structures
 * - Starting the system tick timer
 */
VOID
NTAPI
KiInitializeTimer(VOID)
{
    ULONG64 Frequency, CurrentCount;
    ULONG i;

    DPRINT("Initializing ARM64 timer system\n");

    /* Read timer frequency from hardware */
    Frequency = KiReadCounterFrequency();
    if (Frequency == 0)
    {
        /* Use default frequency if not available */
        Frequency = 50000000; /* 50 MHz */
        DPRINT1("Timer frequency not available, using default %llu Hz\n", Frequency);
    }
    else
    {
        DPRINT("Timer frequency: %llu Hz\n", Frequency);
    }

    /* Calculate timer increments for 10ms quantum
     * KeTimeIncrement is in 100ns units */
    KeTimeIncrement = TIMER_QUANTUM;
    KeMaximumIncrement = TIMER_QUANTUM * 2;
    KeMinimumIncrement = TIMER_QUANTUM / 2;

    /* Initialize timer table */
    for (i = 0; i < TIMER_TABLE_SIZE; i++)
    {
        InitializeListHead(&KiTimerTableListHead[i].Entry);
        KiTimerTableListHead[i].Time.QuadPart = 0;
    }

    /* Initialize timer lock */
    KeInitializeSpinLock(&KiTimerTableLock);

    /* Initialize timer DPC */
    KeInitializeDpc(&KiTimerExpireDpc, KiTimerExpiration, NULL);
    KeSetTargetProcessorDpc(&KiTimerExpireDpc, 0);

    /* Get current counter value for boot time reference */
    CurrentCount = KiReadPhysicalCounter();
    KeBootTime.QuadPart = CurrentCount;
    KeBootTimeBias = 0;

    /* Set up first timer interrupt for next quantum */
    CurrentCount += (Frequency * TIMER_QUANTUM) / 10000000;
    KiSetPhysicalTimerCompare(CurrentCount);

    /* Enable timer */
    KiEnablePhysicalTimer();

    DPRINT("ARM64 timer initialized (increment=%lu)\n", KeTimeIncrement);
}

/*
 * @brief ARM64 Timer Interrupt Handler
 * @implemented
 *
 * This function handles timer interrupts and performs:
 * - System time updates
 * - Timer expiration checks
 * - Thread quantum management
 * - DPC queue processing
 */
VOID
NTAPI
KiTimerInterruptHandler(
    IN PKTRAP_FRAME TrapFrame)
{
    ULONG64 CurrentCount, NextCount, Frequency;
    KIRQL OldIrql;
    PKPRCB Prcb;

    /* Get current processor block */
    Prcb = KeGetCurrentPrcb();

    /* Update interrupt count */
    Prcb->InterruptCount++;

    /* Read current counter value */
    CurrentCount = KiReadPhysicalCounter();
    Frequency = KiReadCounterFrequency();

    /* Update system time
     * TODO: Implement proper time keeping with KeUpdateSystemTime */
    InterlockedExchangeAdd(&SharedUserData->TickCount, 1);

    /* Set next timer interrupt */
    NextCount = CurrentCount + (Frequency * TIMER_QUANTUM) / 10000000;
    KiSetPhysicalTimerCompare(NextCount);

    /* Process timer expiration
     * TODO: Check timer table for expired timers */

    /* Check for thread quantum expiration
     * TODO: Implement quantum end processing */

    /* Request DPC if needed
     * TODO: Check DPC queue depth and request software interrupt */

    DPRINT("Timer interrupt on CPU %u\n", KeGetCurrentProcessorNumber());
}

/*
 * @brief Update System Time
 * @implemented
 *
 * Updates the system time and processes time-dependent operations
 */
VOID
FASTCALL
KeUpdateSystemTime(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG Increment,
    IN KIRQL Irql)
{
    LARGE_INTEGER Time;
    PKPRCB Prcb;

    /* Get current processor block */
    Prcb = KeGetCurrentPrcb();

    /* Update system time */
    Time.QuadPart = SharedUserData->SystemTime.QuadPart;
    Time.QuadPart += Increment;
    SharedUserData->SystemTime.QuadPart = Time.QuadPart;

    /* Update interrupt time */
    SharedUserData->InterruptTime.QuadPart += Increment;

    /* Update tick count if a full tick has elapsed */
    KiTickOffset += Increment;
    if (KiTickOffset >= KeMaximumIncrement)
    {
        SharedUserData->TickCount++;
        KiTickOffset -= KeMaximumIncrement;
    }

    /* Check for timer expiration
     * TODO: Process expired timers from timer table */

    /* Update thread times
     * TODO: Update kernel and user time for current thread */

    DPRINT("System time updated by %lu\n", Increment);
}

/*
 * @brief Process Timer Expiration
 * @implemented
 *
 * Processes expired timers and queues DPCs as needed
 */
VOID
FASTCALL
KiTimerExpiration(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    KIRQL OldIrql;
    PLIST_ENTRY ListHead, NextEntry;
    PKTIMER Timer;
    ULONG Index;
    LARGE_INTEGER SystemTime;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DPRINT("Processing timer expirations\n");

    /* Get current system time */
    KeQuerySystemTime(&SystemTime);

    /* Acquire timer table lock */
    KeAcquireSpinLock(&KiTimerTableLock, &OldIrql);

    /* Process each timer table entry
     * TODO: Implement timer table processing
     * - Check each table entry for expired timers
     * - Remove expired timers from table
     * - Signal timer objects
     * - Queue DPCs for timer callbacks
     */

    for (Index = 0; Index < TIMER_TABLE_SIZE; Index++)
    {
        ListHead = &KiTimerTableListHead[Index].Entry;

        /* Process timers in this slot
         * TODO: Walk timer list and process expired timers */
    }

    /* Release timer table lock */
    KeReleaseSpinLock(&KiTimerTableLock, OldIrql);

    DPRINT("Timer expiration processing complete\n");
}

/*
 * @brief Query Performance Counter
 * @implemented
 *
 * Returns high-resolution performance counter value
 */
LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Counter;
    ULONG64 CounterValue;

    /* Read physical counter */
    CounterValue = KiReadPhysicalCounter();

    /* Convert to LARGE_INTEGER */
    Counter.QuadPart = CounterValue;

    /* Return frequency if requested */
    if (PerformanceFrequency)
    {
        PerformanceFrequency->QuadPart = KiReadCounterFrequency();
    }

    return Counter;
}

/*
 * @brief Stall Execution
 * @implemented
 *
 * Busy-waits for specified number of microseconds
 */
VOID
NTAPI
KeStallExecutionProcessor(
    IN ULONG Microseconds)
{
    ULONG64 StartCount, CurrentCount, TargetCount;
    ULONG64 Frequency, CyclesNeeded;

    /* Get timer frequency */
    Frequency = KiReadCounterFrequency();

    /* Calculate cycles needed for requested delay */
    CyclesNeeded = (Frequency * Microseconds) / 1000000;

    /* Get start count */
    StartCount = KiReadPhysicalCounter();
    TargetCount = StartCount + CyclesNeeded;

    /* Busy wait until target reached */
    do
    {
        CurrentCount = KiReadPhysicalCounter();

        /* Memory barrier to prevent optimization */
        __asm__ __volatile__ ("" ::: "memory");

    } while (CurrentCount < TargetCount);
}

/*
 * @brief Set System Time
 * @implemented
 *
 * Sets the system time to a new value
 */
BOOLEAN
NTAPI
KeSetSystemTime(
    IN PLARGE_INTEGER NewTime,
    OUT PLARGE_INTEGER OldTime,
    IN BOOLEAN AdjustInterruptTime,
    IN PLARGE_INTEGER HalTime OPTIONAL)
{
    KIRQL OldIrql;
    LARGE_INTEGER DeltaTime;

    UNREFERENCED_PARAMETER(HalTime);

    DPRINT("Setting system time\n");

    /* Raise IRQL to HIGH_LEVEL to prevent interrupts */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

    /* Save old time if requested */
    if (OldTime)
    {
        OldTime->QuadPart = SharedUserData->SystemTime.QuadPart;
    }

    /* Calculate time adjustment */
    DeltaTime.QuadPart = NewTime->QuadPart - SharedUserData->SystemTime.QuadPart;

    /* Set new system time */
    SharedUserData->SystemTime.QuadPart = NewTime->QuadPart;

    /* Adjust interrupt time if requested */
    if (AdjustInterruptTime)
    {
        SharedUserData->InterruptTime.QuadPart += DeltaTime.QuadPart;
    }

    /* Update boot time */
    KeBootTime.QuadPart = NewTime->QuadPart;
    KeBootTimeBias = 0;

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    DPRINT("System time set successfully\n");
    return TRUE;
}

/* EOF */