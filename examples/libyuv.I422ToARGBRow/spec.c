/* Task adapter: workloads, inputs, calls and outputs. */
#include <stdlib.h>
#include "kr.h"
#include "kernel.h"

/* BT.601 limited range (libyuv's kYuvI601Constants), used by I422ToARGB(),
 * the most common call path (source/convert_argb.cc). */
static const struct YuvConstants kYuvI601Constants = {
    {128, 102, 25, 52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18997, 17544, 8696, 14216, -1160, 0, 0, 0}};

/* BT.601 full range / JPEG (libyuv's kYuvJPEGConstants), used by
 * J422ToARGB() (e.g. MJPEG decode, source/convert_argb.cc /
 * source/convert_jpeg.cc). */
static const struct YuvConstants kYuvJPEGConstants = {
    {113, 90, 22, 46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {16320, 14432, 8736, 11488, 32, 0, 0, 0}};

typedef struct {
  const char *name;
  int width;
  const struct YuvConstants *yuv;
  int extreme; /* force src bytes to alternate 0/255 instead of random */
  double weight;
} spec_t;

/* Widths mirror libyuv's real call sites (source/convert_argb.cc
 * I422ToARGBMatrix): I422ToARGB() converts a whole I422 frame row by row
 * (or as one coalesced row when the buffers are contiguous), so common
 * widths are 720p/1080p/4K row widths. J422ToARGB (MJPEG path, e.g.
 * source/convert_jpeg.cc) uses the same row kernel with JPEG (full range)
 * constants. A width that is not a multiple of 8 exercises the scalar tail
 * even in a timed workload; tiny/zero widths and forced 0/255 sample
 * extremes are correctness-only (weight 0). */
static const spec_t W[] = {
    {"w1280_i601", 1280, &kYuvI601Constants, 0, 1.0},
    {"w1920_i601", 1920, &kYuvI601Constants, 0, 1.0},
    {"w3840_i601", 3840, &kYuvI601Constants, 0, 1.0},
    {"w1280_jpeg", 1280, &kYuvJPEGConstants, 0, 0.5},
    {"w1923_tail_i601", 1923, &kYuvI601Constants, 0, 0.5},
    {"w1", 1, &kYuvI601Constants, 0, 0.0},
    {"w2", 2, &kYuvI601Constants, 0, 0.0},
    {"w7", 7, &kYuvI601Constants, 0, 0.0},
    {"w9", 9, &kYuvI601Constants, 0, 0.0},
    {"w0", 0, &kYuvI601Constants, 0, 0.0},
    {"w65_extreme_jpeg", 65, &kYuvJPEGConstants, 1, 0.0},
};

typedef struct {
  int width;
  const struct YuvConstants *yuv;
  uint8_t *y, *u, *v, *dst_ref, *dst_opt;
} ctx_t;

int kr_num_workloads(void) { return (int)(sizeof(W) / sizeof(W[0])); }
const char *kr_workload_name(int w) { return W[w].name; }
double kr_workload_weight(int w) { return W[w].weight; }

void *kr_setup(int w, uint64_t seed) {
  ctx_t *c = malloc(sizeof *c);
  int width = W[w].width;
  size_t uvsize = (size_t)(width + 1) / 2;
  c->width = width;
  c->yuv = W[w].yuv;
  c->y = kr_alloc((size_t)width);
  c->u = kr_alloc(uvsize);
  c->v = kr_alloc(uvsize);
  c->dst_ref = kr_alloc((size_t)width * 4);
  c->dst_opt = kr_alloc((size_t)width * 4);

  kr_fill_u8(c->y, (size_t)width, &seed);
  kr_fill_u8(c->u, uvsize, &seed);
  kr_fill_u8(c->v, uvsize, &seed);

  if (W[w].extreme) {
    /* Alternate 0/255 across all three planes to hammer the fixed point
     * clamp paths (both underflow and overflow of the 2.14 intermediate). */
    int i;
    for (i = 0; i < width; i++) c->y[i] = (i & 1) ? 255 : 0;
    for (i = 0; i < (int)uvsize; i++) {
      c->u[i] = (i & 1) ? 0 : 255;
      c->v[i] = (i & 1) ? 255 : 0;
    }
  } else {
    /* Force the sample extremes 0 and 255 to always appear. */
    if (width > 0) {
      c->y[0] = 0;
      c->y[width - 1] = 255;
    }
    if (uvsize > 0) {
      c->u[0] = 0;
      c->v[0] = 255;
    }
    if (uvsize > 1) {
      c->u[uvsize - 1] = 255;
      c->v[uvsize - 1] = 0;
    }
  }
  return c;
}

void kr_run_ref(void *p) {
  ctx_t *c = p;
  I422ToARGBRow_ref(c->y, c->u, c->v, c->dst_ref, c->yuv, c->width);
}

void kr_run_opt(void *p) {
  ctx_t *c = p;
  I422ToARGBRow_opt(c->y, c->u, c->v, c->dst_opt, c->yuv, c->width);
}

int kr_outputs(void *p, kr_output *o, int max) {
  ctx_t *c = p;
  (void)max;
  o[0] = (kr_output){"rgb_buf", c->dst_ref, c->dst_opt, (size_t)c->width * 4,
                      KR_U8, 0, 0, 0};
  return 1;
}

void kr_teardown(void *p) {
  ctx_t *c = p;
  kr_free(c->y);
  kr_free(c->u);
  kr_free(c->v);
  kr_free(c->dst_ref);
  kr_free(c->dst_opt);
  free(c);
}
