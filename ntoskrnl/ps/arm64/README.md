# ARM64 Process Creation Implementation

## Overview

This directory contains the ARM64-specific process creation implementation for ReactOS. The implementation provides critical process and thread creation functionality needed for ARM64 kernel boot and system initialization.

## Files

### `create.c`
Main ARM64 process creation implementation with the following key components:

- **ASID Management**: Address Space Identifier allocation for ARM64 TLB management
- **Process Address Space Creation**: TTBR0 setup for process-specific virtual memory
- **Thread Context Initialization**: ARM64 register context setup (X0-X30, SP, PC, PSTATE)
- **PEB Creation**: Process Environment Block setup with ARM64-specific processor features
- **Security Context**: Process security initialization with ARM64 capabilities

### `psctx.c`
ARM64 process context get/set operations for debugging and thread management.

### `psarm64.c`
ARM64-specific process and thread management functions (stubs and platform-specific code).

## Key Features Implemented

### 1. ASID (Address Space Identifier) Management
- Bitmap-based ASID allocation (256 ASIDs supported)
- ASID rollover with TLB invalidation strategy
- Thread-safe allocation with spinlock protection

### 2. ARM64 Address Space Creation
- TTBR0_EL1 page table setup for user space
- Physical page allocation for page directories
- Kernel space mapping inheritance
- Process-specific virtual address space isolation

### 3. ARM64 Thread Context Setup
- Complete ARM64 register context initialization (X0-X30)
- Stack pointer (SP) and program counter (PC) setup
- Processor state (PSTATE) configuration for user/kernel mode
- Frame pointer (FP) and link register (LR) initialization
- ARM64 ABI compliance (16-byte stack alignment)

### 4. Process Environment Block (PEB)
- Standard PEB allocation at 0x7FFDF000
- ARM64 processor feature flags setup
- Windows compatibility version information
- Handle table and security context initialization

### 5. Memory Management Integration
- Kernel stack allocation for threads
- Trap frame setup on stack
- Memory protection attributes for ARM64
- Cache coherency considerations

## ARM64-Specific Considerations

### Memory Model
- Weakly-ordered memory model handling
- Proper memory barriers (DMB, DSB, ISB) where needed
- Cache management for instruction/data coherency

### Exception Levels
- EL0 (user mode) vs EL1 (kernel mode) context setup
- Proper PSTATE configuration for security
- Exception vector setup for user mode transitions

### Security Features
- Support for ARM64 security extensions (PAC, BTI, MTE)
- Secure context switching preparations
- Memory tagging readiness

### ABI Compliance
- ARM64 calling convention adherence
- Stack alignment requirements (16 bytes)
- Register usage conventions (X0-X30, SP, PC)

## Integration Points

### With Memory Manager
- Uses `MiAllocatePoolPages()` for page directory allocation
- Uses `MmCreateKernelStack()` for thread stack creation
- Uses `MiGetPfnForVirtualAddress()` for physical address translation

### With Process Manager
- Extends existing `PspCreateProcess()` infrastructure
- Integrates with process notification callbacks
- Works with existing process/thread object management

### With Kernel Executive
- Uses standard kernel object allocation
- Integrates with handle table management
- Works with security subsystem

### With Hardware Abstraction Layer (HAL)
- Coordinates with ARM64 MMU management
- Uses GIC (Generic Interrupt Controller) for interrupt handling
- Integrates with ARM64 cache management

## Functions Exported

### Public Functions (declared in ps.h)
- `PspInitializeArm64ProcessCreation()` - Initialize the subsystem
- `PspCreateArm64SystemProcess()` - Create initial system process (PID 4)
- `PspCreateArm64UserProcess()` - Create user processes
- `PspCreateArm64ProcessThread()` - Create threads for processes
- `PspGetArm64ProcessStatistics()` - Get creation statistics

### Internal Functions
- `PspAllocateAsid()` / `PspFreeAsid()` - ASID management
- `PspCreateArm64AddressSpace()` - Address space setup
- `PspInitializeArm64ThreadContext()` - Thread context initialization
- `PspCreateArm64Peb()` - PEB creation and mapping
- `PspInitializeArm64ProcessSecurity()` - Security context setup

## Critical for Boot Process

This implementation is essential for:
1. **System Process Creation** - The initial system process (PID 4) needed for kernel operation
2. **Session Manager** - Creating smss.exe for session initialization
3. **Desktop Reach** - Enabling the full process creation pipeline needed for Windows subsystem

## Usage Example

```c
// Initialize the ARM64 process creation subsystem
Status = PspInitializeArm64ProcessCreation();

// Create the system process
Status = PspCreateArm64SystemProcess(&SystemProcess, &SystemThread);

// Create a user process
Status = PspCreateArm64UserProcess(&UserProcess, ParentProcess,
                                   &ImageFileName, ProcessFlags);

// Create initial thread for the process
Status = PspCreateArm64ProcessThread(UserProcess, &Thread,
                                      StartAddress, Parameter,
                                      NULL, FALSE);
```

## Future Enhancements

1. **Security Extensions**: Full implementation of ARM64 security features
2. **Performance Optimization**: ASID caching and TLB management improvements
3. **Debug Support**: Enhanced debugging capabilities for ARM64 processes
4. **Compatibility**: 32-bit process support (AArch32 compatibility mode)

## Notes

- All functions follow ReactOS coding conventions
- Memory barriers are used appropriately for ARM64 weak memory model
- Error handling follows NT status code conventions
- Debug output uses standard ReactOS DPRINT macros