/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/ke/amd64/irqobj.c
 * PURPOSE:         AMD64 IRQ Object Management
 *
 * PROGRAMMERS:     ReactOS AMD64 Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DEFINES *******************************************************************/

#define MAX_IRQ_VECTORS 256
#define FIRST_DEVICE_VECTOR 0x30
#define LAST_DEVICE_VECTOR  0xEF

/* STRUCTURES ****************************************************************/

typedef struct _KIRQ_OBJECT
{
    LIST_ENTRY ListEntry;
    UCHAR Vector;
    KIRQL Irql;
    KINTERRUPT_MODE Mode;
    BOOLEAN ShareVector;
    BOOLEAN Connected;
    KAFFINITY ProcessorMask;
    PKSERVICE_ROUTINE ServiceRoutine;
    PVOID ServiceContext;
    KSPIN_LOCK SpinLock;
    ULONG TickCount;
    PKINTERRUPT_ROUTINE FloatingServiceRoutine;
    PVOID FloatingContext;
} KIRQ_OBJECT, *PKIRQ_OBJECT;

typedef struct _KIRQ_VECTOR_DATA
{
    KSPIN_LOCK Lock;
    LIST_ENTRY IrqListHead;
    ULONG ShareCount;
    KIRQL Irql;
} KIRQ_VECTOR_DATA, *PKIRQ_VECTOR_DATA;

/* GLOBALS *******************************************************************/

static KIRQ_VECTOR_DATA IrqVectorTable[MAX_IRQ_VECTORS];
static BOOLEAN IrqTableInitialized = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * @brief Initializes the IRQ vector table
 * @return None
 */
static VOID
KiInitializeIrqVectorTable(VOID)
{
    ULONG i;

    if (IrqTableInitialized)
        return;

    for (i = 0; i < MAX_IRQ_VECTORS; i++)
    {
        KeInitializeSpinLock(&IrqVectorTable[i].Lock);
        InitializeListHead(&IrqVectorTable[i].IrqListHead);
        IrqVectorTable[i].ShareCount = 0;
        IrqVectorTable[i].Irql = PASSIVE_LEVEL;
    }

    IrqTableInitialized = TRUE;
    DPRINT1("IRQ vector table initialized\n");
}

/*
 * @brief Allocates a free interrupt vector
 * @param Irql - Desired IRQL for the vector
 * @return Vector number or 0 if none available
 */
static UCHAR
KiAllocateInterruptVector(
    _In_ KIRQL Irql)
{
    ULONG Vector;
    KIRQL OldIrql;

    /* Search for a free vector in the device vector range */
    for (Vector = FIRST_DEVICE_VECTOR; Vector <= LAST_DEVICE_VECTOR; Vector++)
    {
        KeAcquireSpinLock(&IrqVectorTable[Vector].Lock, &OldIrql);

        if (IrqVectorTable[Vector].ShareCount == 0)
        {
            IrqVectorTable[Vector].Irql = Irql;
            IrqVectorTable[Vector].ShareCount = 1;
            KeReleaseSpinLock(&IrqVectorTable[Vector].Lock, OldIrql);

            DPRINT("Allocated vector %u for IRQL %u\n", Vector, Irql);
            return (UCHAR)Vector;
        }

        KeReleaseSpinLock(&IrqVectorTable[Vector].Lock, OldIrql);
    }

    DPRINT1("No free interrupt vectors available\n");
    return 0;
}

/*
 * @brief Generic interrupt dispatcher
 * @param Vector - Interrupt vector
 * @param TrapFrame - Trap frame
 * @return TRUE if handled, FALSE otherwise
 */
static BOOLEAN
__attribute__((unused))
KiGenericInterruptHandler(
    _In_ UCHAR Vector,
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKIRQ_VECTOR_DATA VectorData;
    PLIST_ENTRY ListEntry;
    PKIRQ_OBJECT IrqObject;
    BOOLEAN Handled = FALSE;
    KIRQL OldIrql;

    /* Get vector data */
    VectorData = &IrqVectorTable[Vector];

    /* Raise IRQL to vector's level */
    OldIrql = KfRaiseIrql(VectorData->Irql);

    /* Acquire vector lock */
    KeAcquireSpinLockAtDpcLevel(&VectorData->Lock);

    /* Walk the IRQ list for this vector */
    ListEntry = VectorData->IrqListHead.Flink;
    while (ListEntry != &VectorData->IrqListHead)
    {
        IrqObject = CONTAINING_RECORD(ListEntry, KIRQ_OBJECT, ListEntry);

        /* Check if this IRQ is connected */
        if (IrqObject->Connected && IrqObject->ServiceRoutine)
        {
            /* Release lock before calling service routine */
            KeReleaseSpinLockFromDpcLevel(&VectorData->Lock);

            /* Call the service routine */
            if (IrqObject->ServiceRoutine((PKINTERRUPT)IrqObject, IrqObject->ServiceContext))
            {
                Handled = TRUE;
            }

            /* Reacquire lock */
            KeAcquireSpinLockAtDpcLevel(&VectorData->Lock);
        }

        ListEntry = ListEntry->Flink;
    }

    /* Release vector lock */
    KeReleaseSpinLockFromDpcLevel(&VectorData->Lock);

    /* Lower IRQL */
    KeLowerIrql(OldIrql);

    return Handled;
}

/* EXPORTED FUNCTIONS ********************************************************/

/*
 * @implemented
 * @brief Initializes an IRQ object
 * @param IrqObject - IRQ object to initialize
 * @param Vector - Interrupt vector (0 for auto-assign)
 * @param Irql - IRQL for the interrupt
 * @param Mode - Interrupt mode (LevelSensitive or Latched)
 * @param ShareVector - TRUE if vector can be shared
 * @param ProcessorMask - Processors that can handle this interrupt
 * @return STATUS_SUCCESS or error code
 */
NTSTATUS
NTAPI
KeInitializeIrqObject(
    _Out_ PKIRQ_OBJECT IrqObject,
    _In_ UCHAR Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode,
    _In_ BOOLEAN ShareVector,
    _In_ KAFFINITY ProcessorMask)
{
    /* Initialize IRQ table if needed */
    if (!IrqTableInitialized)
    {
        KiInitializeIrqVectorTable();
    }

    /* Validate parameters */
    if (!IrqObject)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Irql < DISPATCH_LEVEL || Irql > HIGH_LEVEL)
    {
        DPRINT1("Invalid IRQL %u for interrupt\n", Irql);
        return STATUS_INVALID_PARAMETER;
    }

    /* Initialize the IRQ object */
    RtlZeroMemory(IrqObject, sizeof(KIRQ_OBJECT));
    InitializeListHead(&IrqObject->ListEntry);
    KeInitializeSpinLock(&IrqObject->SpinLock);

    /* Set basic properties */
    IrqObject->Irql = Irql;
    IrqObject->Mode = Mode;
    IrqObject->ShareVector = ShareVector;
    IrqObject->ProcessorMask = ProcessorMask ? ProcessorMask : KeActiveProcessors;
    IrqObject->Connected = FALSE;

    /* Assign or allocate vector */
    if (Vector == 0)
    {
        /* Auto-allocate a vector */
        Vector = KiAllocateInterruptVector(Irql);
        if (Vector == 0)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else if (Vector < FIRST_DEVICE_VECTOR || Vector > LAST_DEVICE_VECTOR)
    {
        DPRINT1("Invalid vector %u\n", Vector);
        return STATUS_INVALID_PARAMETER;
    }

    IrqObject->Vector = Vector;

    DPRINT("IRQ object initialized: Vector=%u, IRQL=%u, Mode=%s, Share=%s\n",
           Vector, Irql,
           Mode == LevelSensitive ? "Level" : "Edge",
           ShareVector ? "Yes" : "No");

    return STATUS_SUCCESS;
}

/*
 * @implemented
 * @brief Connects an interrupt service routine to an IRQ
 * @param IrqObject - IRQ object
 * @param ServiceRoutine - Service routine to connect
 * @param ServiceContext - Context for service routine
 * @return TRUE if successful, FALSE otherwise
 */
BOOLEAN
NTAPI
KeConnectIrq(
    _Inout_ PKIRQ_OBJECT IrqObject,
    _In_ PKSERVICE_ROUTINE ServiceRoutine,
    _In_opt_ PVOID ServiceContext)
{
    PKIRQ_VECTOR_DATA VectorData;
    KIRQL OldIrql;

    /* Validate parameters */
    if (!IrqObject || !ServiceRoutine)
    {
        return FALSE;
    }

    if (IrqObject->Connected)
    {
        DPRINT1("IRQ already connected\n");
        return FALSE;
    }

    /* Get vector data */
    VectorData = &IrqVectorTable[IrqObject->Vector];

    /* Acquire vector lock */
    KeAcquireSpinLock(&VectorData->Lock, &OldIrql);

    /* Check if vector can be shared */
    if (VectorData->ShareCount > 0 && !IrqObject->ShareVector)
    {
        KeReleaseSpinLock(&VectorData->Lock, OldIrql);
        DPRINT1("Vector %u cannot be shared\n", IrqObject->Vector);
        return FALSE;
    }

    /* Set service routine */
    IrqObject->ServiceRoutine = ServiceRoutine;
    IrqObject->ServiceContext = ServiceContext;

    /* Add to vector's IRQ list */
    InsertTailList(&VectorData->IrqListHead, &IrqObject->ListEntry);
    VectorData->ShareCount++;

    /* Mark as connected */
    IrqObject->Connected = TRUE;

    /* Release lock */
    KeReleaseSpinLock(&VectorData->Lock, OldIrql);

    /* Enable interrupt in APIC if needed */
    if (VectorData->ShareCount == 1)
    {
        /* First connection to this vector, enable it */
        HalEnableSystemInterrupt(IrqObject->Vector, IrqObject->Irql, IrqObject->Mode);
    }

    DPRINT("Connected IRQ: Vector=%u, ServiceRoutine=%p\n",
           IrqObject->Vector, ServiceRoutine);

    return TRUE;
}

/*
 * @implemented
 * @brief Disconnects an interrupt service routine
 * @param IrqObject - IRQ object to disconnect
 * @return TRUE if successful, FALSE otherwise
 */
BOOLEAN
NTAPI
KeDisconnectIrq(
    _Inout_ PKIRQ_OBJECT IrqObject)
{
    PKIRQ_VECTOR_DATA VectorData;
    KIRQL OldIrql;

    /* Validate parameters */
    if (!IrqObject || !IrqObject->Connected)
    {
        return FALSE;
    }

    /* Get vector data */
    VectorData = &IrqVectorTable[IrqObject->Vector];

    /* Acquire vector lock */
    KeAcquireSpinLock(&VectorData->Lock, &OldIrql);

    /* Remove from vector's IRQ list */
    RemoveEntryList(&IrqObject->ListEntry);
    VectorData->ShareCount--;

    /* Mark as disconnected */
    IrqObject->Connected = FALSE;
    IrqObject->ServiceRoutine = NULL;
    IrqObject->ServiceContext = NULL;

    /* Disable interrupt if this was the last connection */
    if (VectorData->ShareCount == 0)
    {
        HalDisableSystemInterrupt(IrqObject->Vector, IrqObject->Irql);
    }

    /* Release lock */
    KeReleaseSpinLock(&VectorData->Lock, OldIrql);

    DPRINT("Disconnected IRQ: Vector=%u\n", IrqObject->Vector);

    return TRUE;
}

/* KeSynchronizeExecution is implemented in interrupt.c */

/*
 * @implemented
 * @brief Registers an interrupt handler with the IDT
 * @param Vector - Interrupt vector
 * @param Handler - Handler function
 * @return STATUS_SUCCESS or error code
 */
VOID
NTAPI
KeRegisterInterruptHandler(
    _In_ ULONG Vector,
    _In_ PVOID Handler)
{
    KIDTENTRY64 IdtEntry;
    PKIDTENTRY64 Idt;

    /* Get IDT base */
    Idt = (PKIDTENTRY64)KeGetPcr()->IdtBase;

    /* Build IDT entry */
    IdtEntry.OffsetLow = (USHORT)((ULONG_PTR)Handler & 0xFFFF);
    IdtEntry.Selector = KGDT64_R0_CODE;
    IdtEntry.IstIndex = 0;
    IdtEntry.Type = 0xE;  /* Interrupt gate (5-bit type field) */
    IdtEntry.Dpl = 0;
    IdtEntry.Present = 1;
    IdtEntry.OffsetMiddle = (USHORT)(((ULONG_PTR)Handler >> 16) & 0xFFFF);
    IdtEntry.OffsetHigh = (ULONG)((ULONG_PTR)Handler >> 32);
    IdtEntry.Reserved0 = 0;

    /* Write IDT entry */
    Idt[Vector] = IdtEntry;
}

/*
 * @implemented
 * @brief Sets processor affinity for an interrupt
 * @param IrqObject - IRQ object
 * @param ProcessorMask - New processor mask
 * @return Previous processor mask
 */
KAFFINITY
NTAPI
KeSetInterruptAffinity(
    _Inout_ PKIRQ_OBJECT IrqObject,
    _In_ KAFFINITY ProcessorMask)
{
    KAFFINITY OldMask;

    /* Validate parameters */
    if (!IrqObject)
    {
        return 0;
    }

    /* Save old mask */
    OldMask = IrqObject->ProcessorMask;

    /* Set new mask */
    IrqObject->ProcessorMask = ProcessorMask & KeActiveProcessors;

    /* Update APIC routing if connected */
    if (IrqObject->Connected)
    {
        /* TODO: Update APIC routing tables */
    }

    return OldMask;
}

/* END OF FILE */