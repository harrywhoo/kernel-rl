/* Task adapter: workloads, inputs, calls and outputs. */
#include <stdint.h>
#include <stdlib.h>

#include "kernel.h"
#include "kr.h"

#define MR 4
#define KR 16
#define SR 2

typedef struct {
    size_t m;
    size_t k;
    size_t bl;
    size_t m_idx_start;
    int extreme;
} shape_t;

/*
 * Timed shapes mirror how the repo itself drives this kernel: quantizing the
 * LHS activations of an int4-weight matmul (block length 32 is the group
 * size used by the qsi4c32 RHS format), for typical LLM batch/hidden sizes,
 * plus the exact {77, 288} shape from the repo's own matmul test.
 */
static const struct {
    const char *name;
    shape_t shape;
    double weight;
} W[] = {
    {"prefill_m32_k4096_bl32", {32, 4096, 32, 0, 0}, 1.0},
    {"batch_m128_k4096_bl32", {128, 4096, 32, 0, 0}, 1.0},
    {"mlp_m8_k11008_bl32", {8, 11008, 32, 0, 0}, 1.0},
    {"repo_m77_k288_bl32", {77, 288, 32, 0, 0}, 0.5},
    {"wide_block_m32_k4096_bl128", {32, 4096, 128, 0, 0}, 0.5},

    {"m0", {0, 32, 32, 0, 0}, 0.0},
    {"m1_k32_bl32_tail", {1, 32, 32, 0, 0}, 0.0},
    {"m2_k64_bl32_tail", {2, 64, 32, 0, 0}, 0.0},
    {"m3_k32_bl32_tail", {3, 32, 32, 0, 0}, 0.0},
    {"m5_k64_bl32_tail", {5, 64, 32, 0, 0}, 0.0},
    {"m7_k96_bl32_tail", {7, 96, 32, 0, 0}, 0.0},
    {"m_idx_start_nonzero", {5, 64, 32, 8, 0}, 0.0},
    {"single_block_m4_k32_bl32", {4, 32, 32, 0, 0}, 0.0},
    {"extreme_values_m8_k64_bl32", {8, 64, 32, 0, 1}, 0.0},
};

typedef struct {
    shape_t shape;
    float *lhs;
    void *packed_ref;
    void *packed_opt;
    size_t packed_bytes;
} ctx_t;

static size_t packed_size_bytes(size_t m, size_t k, size_t bl) {
    const size_t num_groups = (m + MR - 1) / MR;
    const size_t num_blocks_per_row = k / bl;
    const size_t bytes_per_block = bl + sizeof(uint16_t);
    return num_groups * MR * num_blocks_per_row * bytes_per_block;
}

int kr_num_workloads(void) {
    return (int)(sizeof(W) / sizeof(W[0]));
}
const char *kr_workload_name(int w) {
    return W[w].name;
}
double kr_workload_weight(int w) {
    return W[w].weight;
}

void *kr_setup(int w, uint64_t seed) {
    ctx_t *c = malloc(sizeof *c);
    c->shape = W[w].shape;

    const size_t rows = c->shape.m_idx_start + c->shape.m;
    const size_t lhs_elems = rows * c->shape.k;
    c->lhs = kr_alloc(lhs_elems * sizeof(float));
    kr_fill_f32(c->lhs, lhs_elems, -4.0F, 4.0F, &seed);

    if (W[w].shape.extreme && c->shape.m > 0) {
        /* Force a wide dynamic range within each block: one huge value,
         * one tiny value, and a fully-zero row, so scales hit both ends. */
        const size_t row0 = c->shape.m_idx_start * c->shape.k;
        c->lhs[row0 + 0] = 3.0e38F;
        c->lhs[row0 + 1] = -3.0e38F;
        c->lhs[row0 + 2] = 1.0e-30F;
        if (c->shape.m > 1) {
            const size_t row1 = (c->shape.m_idx_start + 1) * c->shape.k;
            for (size_t i = 0; i < c->shape.k; i++) {
                c->lhs[row1 + i] = 0.0F;
            }
        }
    }

    c->packed_bytes = packed_size_bytes(c->shape.m, c->shape.k, c->shape.bl);
    c->packed_ref = kr_alloc(c->packed_bytes);
    c->packed_opt = kr_alloc(c->packed_bytes);
    return c;
}

void kr_run_ref(void *p) {
    ctx_t *c = p;
    lhs_quant_pack_qsi8d32p4x8sb_f32_ref(
        c->shape.m, c->shape.k, c->shape.bl, MR, KR, SR, c->shape.m_idx_start, c->lhs, c->shape.k * sizeof(float),
        c->packed_ref);
}

void kr_run_opt(void *p) {
    ctx_t *c = p;
    lhs_quant_pack_qsi8d32p4x8sb_f32_opt(
        c->shape.m, c->shape.k, c->shape.bl, MR, KR, SR, c->shape.m_idx_start, c->lhs, c->shape.k * sizeof(float),
        c->packed_opt);
}

int kr_outputs(void *p, kr_output *o, int max) {
    ctx_t *c = p;
    (void)max;
    o[0] = (kr_output){"lhs_packed", c->packed_ref, c->packed_opt, c->packed_bytes, KR_U8, 0, 0, 0};
    return 1;
}

void kr_teardown(void *p) {
    ctx_t *c = p;
    kr_free(c->lhs);
    kr_free(c->packed_ref);
    kr_free(c->packed_opt);
    free(c);
}
