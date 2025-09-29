/*
 * ReactOS ACPICA memory wrappers (temporary instrumentation)
 * FIXME: Remove after identifying memset/memcpy caller that corrupts
 *        the second root peer namespace node.
 */
#pragma once

#include <stddef.h>

extern void *AcpiRos_SuspectNode;
extern size_t AcpiRos_SuspectNodeSize;

void *AcpiRosMemcpy(void *dst, const void *src, size_t n, void *retaddr);
void *AcpiRosMemmove(void *dst, const void *src, size_t n, void *retaddr);
void *AcpiRosMemset(void *dst, int c, size_t n, void *retaddr);

#ifndef ACPI_ROS_MEMWRAP_DISABLED
#define memcpy(d,s,n)   AcpiRosMemcpy((d),(s),(n), __builtin_return_address(0))
#define memmove(d,s,n)  AcpiRosMemmove((d),(s),(n), __builtin_return_address(0))
#define memset(d,c,n)   AcpiRosMemset((d),(c),(n), __builtin_return_address(0))
#endif

