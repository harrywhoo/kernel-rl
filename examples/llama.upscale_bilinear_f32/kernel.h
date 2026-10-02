#ifndef TASK_KERNEL_H
#define TASK_KERNEL_H
#include <stdint.h>

/*
 * Bilinear image resize (ggml's GGML_SCALE_MODE_BILINEAR, the default:
 * align_corners=false, antialias=false), generalized over a stack of
 * independent planes.
 *
 * `src` holds `n_planes` planes of shape [ne01][ne00] (row-major, ne00
 * innermost), F32, stored back to back with no padding between planes.
 * `dst` holds `n_planes` planes of shape [ne1][ne0] (row-major), F32,
 * contiguous, in the same plane order.
 *
 * For each plane and each output pixel (i0, i1), the source coordinate is
 * computed with the half-pixel-center convention
 *   x = (i0 + 0.5) / sf0 - 0.5,   sf0 = ne0 / ne00
 *   y = (i1 + 0.5) / sf1 - 0.5,   sf1 = ne1 / ne01
 * and the result is a bilinear blend of the 4 nearest source pixels
 * (floor(x)/floor(x)+1, floor(y)/floor(y)+1), each coordinate clamped to
 * [0, ne00-1] / [0, ne01-1] so out-of-range taps repeat the edge pixel.
 *
 * Valid ranges: ne00 >= 1, ne01 >= 1 (each plane must have at least one
 * source pixel per axis); n_planes >= 0; ne0 >= 0, ne1 >= 0 (an output
 * plane may be empty on either axis, in which case nothing is written for
 * that plane).
 */
void upscale_bilinear_f32_ref(const float *src, int64_t ne00, int64_t ne01, int64_t n_planes,
                               float *dst, int64_t ne0, int64_t ne1);

void upscale_bilinear_f32_opt(const float *src, int64_t ne00, int64_t ne01, int64_t n_planes,
                               float *dst, int64_t ne0, int64_t ne1);

#endif
