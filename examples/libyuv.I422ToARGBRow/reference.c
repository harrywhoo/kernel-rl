/* Extracted from libyuv source/row_common.cc (I422ToARGBRow_C), BSD-3-Clause.
 * YuvPixel() and the clamp helpers it needs are copied verbatim from the
 * same file (only the LOAD_YUV_CONSTANTS/CALC_RGB16 variant used on
 * aarch64/arm/riscv, matching the YuvConstants layout in kernel.h). */
#include "kernel.h"

static inline int32_t clamp0(int32_t v) {
  return -(v >= 0) & v;
}

static inline int32_t clamp255(int32_t v) {
  return (-(v >= 255) | v) & 255;
}

static inline uint32_t Clamp(int32_t val) {
  int v = clamp0(val);
  return (uint32_t)(clamp255(v));
}

#define LOAD_YUV_CONSTANTS                 \
  int ub = yuvconstants->kUVCoeff[0];      \
  int vr = yuvconstants->kUVCoeff[1];      \
  int ug = yuvconstants->kUVCoeff[2];      \
  int vg = yuvconstants->kUVCoeff[3];      \
  int yg = yuvconstants->kRGBCoeffBias[0]; \
  int bb = yuvconstants->kRGBCoeffBias[1]; \
  int bg = yuvconstants->kRGBCoeffBias[2]; \
  int br = yuvconstants->kRGBCoeffBias[3]

#define CALC_RGB16                        \
  int32_t y1 = (uint32_t)(y32 * yg) >> 16; \
  int b16 = y1 + (u * ub) - bb;            \
  int g16 = y1 + bg - (u * ug + v * vg);   \
  int r16 = y1 + (v * vr) - br

static inline void YuvPixel(uint8_t y,
                             uint8_t u,
                             uint8_t v,
                             uint8_t* b,
                             uint8_t* g,
                             uint8_t* r,
                             const struct YuvConstants* yuvconstants) {
  LOAD_YUV_CONSTANTS;
  uint32_t y32 = y * 0x0101;
  CALC_RGB16;
  *b = (uint8_t)(Clamp((int32_t)(b16) >> 6));
  *g = (uint8_t)(Clamp((int32_t)(g16) >> 6));
  *r = (uint8_t)(Clamp((int32_t)(r16) >> 6));
}

void I422ToARGBRow_ref(const uint8_t* src_y,
                        const uint8_t* src_u,
                        const uint8_t* src_v,
                        uint8_t* rgb_buf,
                        const struct YuvConstants* yuvconstants,
                        int width) {
  int x;
  for (x = 0; x < width - 1; x += 2) {
    YuvPixel(src_y[0], src_u[0], src_v[0], rgb_buf + 0, rgb_buf + 1,
             rgb_buf + 2, yuvconstants);
    rgb_buf[3] = 255;
    YuvPixel(src_y[1], src_u[0], src_v[0], rgb_buf + 4, rgb_buf + 5,
             rgb_buf + 6, yuvconstants);
    rgb_buf[7] = 255;
    src_y += 2;
    src_u += 1;
    src_v += 1;
    rgb_buf += 8;  // Advance 2 pixels.
  }
  if (width & 1) {
    YuvPixel(src_y[0], src_u[0], src_v[0], rgb_buf + 0, rgb_buf + 1,
             rgb_buf + 2, yuvconstants);
    rgb_buf[3] = 255;
  }
}
