#ifndef TASK_KERNEL_H
#define TASK_KERNEL_H
#include <stdint.h>

/* Multiply the B, G, R channels of each ARGB pixel (byte order B,G,R,A in
 * memory) by its alpha: c' = (c * a + 255) >> 8. Alpha is copied unchanged.
 * `width` is the number of pixels, any value >= 0. */
void ARGBAttenuateRow_ref(const uint8_t* src_argb, uint8_t* dst_argb, int width);
void ARGBAttenuateRow_opt(const uint8_t* src_argb, uint8_t* dst_argb, int width);

#endif
