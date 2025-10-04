#pragma once

#include <stdint.h>

/* Image utility helpers shared across the UI pipeline */

void UiResampleRGBA8(const uint8_t* src, unsigned sw, unsigned sh,
                     uint8_t* dst, unsigned dw, unsigned dh);

void UiRGBAtoBGRA(const uint8_t* rgba, uint8_t* bgra, unsigned w, unsigned h);

void UiComputeCoverDimensions(unsigned src_w, unsigned src_h,
                              unsigned screen_w, unsigned screen_h,
                              unsigned* out_w, unsigned* out_h);

