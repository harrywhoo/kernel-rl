#ifndef TASK_KERNEL_H
#define TASK_KERNEL_H
#include <stdint.h>

/* Fixed point YUV->RGB coefficients, laid out exactly like libyuv's
 * struct YuvConstants for ARM/RISC-V (include/libyuv/row.h):
 *   kUVCoeff       = {UB, VR, UG, VG, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
 *   kRGBCoeffBias  = {YG, UB*128-YB, UG*128+VG*128+YB, VR*128-YB, YB, 0, 0, 0}
 * where (YG, YB, UB, UG, VG, VR) are the matrix/bias constants for a given
 * color space (e.g. libyuv's kYuvI601Constants for BT.601 limited range,
 * kYuvJPEGConstants for full range JPEG/BT.601). */
struct YuvConstants {
  uint8_t kUVCoeff[16];
  int16_t kRGBCoeffBias[8];
};

/* Convert one row of I422 (4:2:2 planar YUV, 8 bits/sample) to ARGB.
 *
 * - src_y: `width` luma samples, one per output pixel.
 * - src_u, src_v: chroma samples, one per 2 luma samples: ceil(width/2)
 *   samples each. Pixels 2*i and 2*i+1 share src_u[i]/src_v[i]; if width is
 *   odd, the last luma sample also uses the last (only) U/V sample of its
 *   pair index.
 * - rgb_buf: `width` BGRA pixels (4 * width bytes, byte order B,G,R,A in
 *   memory). Alpha is always written as 255.
 * - yuvconstants: fixed point conversion matrix, see struct above. Never
 *   NULL.
 * - width: number of luma pixels to produce. Any value >= 0.
 */
void I422ToARGBRow_ref(const uint8_t* src_y,
                        const uint8_t* src_u,
                        const uint8_t* src_v,
                        uint8_t* rgb_buf,
                        const struct YuvConstants* yuvconstants,
                        int width);
void I422ToARGBRow_opt(const uint8_t* src_y,
                        const uint8_t* src_u,
                        const uint8_t* src_v,
                        uint8_t* rgb_buf,
                        const struct YuvConstants* yuvconstants,
                        int width);

#endif
