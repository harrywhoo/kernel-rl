/* Task adapter: workloads, inputs, calls and outputs. */
#include <stdlib.h>
#include "kr.h"
#include "kernel.h"

typedef struct {
  int width;
  uint8_t *src, *dst_ref, *dst_opt;
} ctx_t;

/* Row widths seen when attenuating 720p/1080p/4K frames, plus tails and tiny rows. */
static const struct { const char *name; int width; double weight; } W[] = {
    {"w1280", 1280, 1.0}, {"w1920", 1920, 1.0}, {"w3840", 3840, 1.0},
    {"w1923_tail", 1923, 0.5},
    {"w1", 1, 0.0}, {"w7", 7, 0.0}, {"w9", 9, 0.0}, {"w0", 0, 0.0},
};

int kr_num_workloads(void) { return (int)(sizeof(W) / sizeof(W[0])); }
const char *kr_workload_name(int w) { return W[w].name; }
double kr_workload_weight(int w) { return W[w].weight; }

void *kr_setup(int w, uint64_t seed) {
  ctx_t *c = malloc(sizeof *c);
  c->width = W[w].width;
  size_t bytes = (size_t)c->width * 4;
  c->src = kr_alloc(bytes);
  c->dst_ref = kr_alloc(bytes);
  c->dst_opt = kr_alloc(bytes);
  kr_fill_u8(c->src, bytes, &seed);
  /* make sure the alpha extremes 0 and 255 always appear */
  if (c->width > 0) c->src[3] = 0;
  if (c->width > 1) c->src[7] = 255;
  return c;
}

void kr_run_ref(void *p) { ctx_t *c = p; ARGBAttenuateRow_ref(c->src, c->dst_ref, c->width); }
void kr_run_opt(void *p) { ctx_t *c = p; ARGBAttenuateRow_opt(c->src, c->dst_opt, c->width); }

int kr_outputs(void *p, kr_output *o, int max) {
  ctx_t *c = p;
  (void)max;
  o[0] = (kr_output){"dst_argb", c->dst_ref, c->dst_opt, (size_t)c->width * 4, KR_U8, 0, 0, 0};
  return 1;
}

void kr_teardown(void *p) {
  ctx_t *c = p;
  kr_free(c->src);
  kr_free(c->dst_ref);
  kr_free(c->dst_opt);
  free(c);
}
