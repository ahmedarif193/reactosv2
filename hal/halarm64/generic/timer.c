/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Generic Timer Support
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* DEFINITIONS ****************************************************************/

/* ARM64 Timer System Registers */
#define CNTKCTL_EL1_EL0PCTEN    (1UL << 0)   /* EL0 Physical Counter Enable */
#define CNTKCTL_EL1_EL0VCTEN    (1UL << 1)   /* EL0 Virtual Counter Enable */
#define CNTKCTL_EL1_EVNTEN      (1UL << 2)   /* Event Stream Enable */
#define CNTKCTL_EL1_EVNTDIR     (1UL << 3)   /* Event Stream Direction */
#define CNTKCTL_EL1_EVNTI_MASK  (0xFUL << 4) /* Event Stream Trigger */

/* Timer Control Register bits */
#define CNTX_CTL_ENABLE         (1UL << 0)   /* Enable timer */
#define CNTX_CTL_IMASK          (1UL << 1)   /* Interrupt mask */
#define CNTX_CTL_ISTATUS        (1UL << 2)   /* Interrupt status */

/* Default timer frequency (will be read from hardware) */
#define ARM64_TIMER_DEFAULT_FREQ    50000000UL  /* 50MHz typical */

/* GLOBALS ********************************************************************/

static ULONG64 TimerFrequency = 0;
static BOOLEAN TimerInitialized = FALSE;
static LARGE_INTEGER PerformanceFrequency;
static ARM64_TIMER_INFO HalTimerConfiguration;
static ULONG64 BootTimeReference = 0;

/* FUNCTIONS ******************************************************************/

/*
 * @brief Initialize ARM64 Generic Timer
 */
BOOLEAN
NTAPI
HalInitializeTimer(VOID)
{
    ULONG64 CntkCtl;
    ULONG64 CurrentCount;

    DPRINT("Initializing ARM64 Generic Timer\n");

    /* Read timer frequency from CNTFRQ_EL0
     * This register contains the timer frequency in Hz
     * It should be programmed by firmware/bootloader */
    __asm__ __volatile__ (
        "mrs %0, CNTFRQ_EL0\n"
        : "=r"(TimerFrequency)
    );

    if (TimerFrequency == 0)
    {
        DPRINT1("Timer frequency not set by firmware, using default %lu Hz\n", ARM64_TIMER_DEFAULT_FREQ);
        TimerFrequency = ARM64_TIMER_DEFAULT_FREQ;
    }

    /* Validate timer frequency - should be reasonable for ARM64 systems */
    if (TimerFrequency < 1000000 || TimerFrequency > 1000000000)
    {
        DPRINT1("Invalid timer frequency %llu Hz, using default %lu Hz\n",
                TimerFrequency, ARM64_TIMER_DEFAULT_FREQ);
        TimerFrequency = ARM64_TIMER_DEFAULT_FREQ;
    }

    DPRINT("ARM64 Timer frequency: %llu Hz (%llu.%03llu MHz)\n",
           TimerFrequency, TimerFrequency / 1000000, (TimerFrequency % 1000000) / 1000);

    /* Configure CNTKCTL_EL1 to allow EL0 access to timers */
    __asm__ __volatile__ (
        "mrs %0, CNTKCTL_EL1\n"
        : "=r"(CntkCtl)
    );

    /* Enable EL0 access to physical and virtual counters */
    CntkCtl |= CNTKCTL_EL1_EL0PCTEN | CNTKCTL_EL1_EL0VCTEN;

    /* Optionally enable event stream for WFE-based delays */
    CntkCtl |= CNTKCTL_EL1_EVNTEN;
    CntkCtl &= ~CNTKCTL_EL1_EVNTI_MASK;
    CntkCtl |= (7UL << 4);  /* Event every 2^7 = 128 ticks */

    __asm__ __volatile__ (
        "msr CNTKCTL_EL1, %0\n"
        "isb\n"
        :: "r"(CntkCtl)
    );

    /* Store timer configuration */
    HalTimerConfiguration.Frequency = TimerFrequency;
    HalTimerConfiguration.PhysicalTimerIRQ = 30;        /* Standard ARM64 physical timer IRQ */
    HalTimerConfiguration.VirtualTimerIRQ = 27;         /* Standard ARM64 virtual timer IRQ */
    HalTimerConfiguration.HypervisorTimerIRQ = 26;      /* Standard ARM64 hypervisor timer IRQ */
    HalTimerConfiguration.SecureTimerPresent = TRUE;    /* Assume secure timer is available */

    /* Set up performance frequency for QueryPerformanceCounter */
    PerformanceFrequency.QuadPart = (LONGLONG)TimerFrequency;

    /* Get current counter value as boot time reference */
    CurrentCount = HalGetTimerCount();
    BootTimeReference = CurrentCount;

    /* Disable any existing timer interrupts initially */
    HalSetPhysicalTimer(0, FALSE, TRUE);
    HalSetVirtualTimer(0, FALSE, TRUE);

    DPRINT("ARM64 Generic Timer initialized (Boot reference: %llu)\n", BootTimeReference);

    TimerInitialized = TRUE;
    return TRUE;
}

/*
 * @brief Get current system timer count
 */
ULONG64
NTAPI
HalGetTimerCount(VOID)
{
    ULONG64 Count;

    /* TODO: Read current physical counter value
     * CNTPCT_EL0 provides the physical counter value
     */
    __asm__ __volatile__ (
        "mrs %0, CNTPCT_EL0\n"
        : "=r"(Count)
    );

    return Count;
}

/*
 * @brief Get current virtual timer count
 */
ULONG64
NTAPI
HalGetVirtualTimerCount(VOID)
{
    ULONG64 Count;

    /* TODO: Read current virtual counter value
     * CNTVCT_EL0 provides the virtual counter value
     */
    __asm__ __volatile__ (
        "mrs %0, CNTVCT_EL0\n"
        : "=r"(Count)
    );

    return Count;
}

/*
 * @brief Set physical timer value and control
 */
VOID
NTAPI
HalSetPhysicalTimer(
    IN ULONG64 CompareValue,
    IN BOOLEAN Enable,
    IN BOOLEAN InterruptMask)
{
    ULONG64 Control = 0;

    /* TODO: Set physical timer compare value */
    __asm__ __volatile__ (
        "msr CNTP_CVAL_EL0, %0\n"
        "isb\n"
        :
        : "r"(CompareValue)
    );

    /* Set timer control bits */
    if (Enable)
        Control |= CNTX_CTL_ENABLE;
    if (InterruptMask)
        Control |= CNTX_CTL_IMASK;

    /* TODO: Set physical timer control register */
    __asm__ __volatile__ (
        "msr CNTP_CTL_EL0, %0\n"
        "isb\n"
        :
        : "r"(Control)
    );

    DPRINT("Physical timer set: CompareValue=%llu, Enable=%d, Mask=%d\n",
           CompareValue, Enable, InterruptMask);
}

/*
 * @brief Set virtual timer value and control
 */
VOID
NTAPI
HalSetVirtualTimer(
    IN ULONG64 CompareValue,
    IN BOOLEAN Enable,
    IN BOOLEAN InterruptMask)
{
    ULONG64 Control = 0;

    /* TODO: Set virtual timer compare value */
    __asm__ __volatile__ (
        "msr CNTV_CVAL_EL0, %0\n"
        "isb\n"
        :
        : "r"(CompareValue)
    );

    /* Set timer control bits */
    if (Enable)
        Control |= CNTX_CTL_ENABLE;
    if (InterruptMask)
        Control |= CNTX_CTL_IMASK;

    /* TODO: Set virtual timer control register */
    __asm__ __volatile__ (
        "msr CNTV_CTL_EL0, %0\n"
        "isb\n"
        :
        : "r"(Control)
    );

    DPRINT("Virtual timer set: CompareValue=%llu, Enable=%d, Mask=%d\n",
           CompareValue, Enable, InterruptMask);
}

/*
 * @brief Get timer frequency
 */
ULONG64
NTAPI
HalGetTimerFrequency(VOID)
{
    return TimerFrequency;
}

/*
 * @brief Calculate microseconds from timer ticks
 */
ULONG64
NTAPI
HalTicksToMicroseconds(
    IN ULONG64 Ticks)
{
    /* Convert ticks to microseconds using timer frequency */
    return (Ticks * 1000000ULL) / TimerFrequency;
}

/*
 * @brief Calculate timer ticks from microseconds
 */
ULONG64
NTAPI
HalMicrosecondsToTicks(
    IN ULONG64 Microseconds)
{
    /* Convert microseconds to ticks using timer frequency */
    return (Microseconds * TimerFrequency) / 1000000ULL;
}

/*
 * @brief Delay execution for specified microseconds
 */
VOID
NTAPI
HalDelayMicroseconds(
    IN ULONG Microseconds)
{
    ULONG64 StartTime, EndTime, CurrentTime;
    ULONG64 DelayTicks;

    if (!TimerInitialized)
    {
        DPRINT1("Timer not initialized, cannot delay\n");
        return;
    }

    /* Calculate delay in timer ticks */
    DelayTicks = HalMicrosecondsToTicks(Microseconds);

    /* Get current time */
    StartTime = HalGetTimerCount();
    EndTime = StartTime + DelayTicks;

    /* Busy wait until the delay period has elapsed */
    do
    {
        CurrentTime = HalGetTimerCount();
        /* Handle counter wrap-around (though unlikely with 64-bit counter) */
    } while ((CurrentTime >= StartTime) ? (CurrentTime < EndTime) :
             (CurrentTime < EndTime && EndTime < StartTime));
}

/*
 * @brief System timer interrupt handler
 */
BOOLEAN
NTAPI
HalTimerInterruptHandler(
    IN PKTRAP_FRAME TrapFrame)
{
    ULONG64 CurrentTime, NextTick;
    ULONG64 TickInterval;
    static ULONG TickCount = 0;

    UNREFERENCED_PARAMETER(TrapFrame);

    if (!TimerInitialized)
    {
        return FALSE;
    }

    /* Calculate tick interval (typically 100Hz = 10ms) */
    TickInterval = TimerFrequency / 100;  /* 100 Hz system tick */

    /* Get current time and calculate next interrupt time */
    CurrentTime = HalGetTimerCount();
    NextTick = CurrentTime + TickInterval;

    /* Set next timer interrupt */
    HalSetPhysicalTimer(NextTick, TRUE, FALSE);

    /* Increment tick counter for debugging */
    TickCount++;

    /* Update system time - call kernel clock interrupt handler */
    if (TickCount % 1000 == 0)  /* Every 10 seconds */
    {
        DPRINT("ARM64 Timer tick %lu (time=%llu)\n", TickCount, CurrentTime - BootTimeReference);
    }

    /* TODO: Call KeUpdateSystemTime() when kernel integration is complete
     * This would update the system time and handle time-based scheduling */

    return TRUE;
}

/*
 * @brief Initialize system clock timer
 */
VOID
NTAPI
HalInitializeSystemClock(
    IN ULONG ClockRate)
{
    ULONG64 CurrentTime, NextTick;
    ULONG64 TickInterval;

    if (!TimerInitialized)
    {
        DPRINT1("Timer not initialized\n");
        return;
    }

    /* Validate clock rate - typical values are 100Hz, 250Hz, 1000Hz */
    if (ClockRate == 0 || ClockRate > 10000)
    {
        DPRINT1("Invalid clock rate %lu Hz, using default 100 Hz\n", ClockRate);
        ClockRate = 100;
    }

    /* Calculate tick interval in timer counts */
    TickInterval = TimerFrequency / ClockRate;

    if (TickInterval == 0)
    {
        DPRINT1("Timer interval calculation error\n");
        return;
    }

    DPRINT("Setting up ARM64 system clock: Rate=%lu Hz, Interval=%llu ticks (%llu.%03llu ms)\n",
           ClockRate, TickInterval,
           (TickInterval * 1000) / TimerFrequency,
           ((TickInterval * 1000000) / TimerFrequency) % 1000);

    /* Get current time and set first timer interrupt */
    CurrentTime = HalGetTimerCount();
    NextTick = CurrentTime + TickInterval;

    /* Set up physical timer for system tick generation */
    HalSetPhysicalTimer(NextTick, TRUE, FALSE);

    /* Enable timer interrupt in GIC (IRQ 30 for ARM64 physical timer) */
    HalEnableInterrupt(HalTimerConfiguration.PhysicalTimerIRQ);

    DPRINT("ARM64 system clock initialized - next interrupt at %llu\n", NextTick);
}

/*
 * @brief Query performance counter
 * This is used by QueryPerformanceCounter() API
 */
BOOLEAN
NTAPI
HalQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceCount,
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    if (!TimerInitialized)
    {
        return FALSE;
    }

    /* Return current timer count as performance counter */
    PerformanceCount->QuadPart = (LONGLONG)HalGetTimerCount();

    /* Return timer frequency if requested */
    if (PerformanceFrequency)
    {
        PerformanceFrequency->QuadPart = (LONGLONG)TimerFrequency;
    }

    return TRUE;
}

/*
 * @brief Calibrate timer delay loops
 */
VOID
NTAPI
HalCalibrateTimerDelay(VOID)
{
    /* TODO: Calibrate delay loops for KeStallExecutionProcessor
     * This involves measuring actual delay times and adjusting
     * calibration constants for precise timing
     */

    DPRINT("Calibrating ARM64 timer delays\n");

    /* For ARM64 Generic Timer, delay is quite accurate since
     * it's based on a hardware counter, so minimal calibration needed
     */
}

/*
 * @brief Stall processor execution
 * This implements KeStallExecutionProcessor for ARM64
 */
VOID
NTAPI
HalStallExecution(
    IN ULONG Microseconds)
{
    /* Use timer-based delay */
    HalDelayMicroseconds(Microseconds);
}