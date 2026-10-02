/* Candidate implementation. Starts as a copy of the reference. */
#include "kernel.h"
#include <math.h>

static int64_t clampi64(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void upscale_bilinear_f32_opt(const float *src, int64_t ne00, int64_t ne01, int64_t n_planes,
                               float *dst, int64_t ne0, int64_t ne1) {
    const float pixel_offset = 0.5f;
    const float sf0 = (float)ne0 / (float)ne00;
    const float sf1 = (float)ne1 / (float)ne01;

    for (int64_t plane = 0; plane < n_planes; plane++) {
        const float *s = src + plane * ne00 * ne01;
        float *d = dst + plane * ne0 * ne1;

        for (int64_t i1 = 0; i1 < ne1; i1++) {
            const float y = ((float)i1 + pixel_offset) / sf1 - pixel_offset;
            int64_t y0 = (int64_t)floorf(y);
            int64_t y1 = y0 + 1;

            y0 = clampi64(y0, 0, ne01 - 1);
            y1 = clampi64(y1, 0, ne01 - 1);

            float dy = y - (float)y0;
            dy = clampf(dy, 0.0f, 1.0f);

            for (int64_t i0 = 0; i0 < ne0; i0++) {
                const float x = ((float)i0 + pixel_offset) / sf0 - pixel_offset;
                int64_t x0 = (int64_t)floorf(x);
                int64_t x1 = x0 + 1;

                x0 = clampi64(x0, 0, ne00 - 1);
                x1 = clampi64(x1, 0, ne00 - 1);

                float dx = x - (float)x0;
                dx = clampf(dx, 0.0f, 1.0f);

                const float a = s[y0 * ne00 + x0];
                const float b = s[y0 * ne00 + x1];
                const float c = s[y1 * ne00 + x0];
                const float e = s[y1 * ne00 + x1];

                const float val = a * (1 - dx) * (1 - dy) + b * dx * (1 - dy) +
                                   c * (1 - dx) * dy + e * dx * dy;

                d[i1 * ne0 + i0] = val;
            }
        }
    }
}
