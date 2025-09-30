/*
 * PROJECT:     ReactOS Kernel&Driver SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware Resources Arbiter Library
 * COPYRIGHT:   Copyright 2020 Vadim Galyant <vgal@rambler.ru>
 */

/* INCLUDES *******************************************************************/

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include "arbiter.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* DATA **********************************************************************/

/* FUNCTIONS ******************************************************************/

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbTestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRetestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddReserved(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPreprocessEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAllocateEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbGetNextAllocationRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return FALSE;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbFindSuitableRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return FALSE;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbAddAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbBacktrackAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbStartArbiter(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    PAGED_CODE();
    PARBITER_ORDERING Orderings;
    USHORT Index;

    if (!OrderList)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (MaximumAddress < MinimumAddress)
    {
        UINT64 Temp = MaximumAddress;
        MaximumAddress = MinimumAddress;
        MinimumAddress = Temp;
    }

    if (OrderList->Count == OrderList->Maximum)
    {
        USHORT NewMaximum;
        SIZE_T Size;
        PVOID Buffer;

        NewMaximum = (OrderList->Maximum == 0) ? 4 : OrderList->Maximum * 2;
        Size = sizeof(ARBITER_ORDERING) * NewMaximum;
        Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_ARBITER);
        if (!Buffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (OrderList->Orderings)
        {
            RtlCopyMemory(Buffer,
                          OrderList->Orderings,
                          sizeof(ARBITER_ORDERING) * OrderList->Count);
            ExFreePoolWithTag(OrderList->Orderings, TAG_ARBITER);
        }

        OrderList->Orderings = Buffer;
        OrderList->Maximum = NewMaximum;
    }

    Orderings = OrderList->Orderings;
    Index = OrderList->Count;

    while (Index > 0 && Orderings[Index - 1].Start > MinimumAddress)
    {
        Orderings[Index] = Orderings[Index - 1];
        Index--;
    }

    Orderings[Index].Start = MinimumAddress;
    Orderings[Index].End = MaximumAddress;
    OrderList->Count++;

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    PAGED_CODE();
    USHORT i = 0;

    if (!OrderingList || MaximumAddress < MinimumAddress)
    {
        return STATUS_INVALID_PARAMETER;
    }

    while (i < OrderingList->Count)
    {
        PARBITER_ORDERING Entry = &OrderingList->Orderings[i];

        if (Entry->End < MinimumAddress || Entry->Start > MaximumAddress)
        {
            if (i + 1 < OrderingList->Count)
            {
                RtlMoveMemory(&OrderingList->Orderings[i],
                              &OrderingList->Orderings[i + 1],
                              sizeof(ARBITER_ORDERING) * (OrderingList->Count - (i + 1)));
            }
            OrderingList->Count--;
            continue;
        }

        if (Entry->Start < MinimumAddress)
        {
            Entry->Start = MinimumAddress;
        }
        if (Entry->End > MaximumAddress)
        {
            Entry->End = MaximumAddress;
        }

        if (Entry->End < Entry->Start)
        {
            if (i + 1 < OrderingList->Count)
            {
                RtlMoveMemory(&OrderingList->Orderings[i],
                              &OrderingList->Orderings[i + 1],
                              sizeof(ARBITER_ORDERING) * (OrderingList->Count - (i + 1)));
            }
            OrderingList->Count--;
            continue;
        }

        i++;
    }

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    if (!OrderList)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(OrderList, sizeof(*OrderList));
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    if (!OrderList)
    {
        return;
    }

    if (OrderList->Orderings)
    {
        ExFreePoolWithTag(OrderList->Orderings, TAG_ARBITER);
        OrderList->Orderings = NULL;
    }

    OrderList->Count = 0;
    OrderList->Maximum = 0;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE ArbInstance,
    _In_ PCWSTR OrderName,
    _In_ PCWSTR ReservedOrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;
    UINT64 MaxAddress;

    PAGED_CODE();

    if (!ArbInstance)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ArbFreeOrderingList(&ArbInstance->OrderingList);
    ArbFreeOrderingList(&ArbInstance->ReservedList);

    Status = ArbInitializeOrderingList(&ArbInstance->OrderingList);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ArbInitializeOrderingList(&ArbInstance->ReservedList);
    if (!NT_SUCCESS(Status))
    {
        ArbFreeOrderingList(&ArbInstance->OrderingList);
        return Status;
    }

    if (ArbInstance->ResourceType == CmResourceTypePort)
    {
        MaxAddress = 0xFFFFFFFFULL;
    }
    else
    {
        MaxAddress = 0xFFFFFFFFFFFFFFFFULL;
    }

    Status = ArbAddOrdering(&ArbInstance->OrderingList, 0, MaxAddress);
    if (!NT_SUCCESS(Status))
    {
        ArbFreeOrderingList(&ArbInstance->OrderingList);
        ArbFreeOrderingList(&ArbInstance->ReservedList);
        return Status;
    }

    UNREFERENCED_PARAMETER(OrderName);
    UNREFERENCED_PARAMETER(ReservedOrderName);
    UNREFERENCED_PARAMETER(TranslateOrderingFunction);
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("ArbInitializeArbiterInstance: '%S'\n", ArbiterName);

    ASSERT(Arbiter->UnpackRequirement != NULL);
    ASSERT(Arbiter->PackResource != NULL);
    ASSERT(Arbiter->UnpackResource != NULL);
    ASSERT(Arbiter->MutexEvent == NULL);
    ASSERT(Arbiter->Allocation == NULL);
    ASSERT(Arbiter->PossibleAllocation == NULL);
    ASSERT(Arbiter->AllocationStack == NULL);

    Arbiter->Signature = ARBITER_SIGNATURE;
    Arbiter->BusDeviceObject = BusDeviceObject;

    Arbiter->MutexEvent = ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT), TAG_ARBITER);
    if (!Arbiter->MutexEvent)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(Arbiter->MutexEvent, SynchronizationEvent, TRUE);

    Arbiter->AllocationStack = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_ARB_ALLOCATION);
    if (!Arbiter->AllocationStack)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->AllocationStackMaxSize = PAGE_SIZE;

    Arbiter->Allocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->Allocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->PossibleAllocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->PossibleAllocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->Allocation, TAG_ARB_RANGE);
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitializeRangeList(Arbiter->Allocation);
    RtlInitializeRangeList(Arbiter->PossibleAllocation);

    Arbiter->Name = ArbiterName;
    Arbiter->ResourceType = ResourceType;
    Arbiter->TransactionInProgress = FALSE;

    if (!Arbiter->TestAllocation)
        Arbiter->TestAllocation = ArbTestAllocation;

    if (!Arbiter->RetestAllocation)
        Arbiter->RetestAllocation = ArbRetestAllocation;

    if (!Arbiter->CommitAllocation)
        Arbiter->CommitAllocation = ArbCommitAllocation;

    if (!Arbiter->RollbackAllocation)
        Arbiter->RollbackAllocation = ArbRollbackAllocation;

    if (!Arbiter->AddReserved)
        Arbiter->AddReserved = ArbAddReserved;

    if (!Arbiter->PreprocessEntry)
        Arbiter->PreprocessEntry = ArbPreprocessEntry;

    if (!Arbiter->AllocateEntry)
        Arbiter->AllocateEntry = ArbAllocateEntry;

    if (!Arbiter->GetNextAllocationRange)
        Arbiter->GetNextAllocationRange = ArbGetNextAllocationRange;

    if (!Arbiter->FindSuitableRange)
        Arbiter->FindSuitableRange = ArbFindSuitableRange;

    if (!Arbiter->AddAllocation)
        Arbiter->AddAllocation = ArbAddAllocation;

    if (!Arbiter->BacktrackAllocation)
        Arbiter->BacktrackAllocation = ArbBacktrackAllocation;

    if (!Arbiter->OverrideConflict)
        Arbiter->OverrideConflict = ArbOverrideConflict;

    if (!Arbiter->BootAllocation)
        Arbiter->BootAllocation = ArbBootAllocation;

    if (!Arbiter->QueryConflict)
        Arbiter->QueryConflict = ArbQueryConflict;

    if (!Arbiter->StartArbiter)
        Arbiter->StartArbiter = ArbStartArbiter;

    Status = ArbBuildAssignmentOrdering(Arbiter, OrderName, OrderName, TranslateOrderingFunction);
    if (NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    DPRINT1("ArbInitializeArbiterInstance: Status %X\n", Status);

    return Status;
}
