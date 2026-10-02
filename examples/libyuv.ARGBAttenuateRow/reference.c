/* Extracted from libyuv source/row_common.cc (ARGBAttenuateRow_C), BSD-3-Clause. */
#include "kernel.h"

#define ATTENUATE(f, a) (f * a + 255) >> 8

void ARGBAttenuateRow_ref(const uint8_t* src_argb, uint8_t* dst_argb, int width) {
  int i;
  for (i = 0; i < width - 1; i += 2) {
    uint32_t b = src_argb[0];
    uint32_t g = src_argb[1];
    uint32_t r = src_argb[2];
    uint32_t a = src_argb[3];
    dst_argb[0] = ATTENUATE(b, a);
    dst_argb[1] = ATTENUATE(g, a);
    dst_argb[2] = ATTENUATE(r, a);
    dst_argb[3] = (uint8_t)a;
    b = src_argb[4];
    g = src_argb[5];
    r = src_argb[6];
    a = src_argb[7];
    dst_argb[4] = ATTENUATE(b, a);
    dst_argb[5] = ATTENUATE(g, a);
    dst_argb[6] = ATTENUATE(r, a);
    dst_argb[7] = (uint8_t)a;
    src_argb += 8;
    dst_argb += 8;
  }

  if (width & 1) {
    const uint32_t b = src_argb[0];
    const uint32_t g = src_argb[1];
    const uint32_t r = src_argb[2];
    const uint32_t a = src_argb[3];
    dst_argb[0] = ATTENUATE(b, a);
    dst_argb[1] = ATTENUATE(g, a);
    dst_argb[2] = ATTENUATE(r, a);
    dst_argb[3] = (uint8_t)a;
  }
}
