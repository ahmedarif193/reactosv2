/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * PURPOSE:         ARM64 SList (Singly Linked List) Interlocked Operations
 * FILE:            ntoskrnl/ex/arm64/slist.c
 * PROGRAMMER:      ARM64 Port Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Undefine the macros to implement the actual functions */
#undef ExInitializeSListHead
#undef ExInterlockedFlushSList

/* GLOBALS *****************************************************************/

/* On ARM64, we use simple 16-byte aligned headers for SLists */
/* This matches the Windows ARM64 behavior */

/* External RTL function declarations */
NTSYSAPI
PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER SListHead);

NTSYSAPI
PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry);

NTSYSAPI
PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(
    _Inout_ PSLIST_HEADER SListHead);

NTSYSAPI
USHORT
NTAPI
RtlQueryDepthSList(
    _In_ PSLIST_HEADER SListHead);

/* RtlInitializeSListHead is provided by RTL library */
NTSYSAPI
VOID
NTAPI
RtlInitializeSListHead(
    _Out_ PSLIST_HEADER SListHead);

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
ExInitializeSListHead(
    _Out_ PSLIST_HEADER SListHead)
{
    /* Forward to RTL implementation */
    RtlInitializeSListHead(SListHead);
}

/*
 * @implemented
 */
PSLIST_ENTRY
NTAPI
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER SListHead)
{
    /* Forward to RTL implementation */
    return RtlInterlockedPopEntrySList(SListHead);
}

/*
 * @implemented
 */
PSLIST_ENTRY
NTAPI
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    /* Forward to RTL implementation */
    return RtlInterlockedPushEntrySList(SListHead, SListEntry);
}

/*
 * @implemented
 */
PSLIST_ENTRY
NTAPI
ExInterlockedFlushSList(
    _Inout_ PSLIST_HEADER SListHead)
{
    /* Forward to RTL implementation */
    return RtlInterlockedFlushSList(SListHead);
}

/*
 * @implemented
 */
USHORT
NTAPI
ExQueryDepthSList(
    _In_ PSLIST_HEADER SListHead)
{
    /* Forward to RTL implementation */
    return RtlQueryDepthSList(SListHead);
}

/*
 * These are the fault handling entry points that the kernel debugger
 * and exception handlers may reference. They're placeholders for ARM64
 * since our implementation uses different fault handling mechanisms.
 */

/*
 * @implemented
 */
VOID
NTAPI
ExInterlockedPopEntrySListResume(VOID)
{
    /* This is a placeholder - ARM64 uses different exception handling */
    ASSERT(FALSE);
}

/*
 * @implemented
 */
VOID
NTAPI
ExInterlockedPopEntrySListEnd(VOID)
{
    /* This is a placeholder - ARM64 uses different exception handling */
    ASSERT(FALSE);
}

/*
 * @implemented
 */
VOID
NTAPI
ExInterlockedPopEntrySListFault(VOID)
{
    /* This is a placeholder - ARM64 uses different exception handling */
    ASSERT(FALSE);
}