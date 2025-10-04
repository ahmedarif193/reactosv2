/*
 * PROJECT:     Uefinity UEFI Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Library architecture glue for AMD64
 *
 * This module provides the minimal architectural scaffolding required by
 * the ReactOS boot library (boot/environ) so the generic loader paths can
 * execute on x64 without immediately failing with STATUS_NOT_IMPLEMENTED.
 * The implementation mirrors the existing i386 support code but adapts it
 * for long mode where paging is already enabled and cannot be disabled.
 */

#include <bl.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

/*
 * Boot library globals that describe the currently active execution
 * context and helper callbacks for MM relocation routines. The boot
 * library expects these symbols to exist per architecture build.
 */
static BL_ARCH_CONTEXT BlFirmwareContext;
static BL_ARCH_CONTEXT BlApplicationContext;

PBL_ARCH_CONTEXT CurrentExecutionContext;
PBL_MM_RELOCATE_SELF_MAP BlMmRelocateSelfMap;
PBL_MM_MOVE_VIRTUAL_ADDRESS_RANGE BlMmMoveVirtualAddressRange;
PBL_MM_ZERO_VIRTUAL_ADDRESS_RANGE BlMmZeroVirtualAddressRange;

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static inline VOID
BlAmd64EnableInterrupts(VOID)
{
#if defined(_MSC_VER)
    _enable();
#else
    __asm__ __volatile__("sti" ::: "memory");
#endif
}

static inline VOID
BlAmd64DisableInterrupts(VOID)
{
#if defined(_MSC_VER)
    _disable();
#else
    __asm__ __volatile__("cli" ::: "memory");
#endif
}

static inline VOID
BlAmd64SynchronizePagingFlag(
    _Inout_ PBL_ARCH_CONTEXT Context)
{
    /*
     * Long mode execution always runs with paging enabled. We simply mark
     * the state in the context so the boot library will not attempt to
     * toggle hardware paging (which is illegal once in 64-bit mode).
     */
    Context->ContextFlags |= BL_CONTEXT_PAGING_ON;
}

/* ------------------------------------------------------------------------- */
/* Boot Library callbacks                                                    */
/* ------------------------------------------------------------------------- */

VOID
DECLSPEC_NORETURN
ArchTrapNoProcess(VOID)
{
    /*
     * The boot library installs this handler for debugger INT3 and internal
     * ASSERT interrupts. On x64 we simply halt the processor after disabling
     * interrupts which keeps the behaviour consistent with the i386 variant.
     */
    BlAmd64DisableInterrupts();

#if defined(__GNUC__)
    __asm__ __volatile__ ("hlt\n"
                          "jmp .-2\n");
#elif defined(_MSC_VER)
    while (TRUE)
    {
        __halt();
    }
#else
#error Unsupported compiler
#endif
    __assume(0);
}

static
VOID
ArchSwitchContext(
    _In_ PBL_ARCH_CONTEXT NewContext,
    _In_opt_ PBL_ARCH_CONTEXT OldContext)
{
    UNREFERENCED_PARAMETER(OldContext);

    if (NewContext == NULL)
    {
        return;
    }

    /* On x64 we only honour the interrupt flag; paging stays enabled. */
    if (NewContext->ContextFlags & BL_CONTEXT_INTERRUPTS_ON)
    {
        BlAmd64EnableInterrupts();
    }
    else
    {
        BlAmd64DisableInterrupts();
    }

    BlAmd64SynchronizePagingFlag(NewContext);
}

static
NTSTATUS
ArchInitializeContext(
    _In_ PBL_ARCH_CONTEXT Context)
{
    if (Context == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * All UEFI execution on x64 happens in long mode with paging already
     * active. We expose this to the boot library by marking the translation
     * type as virtual and flagging paging/interrupt defaults accordingly.
     */
    Context->TranslationType = BlVirtual;
    Context->ContextFlags = BL_CONTEXT_INTERRUPTS_ON | BL_CONTEXT_PAGING_ON;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ArchInitializeContexts(VOID)
{
    NTSTATUS Status;

    /* Application (loader) context */
    BlApplicationContext.Mode = BlProtectedMode;
    Status = ArchInitializeContext(&BlApplicationContext);
    if (!NT_SUCCESS(Status))
    {
        ERR("ArchInitializeContext(Application) failed: 0x%08lx\n", Status);
        return Status;
    }

    /* Firmware context: treat as protected mode as well (UEFI long mode). */
    BlFirmwareContext.Mode = BlProtectedMode;
    Status = ArchInitializeContext(&BlFirmwareContext);
    if (!NT_SUCCESS(Status))
    {
        ERR("ArchInitializeContext(Firmware) failed: 0x%08lx\n", Status);
        return Status;
    }

    CurrentExecutionContext = &BlApplicationContext;
    ArchSwitchContext(CurrentExecutionContext, NULL);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Exported boot library entry points                                        */
/* ------------------------------------------------------------------------- */

VOID
BlpArchSwitchContext(
    _In_ BL_ARCH_MODE NewMode)
{
    PBL_ARCH_CONTEXT TargetContext;

    TargetContext = &BlFirmwareContext;
    if (NewMode != BlRealMode)
    {
        TargetContext = &BlApplicationContext;
    }

    if (CurrentExecutionContext != TargetContext)
    {
        ArchSwitchContext(TargetContext, CurrentExecutionContext);
        CurrentExecutionContext = TargetContext;
    }
}

VOID
BlpArchEnableTranslation(VOID)
{
    if (CurrentExecutionContext != NULL)
    {
        CurrentExecutionContext->ContextFlags |= BL_CONTEXT_PAGING_ON;
        BlAmd64SynchronizePagingFlag(CurrentExecutionContext);
    }
}

NTSTATUS
BlpArchInitialize(
    _In_ ULONG Phase)
{
    UNREFERENCED_PARAMETER(Phase);

    if (Phase == 0)
    {
        return ArchInitializeContexts();
    }

    /* Nothing special to do in later phases yet. */
    return STATUS_SUCCESS;
}

/*
 * The legacy BIOS code exposes a 32-bit transfer stub that the boot library
 * calls when hand-offing to 32-bit applications. For the UEFI-only AMD64 port
 * we never transition back to legacy 32-bit mode, so provide a no-op stub to
 * satisfy the link and keep the boot library happy.
 */
VOID
Archx86TransferTo32BitApplicationAsm(VOID)
{
    /* Intentionally empty: x64 UEFI does not use the legacy 32-bit path. */
}

NTSTATUS
OslArchTransferToKernel(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock,
    _In_ PVOID KernelEntrypoint)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(KernelEntrypoint);

    /* This will be replaced once the winload hand-off is wired up. */
    return STATUS_NOT_IMPLEMENTED;
}
