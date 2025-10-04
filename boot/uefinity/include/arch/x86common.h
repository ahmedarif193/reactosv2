/*
 * Uefinity - Multi-Architecture UEFI Bootloader
 * Copyright (C) 2024 Ahmed ARIF (Arif193@gmail.com)
 *
 * x86 Common Definitions (UEFI-only, no BIOS)
 */

#ifndef _UEFINITY_X86COMMON_H_
#define _UEFINITY_X86COMMON_H_

/* Minimal definitions for x86/AMD64 assembly files */
#ifndef HEX
#define HEX(y) 0x##y
#endif

/* Common x86 constants */
#define CR0_PE     HEX(01)    /* Protection Enable */
#define CR0_WP     HEX(10000) /* Write Protect */
#define CR0_PG     HEX(80000000) /* Paging */

#define CR4_PAE    HEX(20)    /* Physical Address Extension */
#define CR4_PSE    HEX(10)    /* Page Size Extension */

/* MSR definitions */
#define MSR_EFER   HEX(C0000080)
#define EFER_LME   HEX(100)   /* Long Mode Enable */
#define EFER_LMA   HEX(400)   /* Long Mode Active */

#endif /* _UEFINITY_X86COMMON_H_ */