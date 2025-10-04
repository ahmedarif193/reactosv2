/*
 * Uefinity - Multi-Architecture UEFI Bootloader
 * Copyright (C) 2024 Ahmed ARIF (Arif193@gmail.com)
 *
 * RISC-V64 Architecture Support - Placeholder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <uefi.h>
#include "../include/arch/arch_interface.h"


/*
 * RISC-V64 architecture support is planned for future implementation.
 * This will provide UEFI boot support for RISC-V 64-bit systems.
 *
 * TODO:
 * - Implement RISC-V MMU setup (Sv39/Sv48 paging)
 * - Add RISC-V CPU feature detection
 * - Implement trap handling
 * - Add timer support (RISC-V timer registers)
 * - Implement UEFI to kernel handoff for RISC-V
 * - Add SBI (Supervisor Binary Interface) support if needed
 */

EFI_STATUS RiscV64Init(VOID)
{
    /* Placeholder implementation */
    return EFI_UNSUPPORTED;
}

/* Architecture interface implementation for RISC-V64 */
ARCH_INTERFACE ArchInterface = {
    .ArchInit = RiscV64Init,
    .ArchName = "RISCV64",
    .ArchCacheLineSize = 64,
    .RequiresIdentityMapping = TRUE,
    /* Other function pointers will be added during implementation */
};