/* Task adapter: workloads, inputs, calls and outputs. */
#include <stdlib.h>
#include "kr.h"
#include "kernel.h"

typedef struct {
  const char *name;
  int64_t ne00, ne01, n_planes;
  int64_t ne0, ne1;
  int extra;      /* EXTRA_* below */
  double weight;  /* 0 = correctness-only */
} workload_t;

enum { EXTRA_NONE, EXTRA_LARGE_MAG };

/*
 * Shapes come from the two real callers of plain (non-align-corners) bilinear
 * upscale in tools/mtmd:
 *  - siglip2_pos_resize_{up,down}: clip_graph::resize_position_embeddings()
 *    (tools/mtmd/clip.cpp), used by NaFlex-style projectors (muse-glimmer,
 *    deepseekocr's ggml_interpolate call) to resize the model's stored
 *    n_per_side x n_per_side learned position grid to the actual image's
 *    patch grid. n_planes = n_embd = 1152, SigLIP-so400m's hidden size (the
 *    ViT backbone behind several of these projectors); grid sizes are
 *    representative NaFlex patch counts, not one specific checkpoint.
 *  - deepseekocr_relpos: get_rel_pos() in tools/mtmd/models/deepseekocr.cpp,
 *    a SAM-style relative-position table resized along one axis only
 *    (ne01 = ne1 = 1), n_planes = head_dim (64), from a stored table of
 *    2*64-1 = 127 positions (max window 64) to 2*32-1 = 63 (window 32).
 */
static const workload_t W[] = {
    {"siglip2_pos_resize_up",   16, 16, 1152, 27, 27, EXTRA_NONE, 1.0},
    {"siglip2_pos_resize_down", 32, 32, 1152, 24, 24, EXTRA_NONE, 1.0},
    {"deepseekocr_relpos",     127,  1,   64, 63,  1, EXTRA_NONE, 1.0},

    {"tiny_1x1",        1,  1, 2,  1,  1, EXTRA_NONE,      0.0},
    {"zero_planes",     8,  8, 0,  6,  6, EXTRA_NONE,      0.0},
    {"zero_output",     5,  5, 3,  0,  4, EXTRA_NONE,      0.0},
    {"downsample_to_1", 8,  8, 2,  1,  1, EXTRA_NONE,      0.0},
    {"tail_odd_dims",   5,  5, 3, 13,  9, EXTRA_NONE,      0.0},
    {"extreme_values",  4,  4, 2,  7,  6, EXTRA_LARGE_MAG, 0.0},
};

typedef struct {
  const workload_t *w;
  float *src, *dst_ref, *dst_opt;
} ctx_t;

int kr_num_workloads(void) { return (int)(sizeof(W) / sizeof(W[0])); }
const char *kr_workload_name(int w) { return W[w].name; }
double kr_workload_weight(int w) { return W[w].weight; }

void *kr_setup(int wi, uint64_t seed) {
  const workload_t *w = &W[wi];
  ctx_t *c = malloc(sizeof *c);
  c->w = w;

  const size_t n_in = (size_t)(w->n_planes * w->ne00 * w->ne01);
  const size_t n_out = (size_t)(w->n_planes * w->ne0 * w->ne1);

  c->src = kr_alloc(n_in * sizeof(float));
  kr_fill_f32(c->src, n_in, -4.0f, 4.0f, &seed);
  if (w->extra == EXTRA_LARGE_MAG && n_in >= 4) {
    c->src[0] = 1e30f;
    c->src[1] = -1e30f;
    c->src[2] = 1e-30f;
    c->src[3] = -1e-30f;
  }

  c->dst_ref = kr_alloc(n_out * sizeof(float));
  c->dst_opt = kr_alloc(n_out * sizeof(float));
  return c;
}

void kr_run_ref(void *p) {
  ctx_t *c = p;
  const workload_t *w = c->w;
  upscale_bilinear_f32_ref(c->src, w->ne00, w->ne01, w->n_planes, c->dst_ref, w->ne0, w->ne1);
}

void kr_run_opt(void *p) {
  ctx_t *c = p;
  const workload_t *w = c->w;
  upscale_bilinear_f32_opt(c->src, w->ne00, w->ne01, w->n_planes, c->dst_opt, w->ne0, w->ne1);
}

int kr_outputs(void *p, kr_output *o, int max) {
  ctx_t *c = p;
  (void)max;
  const size_t count = (size_t)(c->w->n_planes * c->w->ne0 * c->w->ne1);
  /* A weighted sum of 4 terms computed in a different grouping (e.g. two
   * lerps instead of 4 products) can round differently in the last bit. */
  o[0] = (kr_output){"dst", c->dst_ref, c->dst_opt, count, KR_F32, 1e-5, 1e-5, 0};
  return 1;
}

void kr_teardown(void *p) {
  ctx_t *c = p;
  kr_free(c->src);
  kr_free(c->dst_ref);
  kr_free(c->dst_opt);
  free(c);
}
