/* Human expert version: libyuv source/row_neon64.cc (ARGBAttenuateRow_NEON),
 * BSD-3-Clause. The NEON loop handles 8 pixels at a time; libyuv's ANY11
 * wrapper handles the remainder, reproduced here as a scalar tail. */
#include "kernel.h"

#define ATTENUATE(f, a) (f * a + 255) >> 8

static void attenuate_neon8(const uint8_t* src_argb, uint8_t* dst_argb, int width) {
  asm volatile(
      "movi        v7.8h, #0x00ff                \n"
      "1:                                        \n"
      "ld4         {v0.8b,v1.8b,v2.8b,v3.8b}, [%0], #32 \n"
      "subs        %w2, %w2, #8                  \n"
      "umull       v4.8h, v0.8b, v3.8b           \n"
      "prfm        pldl1keep, [%0, 448]          \n"
      "umull       v5.8h, v1.8b, v3.8b           \n"
      "umull       v6.8h, v2.8b, v3.8b           \n"
      "addhn       v0.8b, v4.8h, v7.8h           \n"
      "addhn       v1.8b, v5.8h, v7.8h           \n"
      "addhn       v2.8b, v6.8h, v7.8h           \n"
      "st4         {v0.8b,v1.8b,v2.8b,v3.8b}, [%1], #32 \n"
      "b.gt        1b                            \n"
      : "+r"(src_argb), "+r"(dst_argb), "+r"(width)
      :
      : "cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7");
}

void ARGBAttenuateRow_opt(const uint8_t* src_argb, uint8_t* dst_argb, int width) {
  int n = width & ~7;
  if (n > 0) attenuate_neon8(src_argb, dst_argb, n);
  for (int i = n; i < width; i++) {
    const uint32_t b = src_argb[4 * i + 0];
    const uint32_t g = src_argb[4 * i + 1];
    const uint32_t r = src_argb[4 * i + 2];
    const uint32_t a = src_argb[4 * i + 3];
    dst_argb[4 * i + 0] = ATTENUATE(b, a);
    dst_argb[4 * i + 1] = ATTENUATE(g, a);
    dst_argb[4 * i + 2] = ATTENUATE(r, a);
    dst_argb[4 * i + 3] = (uint8_t)a;
  }
}
