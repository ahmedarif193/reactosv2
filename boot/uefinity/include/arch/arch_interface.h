/*
 * Uefinity - Multi-Architecture UEFI Bootloader
 * Copyright (C) 2024 Ahmed ARIF (Arif193@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _UEFINITY_ARCH_INTERFACE_H_
#define _UEFINITY_ARCH_INTERFACE_H_

#include <uefi.h>

/* CPU Information Structure */
typedef struct _CPU_INFO {
    ULONG ProcessorType;
    ULONG ProcessorRevision;
    ULONG ProcessorFeatures;
    ULONG CacheLineSize;
    ULONG L1DataCacheSize;
    ULONG L1InstructionCacheSize;
    ULONG L2CacheSize;
    CHAR VendorString[64];
} CPU_INFO, *PCPU_INFO;

/* Exception Frame Structure */
typedef struct _EXCEPTION_FRAME {
    ULONG64 ExceptionType;
    ULONG64 ErrorCode;
    ULONG64 InstructionPointer;
    ULONG64 StackPointer;
    PVOID ArchSpecificData;  /* Architecture-specific registers */
} EXCEPTION_FRAME, *PEXCEPTION_FRAME;

/* Memory Map Structure */
typedef struct _MEMORY_MAP {
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    UINTN MemoryMapSize;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
} MEMORY_MAP, *PMEMORY_MAP;

/* Kernel Handoff Parameters */
typedef struct _KERNEL_PARAMS {
    PVOID LoaderBlock;
    PVOID SystemTable;
    PVOID MemoryMap;
    ULONG64 KernelEntry;
    ULONG64 ArchFlags;
} KERNEL_PARAMS, *PKERNEL_PARAMS;

/* Architecture Interface - Each architecture must implement these functions */
typedef struct _ARCH_INTERFACE {
    /* CPU Initialization and Detection */
    EFI_STATUS (*ArchInit)(VOID);
    EFI_STATUS (*ArchDetectCPU)(PCPU_INFO CpuInfo);
    VOID (*ArchEnableInterrupts)(VOID);
    VOID (*ArchDisableInterrupts)(VOID);

    /* Memory Management */
    EFI_STATUS (*ArchSetupMMU)(PMEMORY_MAP MemoryMap);
    EFI_STATUS (*ArchEnablePaging)(VOID);
    VOID (*ArchInvalidateTLB)(VOID);
    VOID (*ArchFlushCache)(VOID);

    /* Exception Handling */
    EFI_STATUS (*ArchSetupExceptions)(VOID);
    VOID (*ArchHandleException)(PEXCEPTION_FRAME Frame);

    /* Timer Support */
    EFI_STATUS (*ArchInitializeTimer)(VOID);
    ULONG64 (*ArchGetTimerFrequency)(VOID);
    ULONG64 (*ArchGetTimerValue)(VOID);
    VOID (*ArchStallExecution)(ULONG Microseconds);

    /* Kernel Handoff */
    EFI_STATUS (*ArchPrepareForKernel)(PKERNEL_PARAMS Params);
    VOID (*ArchJumpToKernel)(PVOID KernelEntry, PVOID LoaderBlock);

    /* Architecture Information */
    CONST CHAR8 *ArchName;           /* "ARM64", "AMD64", "RISCV64" */
    ULONG ArchFeatures;               /* Architecture-specific feature flags */
    ULONG ArchCacheLineSize;          /* Default cache line size */
    BOOLEAN RequiresIdentityMapping;  /* If TRUE, maintain identity mapping */
} ARCH_INTERFACE, *PARCH_INTERFACE;

/* Global architecture interface - must be provided by each arch */
extern ARCH_INTERFACE ArchInterface;

/* Architecture initialization function - called by main bootloader */
EFI_STATUS ArchInitialize(VOID);

/* Helper macros for architecture-specific code */
#ifdef _ARM64_
    #include "arm64.h"
    #define ARCH_NAME "ARM64"
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGN 16
#elif defined(_AMD64_)
    #include "amd64.h"
    #define ARCH_NAME "AMD64"
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGN 16
#elif defined(_RISCV64_)
    #include "riscv64.h"
    #define ARCH_NAME "RISCV64"
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGN 16
#else
    #error "Unsupported architecture"
#endif

/* Common architecture helpers */
static inline VOID ArchMemoryBarrier(VOID) {
    #ifdef _ARM64_
        __asm__ __volatile__("dmb sy" ::: "memory");
    #elif defined(_AMD64_)
        __asm__ __volatile__("mfence" ::: "memory");
    #elif defined(_RISCV64_)
        __asm__ __volatile__("fence" ::: "memory");
    #endif
}

static inline VOID ArchInstructionBarrier(VOID) {
    #ifdef _ARM64_
        __asm__ __volatile__("isb" ::: "memory");
    #elif defined(_AMD64_)
        /* No direct equivalent on x86 */
        __asm__ __volatile__("" ::: "memory");
    #elif defined(_RISCV64_)
        __asm__ __volatile__("fence.i" ::: "memory");
    #endif
}

#endif /* _UEFINITY_ARCH_INTERFACE_H_ */