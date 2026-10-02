/*
 * kr.h — contract between the trusted benchmark driver (driver.c) and a
 * task-specific adapter (spec.c).
 *
 * A task adapter describes *what* to run: workloads, how to build inputs for
 * a given seed, how to call the reference and the candidate, and which buffers
 * are outputs. The driver owns *how* it is measured: poisoning outputs,
 * comparing, guard-zone checks, rotation across input sets, and timing.
 *
 * Adapters must allocate every buffer that a kernel reads or writes with
 * kr_alloc(), so the driver can detect out-of-bounds writes.
 */
#ifndef KR_H
#define KR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  KR_U8, KR_I8, KR_U16, KR_I16, KR_U32, KR_I32, KR_U64, KR_I64, KR_F32, KR_F64
} kr_dtype;

typedef struct {
  const char *name;
  void *ref;      /* buffer written by the reference */
  void *opt;      /* buffer written by the candidate */
  size_t count;   /* number of elements to compare */
  kr_dtype dtype;
  double atol;    /* |opt - ref| <= atol + rtol * |ref| */
  double rtol;
  int inplace;    /* 1 if the buffer is also an input: the driver must not poison it */
} kr_output;

/* ---- implemented by the task adapter (spec.c) ---- */
int kr_num_workloads(void);
const char *kr_workload_name(int w);
double kr_workload_weight(int w);          /* relative weight in the aggregate score */
void *kr_setup(int w, uint64_t seed);      /* allocate and fill one input set */
void kr_run_ref(void *ctx);
void kr_run_opt(void *ctx);
int kr_outputs(void *ctx, kr_output *outs, int max_outs);
void kr_teardown(void *ctx);

/* ---- provided by the driver ---- */
void *kr_alloc(size_t bytes);              /* 64B-aligned, guard-zoned, zero-filled */
void kr_free(void *p);
uint64_t kr_rand_u64(uint64_t *state);     /* splitmix64 */
void kr_fill_u8(uint8_t *p, size_t n, uint64_t *state);
void kr_fill_u16(uint16_t *p, size_t n, uint16_t max_inclusive, uint64_t *state);
void kr_fill_i16(int16_t *p, size_t n, int16_t lo, int16_t hi, uint64_t *state);
void kr_fill_i32(int32_t *p, size_t n, int32_t lo, int32_t hi, uint64_t *state);
void kr_fill_f32(float *p, size_t n, float lo, float hi, uint64_t *state);

#ifdef __cplusplus
}
#endif
#endif /* KR_H */
