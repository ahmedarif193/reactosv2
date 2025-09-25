/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         ARM64 Thread Quantum Management and Timer Integration
 * COPYRIGHT:       Copyright 2024 Ahmed Arif (arif.ing@outlook.com)
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* Timer interrupt quantum tracking */
ULONG KiQuantumTicks = 0;
LARGE_INTEGER KiQuantumStartTime;

/* ARM64 Generic Timer constants */
#define ARM64_TIMER_TICKS_PER_QUANTUM 3  /* Number of timer ticks per quantum */
/* ARM64_QUANTUM_TARGET_MS now defined in ke.h */

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Calculate quantum ticks based on ARM64 timer frequency
 *
 * ARM64 Generic Timer provides a consistent time source across processors.
 * This function calculates how many timer ticks correspond to one quantum.
 *
 * @return ULONG - Number of timer ticks per quantum
 */
FORCEINLINE
ULONG
KiCalculateQuantumTicks(VOID)
{
    ULONG64 TimerFreq;
    ULONG QuantumTicks;

    /* Read ARM64 Generic Timer frequency */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(TimerFreq));

    if (TimerFreq == 0)
    {
        /* Fallback to typical ARM64 timer frequency */
        TimerFreq = ARM64_TIMER_FREQ_DEFAULT; /* 62.5 MHz */
    }

    /* Calculate ticks for target quantum duration */
    QuantumTicks = (ULONG)((TimerFreq * ARM64_QUANTUM_TARGET_MS) / 1000);

    /* Ensure minimum quantum */
    if (QuantumTicks < ARM64_TIMER_TICKS_PER_QUANTUM)
        QuantumTicks = ARM64_TIMER_TICKS_PER_QUANTUM;

    DPRINT("KiCalculateQuantumTicks: Timer freq %llu Hz, quantum ticks %lu\n",
           TimerFreq, QuantumTicks);

    return QuantumTicks;
}

/**
 * @brief Read ARM64 Generic Timer counter
 *
 * @return ULONG64 - Current timer count
 */
FORCEINLINE
ULONG64
KiReadTimerCounter(VOID)
{
    ULONG64 Count;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Count));
    return Count;
}

/**
 * @brief Set ARM64 Generic Timer compare value for next quantum expiration
 *
 * @param CompareValue - Timer value when interrupt should fire
 */
FORCEINLINE
VOID
KiSetTimerCompare(
    IN ULONG64 CompareValue)
{
    /* Set the compare value for the virtual timer */
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(CompareValue));

    /* Enable the virtual timer */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1ULL));

    /* Memory barrier to ensure timer is set */
    ARM64_ISB();
}

/**
 * @brief Disable ARM64 Generic Timer
 */
FORCEINLINE
VOID
KiDisableTimer(VOID)
{
    /* Disable the virtual timer */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0ULL));
    ARM64_ISB();
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Initialize ARM64 quantum management
 *
 * Sets up the ARM64 Generic Timer for quantum expiration interrupts.
 * This is called during scheduler initialization.
 */
VOID
NTAPI
KiInitializeQuantumManagement(VOID)
{
    ULONG64 CurrentTime;

    DPRINT("KiInitializeQuantumManagement: Initializing ARM64 quantum timer\n");

    /* Calculate quantum duration in timer ticks */
    KiQuantumTicks = KiCalculateQuantumTicks();

    /* Get current time */
    CurrentTime = KiReadTimerCounter();
    KiQuantumStartTime.QuadPart = (LONGLONG)CurrentTime;

    /* Set up initial timer for first quantum */
    KiSetTimerCompare(CurrentTime + KiQuantumTicks);

    DPRINT("KiInitializeQuantumManagement: Quantum timer initialized, ticks=%lu\n",
           KiQuantumTicks);
}

/**
 * @brief Handle quantum expiration timer interrupt
 *
 * This is called by the ARM64 Generic Timer interrupt handler when
 * a thread's quantum expires. It triggers the scheduler to potentially
 * switch to another thread.
 *
 * @param TrapFrame - Interrupt trap frame
 * @return VOID
 */
VOID
FASTCALL
KiQuantumTimerHandler(
    IN PKTRAP_FRAME TrapFrame)
{
    PKPRCB Prcb;
    PKTHREAD CurrentThread;
    ULONG64 CurrentTime;
    BOOLEAN Reschedule = FALSE;

    UNREFERENCED_PARAMETER(TrapFrame);

    /* Disable the timer interrupt */
    KiDisableTimer();

    Prcb = KeGetCurrentPrcb();
    CurrentThread = Prcb->CurrentThread;

    /* Update timer statistics */
    Prcb->InterruptTime++;

    if (CurrentThread && CurrentThread != Prcb->IdleThread)
    {
        /* Decrement thread quantum */
        if (CurrentThread->Quantum > 0)
        {
            CurrentThread->Quantum--;
        }

        /* Check if quantum expired */
        if (CurrentThread->Quantum == 0)
        {
            /* Quantum expired, request reschedule */
            Reschedule = KiQuantumEnd(Prcb);
        }

        /* Update thread timing */
        CurrentThread->KernelTime++;
    }

    /* Set up next quantum timer */
    CurrentTime = KiReadTimerCounter();
    KiSetTimerCompare(CurrentTime + KiQuantumTicks);

    /* Request scheduler dispatch if needed */
    if (Reschedule)
    {
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
}

/**
 * @brief Update thread quantum on thread switch
 *
 * Called when switching to a new thread to set up appropriate quantum
 * based on thread priority and scheduling policy.
 *
 * @param Thread - Thread being switched to
 */
VOID
FASTCALL
KiUpdateQuantumOnSwitch(
    IN PKTHREAD Thread)
{
    UCHAR NewQuantum;

    if (!Thread || Thread->State != Running)
        return;

    /* Calculate new quantum based on priority */
    if (Thread->Priority >= LOW_REALTIME_PRIORITY)
    {
        /* Real-time threads get longer quantum */
        NewQuantum = THREAD_QUANTUM + 3;
    }
    else if (Thread->Priority >= (LOW_REALTIME_PRIORITY / 2))
    {
        /* High variable priority threads get standard quantum */
        NewQuantum = THREAD_QUANTUM;
    }
    else
    {
        /* Low priority threads get shorter quantum for better responsiveness */
        NewQuantum = THREAD_QUANTUM - 2;
    }

    /* Apply minimum and maximum limits */
    if (NewQuantum < THREAD_QUANTUM_MIN)
        NewQuantum = THREAD_QUANTUM_MIN;
    else if (NewQuantum > THREAD_QUANTUM_MAX)
        NewQuantum = THREAD_QUANTUM_MAX;

    /* Set the new quantum */
    Thread->Quantum = NewQuantum;

    /* Update quantum start time for statistics */
    KiQuantumStartTime.QuadPart = (LONGLONG)KiReadTimerCounter();
}

/**
 * @brief Handle voluntary quantum yield
 *
 * Called when a thread voluntarily yields its quantum (e.g., through Sleep(0)
 * or when waiting on a synchronization object).
 *
 * @param Thread - Thread yielding quantum
 * @return BOOLEAN - TRUE if reschedule should occur
 */
BOOLEAN
FASTCALL
KiYieldQuantum(
    IN PKTHREAD Thread)
{
    PKPRCB Prcb;
    ULONG64 QuantumUsed;
    ULONG64 CurrentTime;

    if (!Thread)
        return FALSE;

    Prcb = KeGetCurrentPrcb();
    CurrentTime = KiReadTimerCounter();

    /* Calculate how much of the quantum was used */
    QuantumUsed = CurrentTime - KiQuantumStartTime.QuadPart;

    /* If thread used less than half quantum, give bonus on next run */
    if (QuantumUsed < (KiQuantumTicks / 2) && Thread->Quantum < THREAD_QUANTUM_MAX)
    {
        Thread->Quantum++;
    }

    /* Reset quantum start time */
    KiQuantumStartTime.QuadPart = (LONGLONG)CurrentTime;

    /* Check if there are other threads ready */
    return (Prcb->ReadySummary != 0);
}

/**
 * @brief Adjust quantum for I/O bound threads
 *
 * Threads that spend time waiting for I/O get quantum bonuses to improve
 * system responsiveness.
 *
 * @param Thread - Thread returning from I/O wait
 * @param WaitTime - Time spent waiting (in 100ns units)
 */
VOID
FASTCALL
KiAdjustQuantumForIo(
    IN PKTHREAD Thread,
    IN LARGE_INTEGER WaitTime)
{
    ULONG WaitMs;
    UCHAR BonusQuantum;

    if (!Thread || Thread->DisableBoost)
        return;

    /* Convert wait time to milliseconds */
    WaitMs = (ULONG)(WaitTime.QuadPart / 10000);

    /* Calculate bonus quantum based on wait time */
    if (WaitMs > 100)
    {
        /* Long I/O wait - significant bonus */
        BonusQuantum = 4;
    }
    else if (WaitMs > 10)
    {
        /* Medium I/O wait - moderate bonus */
        BonusQuantum = 2;
    }
    else if (WaitMs > 1)
    {
        /* Short I/O wait - small bonus */
        BonusQuantum = 1;
    }
    else
    {
        /* Very short wait - no bonus */
        return;
    }

    /* Apply bonus quantum */
    Thread->Quantum += BonusQuantum;
    if (Thread->Quantum > THREAD_QUANTUM_MAX)
        Thread->Quantum = THREAD_QUANTUM_MAX;

    DPRINT("KiAdjustQuantumForIo: Thread %p, wait %lums, bonus %d, new quantum %d\n",
           Thread, WaitMs, BonusQuantum, Thread->Quantum);
}

/**
 * @brief Get current quantum usage statistics
 *
 * @param Thread - Thread to query (NULL for current thread)
 * @param QuantumUsed - Receives quantum used so far
 * @param QuantumRemaining - Receives quantum remaining
 * @return BOOLEAN - TRUE if statistics are valid
 */
BOOLEAN
FASTCALL
KiGetQuantumStatistics(
    IN PKTHREAD Thread,
    OUT PULONG QuantumUsed,
    OUT PULONG QuantumRemaining)
{
    ULONG64 CurrentTime;
    ULONG64 ElapsedTicks;

    if (!Thread)
        Thread = KeGetCurrentThread();

    if (!Thread || Thread->State != Running)
        return FALSE;

    CurrentTime = KiReadTimerCounter();
    ElapsedTicks = CurrentTime - KiQuantumStartTime.QuadPart;

    /* Calculate quantum usage */
    *QuantumUsed = (ULONG)(ElapsedTicks * 100 / KiQuantumTicks); /* Percentage */
    *QuantumRemaining = Thread->Quantum;

    return TRUE;
}

/**
 * @brief ARM64 timer interrupt service routine
 *
 * This is the main entry point for ARM64 Generic Timer interrupts.
 * It determines the source of the timer interrupt and dispatches accordingly.
 *
 * @param InterruptObject - Interrupt object
 * @param ServiceContext - Service context
 * @return BOOLEAN - TRUE if interrupt was handled
 */
BOOLEAN
NTAPI
KiTimerInterruptServiceRoutine(
    IN PKINTERRUPT InterruptObject,
    IN PVOID ServiceContext)
{
    ULONG64 TimerStatus;

    UNREFERENCED_PARAMETER(InterruptObject);
    UNREFERENCED_PARAMETER(ServiceContext);

    /* Read ARM64 Generic Timer status */
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(TimerStatus));

    /* Check if virtual timer triggered the interrupt */
    if (TimerStatus & 0x4) /* ISTATUS bit */
    {
        /* Handle quantum timer */
        KiQuantumTimerHandler(NULL);
        return TRUE;
    }

    /* Not our interrupt */
    return FALSE;
}

/**
 * @brief Initialize ARM64 timer interrupt
 *
 * Sets up the interrupt handler for the ARM64 Generic Timer.
 * This should be called during system initialization.
 *
 * @return NTSTATUS - Status code
 */
NTSTATUS
NTAPI
KiInitializeTimerInterrupt(VOID)
{
    NTSTATUS Status;

    DPRINT("KiInitializeTimerInterrupt: Setting up ARM64 timer interrupt\n");

    /* Initialize quantum management */
    KiInitializeQuantumManagement();

    /* TODO: Connect timer interrupt handler to GIC */
    /* This will require integration with the ARM64 HAL and GIC driver */
    Status = STATUS_SUCCESS;

    if (NT_SUCCESS(Status))
    {
        DPRINT("KiInitializeTimerInterrupt: ARM64 timer interrupt initialized\n");
    }
    else
    {
        DPRINT1("KiInitializeTimerInterrupt: Failed to initialize timer interrupt: 0x%lx\n", Status);
    }

    return Status;
}