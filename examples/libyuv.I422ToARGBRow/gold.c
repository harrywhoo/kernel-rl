/* Human expert version: libyuv source/row_neon64.cc (I422ToARGBRow_NEON),
 * BSD-3-Clause. The NEON loop (READYUV422 + I4XXTORGB + RGBTORGB8, inlined
 * below) handles 8 Y pixels (4 U/V samples) per iteration and is kept
 * verbatim. libyuv only ever calls this row function on the part of a row
 * that is a multiple of 8 pixels (see source/row_any.cc, ANY31C instantiated
 * for I422ToARGBRow_NEON with MASK=7); the remaining 0-7 pixels are handled
 * by libyuv's generic ANY31C wrapper, which re-invokes the same NEON kernel
 * on a zero-padded 8-wide scratch buffer and copies out only the valid
 * bytes. Since the NEON kernel and the C reference are designed to agree
 * bit-for-bit, we reproduce that tail more directly here: the same fixed
 * point per-pixel formula as the reference, applied to the leftover
 * pixels. */
#include <stddef.h>
#include "kernel.h"

#define YUVTORGB_REGS                                                        \
  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v24", \
      "v25", "v26", "v27", "v28", "v29", "v30", "v31"

static void I422ToARGBRow_NEON8(const uint8_t* src_y,
                                 const uint8_t* src_u,
                                 const uint8_t* src_v,
                                 uint8_t* dst_argb,
                                 const struct YuvConstants* yuvconstants,
                                 int width) {
  asm volatile(
      "ld4r       {v28.16b, v29.16b, v30.16b, v31.16b}, [%[kUVCoeff]] \n"
      "ld4r       {v24.8h, v25.8h, v26.8h, v27.8h}, [%[kRGBCoeffBias]] \n"
      "movi        v19.8b, #255                  \n" /* A */
      "1:          \n"                               //
      // READYUV422: read 8 Y, 4 U and 4 V from 422.
      "ldr        d0, [%[src_y]], #8             \n"
      "ldr        s1, [%[src_u]], #4             \n"
      "ldr        s2, [%[src_v]], #4             \n"
      "zip1       v0.16b, v0.16b, v0.16b         \n"
      "prfm       pldl1keep, [%[src_y], 448]     \n"
      "zip1       v1.8b, v1.8b, v1.8b            \n"
      "zip1       v2.8b, v2.8b, v2.8b            \n"
      "prfm       pldl1keep, [%[src_u], 128]     \n"
      "prfm       pldl1keep, [%[src_v], 128]     \n"
      "subs        %w[width], %w[width], #8      \n"
      // I4XXTORGB: convert from YUV (I444 or I420/I422) to 2.14 fixed RGB.
      "umull2     v3.4s, v0.8h, v24.8h           \n"
      "umull      v6.8h, v1.8b, v30.8b           \n"
      "umull      v0.4s, v0.4h, v24.4h           \n"
      "umlal      v6.8h, v2.8b, v31.8b           \n" /* DG */
      "uzp2       v0.8h, v0.8h, v3.8h            \n" /* Y */
      "umull      v4.8h, v1.8b, v28.8b           \n" /* DB */
      "umull      v5.8h, v2.8b, v29.8b           \n" /* DR */
      "add        v17.8h, v0.8h, v26.8h          \n" /* G */
      "add        v16.8h, v0.8h, v4.8h           \n" /* B */
      "add        v18.8h, v0.8h, v5.8h           \n" /* R */
      "uqsub      v17.8h, v17.8h, v6.8h          \n" /* G */
      "uqsub      v16.8h, v16.8h, v25.8h         \n" /* B */
      "uqsub      v18.8h, v18.8h, v27.8h         \n" /* R */
      // RGBTORGB8: convert from 2.14 fixed point RGB to 8 bit RGB.
      "uqshrn     v17.8b, v17.8h, #6             \n"
      "uqshrn     v16.8b, v16.8h, #6             \n"
      "uqshrn     v18.8b, v18.8h, #6             \n"
      "st4         {v16.8b,v17.8b,v18.8b,v19.8b}, [%[dst_argb]], #32 \n"
      "b.gt        1b                            \n"
      : [src_y] "+r"(src_y),                                // %[src_y]
        [src_u] "+r"(src_u),                                // %[src_u]
        [src_v] "+r"(src_v),                                // %[src_v]
        [dst_argb] "+r"(dst_argb),                          // %[dst_argb]
        [width] "+r"(width)                                 // %[width]
      : [kUVCoeff] "r"(yuvconstants->kUVCoeff),             // %[kUVCoeff]
        [kRGBCoeffBias] "r"(yuvconstants->kRGBCoeffBias)    // %[kRGBCoeffBias]
      : "cc", "memory", YUVTORGB_REGS, "v19");
}

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

static inline void YuvPixel(uint8_t y,
                             uint8_t u,
                             uint8_t v,
                             uint8_t* b,
                             uint8_t* g,
                             uint8_t* r,
                             const struct YuvConstants* yuvconstants) {
  int ub = yuvconstants->kUVCoeff[0];
  int vr = yuvconstants->kUVCoeff[1];
  int ug = yuvconstants->kUVCoeff[2];
  int vg = yuvconstants->kUVCoeff[3];
  int yg = yuvconstants->kRGBCoeffBias[0];
  int bb = yuvconstants->kRGBCoeffBias[1];
  int bg = yuvconstants->kRGBCoeffBias[2];
  int br = yuvconstants->kRGBCoeffBias[3];
  uint32_t y32 = y * 0x0101;
  int32_t y1 = (uint32_t)(y32 * yg) >> 16;
  int b16 = y1 + (u * ub) - bb;
  int g16 = y1 + bg - (u * ug + v * vg);
  int r16 = y1 + (v * vr) - br;
  *b = (uint8_t)(Clamp((int32_t)(b16) >> 6));
  *g = (uint8_t)(Clamp((int32_t)(g16) >> 6));
  *r = (uint8_t)(Clamp((int32_t)(r16) >> 6));
}

void I422ToARGBRow_opt(const uint8_t* src_y,
                        const uint8_t* src_u,
                        const uint8_t* src_v,
                        uint8_t* rgb_buf,
                        const struct YuvConstants* yuvconstants,
                        int width) {
  int n = width & ~7;
  if (n > 0) {
    I422ToARGBRow_NEON8(src_y, src_u, src_v, rgb_buf, yuvconstants, n);
  }
  if (n < width) {
    const uint8_t* y_ptr = src_y + n;
    const uint8_t* u_ptr = src_u + (n >> 1);
    const uint8_t* v_ptr = src_v + (n >> 1);
    uint8_t* rgb_ptr = rgb_buf + (size_t)n * 4;
    int rem = width - n;
    int x;
    for (x = 0; x < rem - 1; x += 2) {
      YuvPixel(y_ptr[0], u_ptr[0], v_ptr[0], rgb_ptr + 0, rgb_ptr + 1,
               rgb_ptr + 2, yuvconstants);
      rgb_ptr[3] = 255;
      YuvPixel(y_ptr[1], u_ptr[0], v_ptr[0], rgb_ptr + 4, rgb_ptr + 5,
               rgb_ptr + 6, yuvconstants);
      rgb_ptr[7] = 255;
      y_ptr += 2;
      u_ptr += 1;
      v_ptr += 1;
      rgb_ptr += 8;
    }
    if (rem & 1) {
      YuvPixel(y_ptr[0], u_ptr[0], v_ptr[0], rgb_ptr + 0, rgb_ptr + 1,
               rgb_ptr + 2, yuvconstants);
      rgb_ptr[3] = 255;
    }
  }
}
