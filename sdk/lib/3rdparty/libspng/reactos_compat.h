/*
 * ReactOS compatibility shim for libspng builds.
 * Provides minimal definitions missing from the freestanding CRT surface.
 */
#pragma once

#include <math.h>

#ifndef fpclassify
#define fpclassify(_x) (((_x) == 0.0f || (_x) == -0.0f) ? FP_ZERO : FP_NORMAL)
#endif
