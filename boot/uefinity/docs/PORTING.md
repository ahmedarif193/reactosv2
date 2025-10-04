# Porting Uefinity to New Architectures

This guide explains how to add support for a new architecture to Uefinity.

## Architecture Requirements

Before porting Uefinity to a new architecture, ensure:
1. UEFI firmware support exists for the target architecture
2. A cross-compiler toolchain is available
3. UEFI development headers for the architecture

## Step-by-Step Porting Guide

### 1. Create Architecture Directory

Create a new directory under `arch/` for your architecture:
```
arch/
└── your_arch/
    ├── init.c           # Architecture initialization
    ├── mmu.c           # Memory management unit setup
    ├── exceptions.c    # Exception/interrupt handling
    ├── timer.c         # Timer support
    └── handoff.c       # Kernel handoff implementation
```

### 2. Implement Architecture Interface

Your architecture must implement all functions defined in `include/arch/arch_interface.h`:

```c
/* Required functions in arch/your_arch/init.c */

EFI_STATUS YourArchInit(VOID);
EFI_STATUS YourArchDetectCPU(PARCH_CPU_INFO CpuInfo);
EFI_STATUS YourArchSetupMMU(PMEMORY_MAP MemoryMap);
EFI_STATUS YourArchSetupExceptions(VOID);
VOID YourArchJumpToKernel(PVOID Entry, PVOID LoaderBlock);

/* Export the interface */
ARCH_INTERFACE ArchInterface = {
    .ArchInit = YourArchInit,
    .ArchDetectCPU = YourArchDetectCPU,
    .ArchSetupMMU = YourArchSetupMMU,
    // ... fill all function pointers
    .ArchName = "YOURARCH",
    .ArchCacheLineSize = 64,  // Your architecture's cache line size
    .RequiresIdentityMapping = TRUE,
};
```

### 3. Add Architecture Header

Create `include/arch/yourarch.h` with architecture-specific definitions:

```c
#ifndef _UEFINITY_YOURARCH_H_
#define _UEFINITY_YOURARCH_H_

/* Architecture-specific register definitions */
/* Architecture-specific constants */
/* Inline assembly helpers */

#endif
```

### 4. Update CMakeLists.txt

Add your architecture to the main `CMakeLists.txt`:

```cmake
elseif(ARCH STREQUAL "yourarch")
    # Architecture-specific flags
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=yourarch")
    add_definitions(-D_YOURARCH_ -DYOURARCH)

    # Architecture source files
    set(ARCH_SOURCES
        arch/yourarch/init.c
        arch/yourarch/mmu.c
        arch/yourarch/exceptions.c
        arch/yourarch/timer.c
        arch/yourarch/handoff.c
    )
```

### 5. Implement Required Functionality

#### Memory Management
- Set up page tables/TLB as required by your architecture
- Maintain identity mapping if needed for UEFI
- Implement cache management functions

#### Exception Handling
- Set up exception/interrupt vectors
- Implement exception handlers
- Provide stack unwinding if possible

#### Timer Support
- Initialize architecture timer
- Provide delay/stall functions
- Implement time measurement

#### Kernel Handoff
- Prepare CPU state for kernel entry
- Set up initial stack
- Jump to kernel entry point with proper parameters

### 6. Testing Your Port

1. Build Uefinity for your architecture:
   ```bash
   mkdir build-yourarch
   cd build-yourarch
   cmake .. -DARCH=yourarch
   make
   ```

2. Test on QEMU or real hardware with UEFI firmware

3. Verify:
   - UEFI services work correctly
   - File systems can be accessed
   - Boot menu appears
   - OS can be loaded and started

### 7. Architecture-Specific Considerations

#### Stack Alignment
Define proper stack alignment in your architecture header:
```c
#define ARCH_STACK_ALIGN 16  // Or your architecture's requirement
```

#### Endianness
Handle endianness if different from little-endian:
```c
#ifdef BIG_ENDIAN
    // Add byte-swapping for UEFI structures
#endif
```

#### Calling Convention
Ensure compliance with UEFI calling convention for your architecture.

## Example: Adding MIPS64 Support

```c
// arch/mips64/init.c
#include <uefi.h>
#include "arch_interface.h"

EFI_STATUS Mips64Init(VOID)
{
    // Initialize MIPS64 CP0 registers
    // Set up exception base
    // Configure cache
    return EFI_SUCCESS;
}

// Continue implementing all required functions...
```

## Submitting Your Port

1. Ensure all code follows project coding standards
2. Add documentation for architecture-specific features
3. Include test results and supported platforms
4. Submit pull request with detailed description

## Getting Help

For questions about porting:
- Contact maintainer: Ahmed ARIF <Arif193@gmail.com>
- Open an issue on the project repository
- Consult UEFI specification for your architecture

## Architecture Support Status

| Architecture | Status | Maintainer |
|-------------|---------|-----------|
| ARM64 | Complete | Ahmed ARIF |
| AMD64 | In Progress | TBD |
| RISC-V64 | Planned | TBD |
| Your Arch | Your Status | Your Name |
