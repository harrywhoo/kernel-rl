/*
 * driver.c — trusted benchmark driver. Linked with a task adapter (spec.c),
 * the reference implementation and the candidate implementation.
 *
 *   bench --mode check  [--seeds N]
 *   bench --mode time   [--trials T] [--min-batch-us U] [--rotate R]
 *   bench --mode all    (check, then time if every workload is correct)
 *   bench --mode probe-noop | probe-perturb   (harness self-tests; must FAIL)
 *
 * Prints a single JSON object on stdout.
 */
#define _GNU_SOURCE
#include "kr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

#define GUARD_BYTES 64
#define GUARD_BYTE 0xCD
#define POISON_BYTE 0xA5
#define MAX_ALLOCS 4096
#define MAX_OUTS 16
#define MAX_TRIALS 101
#define MAX_ROTATE 16

/* ------------------------------------------------------------------ */
/* allocation with guard zones                                         */
/* ------------------------------------------------------------------ */
typedef struct {
  uint8_t *base;
  size_t bytes;
} alloc_rec;

static alloc_rec g_allocs[MAX_ALLOCS];
static int g_nallocs;

void *kr_alloc(size_t bytes) {
  size_t total = bytes + 2 * GUARD_BYTES;
  total = (total + 63) & ~(size_t)63;
  uint8_t *base = NULL;
  if (posix_memalign((void **)&base, 64, total) != 0 || !base) {
    fprintf(stderr, "kr_alloc: out of memory\n");
    exit(3);
  }
  memset(base, GUARD_BYTE, total);
  memset(base + GUARD_BYTES, 0, bytes);
  if (g_nallocs == MAX_ALLOCS) {
    fprintf(stderr, "kr_alloc: too many allocations\n");
    exit(3);
  }
  g_allocs[g_nallocs].base = base;
  g_allocs[g_nallocs].bytes = bytes;
  g_nallocs++;
  return base + GUARD_BYTES;
}

void kr_free(void *p) {
  if (!p) return;
  uint8_t *base = (uint8_t *)p - GUARD_BYTES;
  for (int i = 0; i < g_nallocs; i++) {
    if (g_allocs[i].base == base) {
      g_allocs[i] = g_allocs[--g_nallocs];
      free(base);
      return;
    }
  }
  fprintf(stderr, "kr_free: unknown pointer\n");
  exit(3);
}

/* Returns the number of corrupted guard zones. */
static int check_guards(void) {
  int bad = 0;
  for (int i = 0; i < g_nallocs; i++) {
    uint8_t *b = g_allocs[i].base;
    size_t n = g_allocs[i].bytes;
    for (size_t k = 0; k < GUARD_BYTES; k++) {
      if (b[k] != GUARD_BYTE || b[GUARD_BYTES + n + k] != GUARD_BYTE) {
        bad++;
        break;
      }
    }
  }
  return bad;
}

/* ------------------------------------------------------------------ */
/* deterministic random fill                                           */
/* ------------------------------------------------------------------ */
uint64_t kr_rand_u64(uint64_t *s) {
  uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

void kr_fill_u8(uint8_t *p, size_t n, uint64_t *s) {
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)kr_rand_u64(s);
}

void kr_fill_u16(uint16_t *p, size_t n, uint16_t max_inclusive, uint64_t *s) {
  for (size_t i = 0; i < n; i++)
    p[i] = (uint16_t)(kr_rand_u64(s) % ((uint64_t)max_inclusive + 1));
}

void kr_fill_i16(int16_t *p, size_t n, int16_t lo, int16_t hi, uint64_t *s) {
  uint64_t span = (uint64_t)((int64_t)hi - lo + 1);
  for (size_t i = 0; i < n; i++) p[i] = (int16_t)(lo + (int64_t)(kr_rand_u64(s) % span));
}

void kr_fill_i32(int32_t *p, size_t n, int32_t lo, int32_t hi, uint64_t *s) {
  uint64_t span = (uint64_t)((int64_t)hi - lo + 1);
  for (size_t i = 0; i < n; i++) p[i] = (int32_t)(lo + (int64_t)(kr_rand_u64(s) % span));
}

void kr_fill_f32(float *p, size_t n, float lo, float hi, uint64_t *s) {
  for (size_t i = 0; i < n; i++) {
    double u = (double)(kr_rand_u64(s) >> 11) * (1.0 / 9007199254740992.0);
    p[i] = (float)(lo + (hi - lo) * u);
  }
}

/* ------------------------------------------------------------------ */
/* comparison                                                          */
/* ------------------------------------------------------------------ */
static size_t dtype_size(kr_dtype t) {
  switch (t) {
    case KR_U8: case KR_I8: return 1;
    case KR_U16: case KR_I16: return 2;
    case KR_U32: case KR_I32: case KR_F32: return 4;
    default: return 8;
  }
}

static double load_elem(const void *p, size_t i, kr_dtype t) {
  switch (t) {
    case KR_U8: return ((const uint8_t *)p)[i];
    case KR_I8: return ((const int8_t *)p)[i];
    case KR_U16: return ((const uint16_t *)p)[i];
    case KR_I16: return ((const int16_t *)p)[i];
    case KR_U32: return ((const uint32_t *)p)[i];
    case KR_I32: return ((const int32_t *)p)[i];
    case KR_U64: return (double)((const uint64_t *)p)[i];
    case KR_I64: return (double)((const int64_t *)p)[i];
    case KR_F32: return ((const float *)p)[i];
    case KR_F64: return ((const double *)p)[i];
  }
  return 0;
}

static void store_elem(void *p, size_t i, kr_dtype t, double v) {
  switch (t) {
    case KR_U8: ((uint8_t *)p)[i] = (uint8_t)(int64_t)v; break;
    case KR_I8: ((int8_t *)p)[i] = (int8_t)(int64_t)v; break;
    case KR_U16: ((uint16_t *)p)[i] = (uint16_t)(int64_t)v; break;
    case KR_I16: ((int16_t *)p)[i] = (int16_t)(int64_t)v; break;
    case KR_U32: ((uint32_t *)p)[i] = (uint32_t)(int64_t)v; break;
    case KR_I32: ((int32_t *)p)[i] = (int32_t)(int64_t)v; break;
    case KR_U64: ((uint64_t *)p)[i] = (uint64_t)v; break;
    case KR_I64: ((int64_t *)p)[i] = (int64_t)v; break;
    case KR_F32: ((float *)p)[i] = (float)v; break;
    case KR_F64: ((double *)p)[i] = v; break;
  }
}

typedef struct {
  int pass;
  size_t mismatches;
  long first_bad;
  double max_abs_err;
} cmp_result;

static cmp_result compare_output(const kr_output *o) {
  cmp_result r = {1, 0, -1, 0.0};
  for (size_t i = 0; i < o->count; i++) {
    double a = load_elem(o->ref, i, o->dtype);
    double b = load_elem(o->opt, i, o->dtype);
    if (isnan(a) || isnan(b)) {
      if (isnan(a) && isnan(b)) continue;
      r.max_abs_err = INFINITY;
    } else {
      double err = fabs(a - b);
      if (err > r.max_abs_err) r.max_abs_err = err;
      if (err <= o->atol + o->rtol * fabs(a)) continue;
    }
    r.pass = 0;
    r.mismatches++;
    if (r.first_bad < 0) r.first_bad = (long)i;
  }
  return r;
}

/* ------------------------------------------------------------------ */
/* timing                                                              */
/* ------------------------------------------------------------------ */
static uint64_t now_ns(void) {
#ifdef __APPLE__
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

typedef void (*run_fn)(void *);

static double batch_ns(run_fn f, void **ctxs, int nctx, long iters) {
  uint64_t t0 = now_ns();
  for (long i = 0; i < iters; i++) f(ctxs[i % nctx]);
  return (double)(now_ns() - t0);
}

static long calibrate(run_fn f, void **ctxs, int nctx, double min_ns) {
  long iters = 1;
  for (;;) {
    double t = batch_ns(f, ctxs, nctx, iters);
    if (t >= min_ns || iters > (1L << 30)) break;
    long next = t > 0 ? (long)(iters * (min_ns / t) * 1.2) + 1 : iters * 10;
    if (next <= iters) next = iters * 2;
    iters = next;
  }
  return iters;
}

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

typedef struct {
  double median, min, cv;
} stats;

static stats summarize(double *v, int n) {
  stats s;
  double sum = 0, sq = 0;
  for (int i = 0; i < n; i++) sum += v[i];
  double mean = sum / n;
  for (int i = 0; i < n; i++) sq += (v[i] - mean) * (v[i] - mean);
  qsort(v, n, sizeof(double), cmp_double);
  s.median = (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
  s.min = v[0];
  s.cv = mean > 0 ? sqrt(sq / n) / mean : 0;
  return s;
}

/* ------------------------------------------------------------------ */
/* JSON helpers                                                        */
/* ------------------------------------------------------------------ */
static void json_str(const char *s) {
  putchar('"');
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') putchar('\\');
    putchar(*s);
  }
  putchar('"');
}

static void json_num(double v) {
  if (isfinite(v)) printf("%.6g", v);
  else printf("null");
}

/* ------------------------------------------------------------------ */
/* modes                                                               */
/* ------------------------------------------------------------------ */
enum { PROBE_NONE, PROBE_NOOP, PROBE_PERTURB };

static void poison(kr_output *outs, int n, int opt_side) {
  for (int k = 0; k < n; k++) {
    if (outs[k].inplace) continue;
    memset(opt_side ? outs[k].opt : outs[k].ref, POISON_BYTE,
           outs[k].count * dtype_size(outs[k].dtype));
  }
}

static void perturb(kr_output *o) {
  size_t i = o->count / 2;
  double v = load_elem(o->opt, i, o->dtype);
  double delta = 10.0 * (o->atol + o->rtol * fabs(v)) + 1.0;
  if (o->dtype == KR_F32 || o->dtype == KR_F64) {
    store_elem(o->opt, i, o->dtype, v + delta);
  } else {
    /* integer: move away from the reference by more than atol, wrapping safely */
    double lo = 0, hi = 255;
    switch (o->dtype) {
      case KR_I8: lo = -128; hi = 127; break;
      case KR_U16: hi = 65535; break;
      case KR_I16: lo = -32768; hi = 32767; break;
      case KR_U32: hi = 4294967295.0; break;
      case KR_I32: lo = -2147483648.0; hi = 2147483647.0; break;
      case KR_U64: case KR_I64: lo = -1e18; hi = 1e18; break;
      default: break;
    }
    double nv = (v + delta <= hi) ? v + delta : v - delta;
    if (nv < lo) nv = lo;
    store_elem(o->opt, i, o->dtype, floor(nv));
  }
}

/* Returns 1 if every workload passed. */
static int run_check(int seeds, int probe) {
  int all_pass = 1;
  int nw = kr_num_workloads();
  printf("\"check\":[");
  for (int w = 0; w < nw; w++) {
    int wpass = 1, guard_bad = 0;
    double max_err = 0;
    size_t mism = 0;
    const char *bad_out = "";
    long first_bad = -1;
    int bad_seed = -1;
    for (int s = 0; s < seeds; s++) {
      uint64_t seed = 0x5EED0000ull + (uint64_t)w * 1000 + (uint64_t)s;
      void *ctx = kr_setup(w, seed);
      kr_output outs[MAX_OUTS];
      int no = kr_outputs(ctx, outs, MAX_OUTS);
      poison(outs, no, 0);
      kr_run_ref(ctx);
      poison(outs, no, 1);
      if (probe != PROBE_NOOP) kr_run_opt(ctx);
      if (probe == PROBE_PERTURB && no > 0) perturb(&outs[0]);
      guard_bad += check_guards();
      for (int k = 0; k < no; k++) {
        cmp_result r = compare_output(&outs[k]);
        if (r.max_abs_err > max_err) max_err = r.max_abs_err;
        if (!r.pass) {
          if (wpass) {
            bad_out = outs[k].name;
            first_bad = r.first_bad;
            bad_seed = s;
          }
          wpass = 0;
          mism += r.mismatches;
        }
      }
      kr_teardown(ctx);
    }
    if (guard_bad) wpass = 0;
    if (!wpass) all_pass = 0;
    printf("%s{\"workload\":", w ? "," : "");
    json_str(kr_workload_name(w));
    printf(",\"pass\":%s,\"max_abs_err\":", wpass ? "true" : "false");
    json_num(max_err);
    printf(",\"mismatches\":%zu,\"guard_violations\":%d", mism, guard_bad);
    if (!wpass && first_bad >= 0) {
      printf(",\"first_bad\":{\"output\":");
      json_str(bad_out);
      printf(",\"index\":%ld,\"seed_index\":%d}", first_bad, bad_seed);
    }
    printf("}");
  }
  printf("]");
  return all_pass;
}

/* Spin so the core leaves its idle frequency state before anything is timed. */
static void spin_warmup(double ms) {
  volatile uint64_t x = 0;
  uint64_t end = now_ns() + (uint64_t)(ms * 1e6);
  while (now_ns() < end)
    for (int i = 0; i < 1000; i++) x += i;
}

static void run_time(int trials, double min_batch_us, int rotate) {
  int nw = kr_num_workloads();
  spin_warmup(300);
  double log_sum = 0, wsum = 0;
  printf("\"time\":[");
  int first = 1;
  for (int w = 0; w < nw; w++) {
    double wt = kr_workload_weight(w);
    if (wt <= 0) continue; /* correctness-only workload */
    void *ctxs[MAX_ROTATE];
    for (int r = 0; r < rotate; r++) ctxs[r] = kr_setup(w, 0x71AE0000ull + (uint64_t)w * 1000 + r);
    /* warm up both implementations on every input set */
    batch_ns(kr_run_ref, ctxs, rotate, rotate * 4L);
    batch_ns(kr_run_opt, ctxs, rotate, rotate * 4L);
    long n_ref = calibrate(kr_run_ref, ctxs, rotate, min_batch_us * 1e3);
    long n_opt = calibrate(kr_run_opt, ctxs, rotate, min_batch_us * 1e3);
    double tr[MAX_TRIALS], to[MAX_TRIALS];
    for (int t = 0; t < trials; t++) {
      /* interleave, alternating order, to cancel slow drift */
      if (t & 1) {
        to[t] = batch_ns(kr_run_opt, ctxs, rotate, n_opt) / n_opt;
        tr[t] = batch_ns(kr_run_ref, ctxs, rotate, n_ref) / n_ref;
      } else {
        tr[t] = batch_ns(kr_run_ref, ctxs, rotate, n_ref) / n_ref;
        to[t] = batch_ns(kr_run_opt, ctxs, rotate, n_opt) / n_opt;
      }
    }
    stats sr = summarize(tr, trials), so = summarize(to, trials);
    double speedup = so.median > 0 ? sr.median / so.median : 0;
    if (speedup > 0) {
      log_sum += wt * log(speedup);
      wsum += wt;
    }
    for (int r = 0; r < rotate; r++) kr_teardown(ctxs[r]);
    printf("%s{\"workload\":", first ? "" : ",");
    first = 0;
    json_str(kr_workload_name(w));
    printf(",\"weight\":");
    json_num(wt);
    printf(",\"ref_ns\":");
    json_num(sr.median);
    printf(",\"opt_ns\":");
    json_num(so.median);
    printf(",\"ref_cv\":");
    json_num(sr.cv);
    printf(",\"opt_cv\":");
    json_num(so.cv);
    printf(",\"speedup\":");
    json_num(speedup);
    printf(",\"iters_ref\":%ld,\"iters_opt\":%ld}", n_ref, n_opt);
  }
  printf("],\"geomean_speedup\":");
  json_num(wsum > 0 ? exp(log_sum / wsum) : 0);
}

int main(int argc, char **argv) {
  const char *mode = "all";
  int seeds = 3, trials = 15, rotate = 4;
  double min_batch_us = 2000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
    else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--rotate") && i + 1 < argc) rotate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--min-batch-us") && i + 1 < argc) min_batch_us = atof(argv[++i]);
    else {
      fprintf(stderr, "usage: %s [--mode check|time|all|probe-noop|probe-perturb] "
                      "[--seeds N] [--trials T] [--rotate R] [--min-batch-us U]\n", argv[0]);
      return 2;
    }
  }
  if (trials < 3) trials = 3;
  if (trials > MAX_TRIALS) trials = MAX_TRIALS;
  if (rotate < 1) rotate = 1;
  if (rotate > MAX_ROTATE) rotate = MAX_ROTATE;
#ifdef __APPLE__
  /* ask for a performance core; macOS has no hard affinity */
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

  printf("{\"mode\":");
  json_str(mode);
  printf(",");
  int ok = 1;
  if (!strcmp(mode, "check") || !strcmp(mode, "all")) {
    ok = run_check(seeds, PROBE_NONE);
    printf(",\"correct\":%s", ok ? "true" : "false");
    if (!strcmp(mode, "all") && ok) {
      printf(",");
      run_time(trials, min_batch_us, rotate);
    }
  } else if (!strcmp(mode, "time")) {
    run_time(trials, min_batch_us, rotate);
  } else if (!strcmp(mode, "probe-noop") || !strcmp(mode, "probe-perturb")) {
    ok = run_check(seeds, !strcmp(mode, "probe-noop") ? PROBE_NOOP : PROBE_PERTURB);
    printf(",\"correct\":%s", ok ? "true" : "false");
  } else {
    printf("\"error\":\"unknown mode\"");
  }
  printf("}\n");
  return 0;
}
