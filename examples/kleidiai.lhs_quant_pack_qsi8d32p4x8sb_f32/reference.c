/*
 * Plain scalar port of KleidiAI's kai_lhs_quant_pack_qsi8d32p4x8sb_f32_neon
 * (kai/ukernels/matmul/pack/kai_lhs_quant_pack_qsi8d32p4x8sb_f32_neon.c),
 * combined with the per-block symmetric quantization from
 * test/reference/quantize.cpp (quantize_symmetric / round-to-nearest-even).
 * Apache-2.0, Arm Limited.
 */
#include "kernel.h"

#include <math.h>
#include <string.h>

static const size_t kai_num_bytes_multiplier = sizeof(uint16_t);

static uint16_t kai_cast_f16_f32(float value) {
    __fp16 half = (__fp16)value;
    uint16_t bits;
    memcpy(&bits, &half, sizeof(bits));
    return bits;
}

/* Round-to-nearest, ties-to-even, matching the NEON FCVTNS-based path. */
static int32_t round_to_nearest_even_i32(float value) {
    return (int32_t)lrintf(value);
}

void lhs_quant_pack_qsi8d32p4x8sb_f32_ref(
    size_t m, size_t k, size_t bl, size_t mr, size_t kr, size_t sr, size_t m_idx_start, const float* lhs,
    size_t lhs_stride, void* lhs_packed) {
    (void)mr;
    (void)kr;
    (void)sr;

    if (m == 0) {
        return;
    }

    const size_t local_mr = 4;
    const size_t k_block_len = 8; /* kr / sr = 16 / 2 */
    const size_t num_blocks_per_row = k / bl;
    const size_t num_bytes_per_block = bl * sizeof(int8_t) + kai_num_bytes_multiplier;
    const size_t lhs_packed_stride = local_mr * num_blocks_per_row * num_bytes_per_block;
    const size_t num_sub_chunks = bl / k_block_len;

    for (size_t row_idx = 0; row_idx < m; ++row_idx) {
        const size_t dst_x = (row_idx + m_idx_start) % local_mr;
        const size_t group = row_idx / local_mr;
        const float* src_row = (const float*)((const uint8_t*)lhs + (row_idx + m_idx_start) * lhs_stride);
        uint8_t* group_base = (uint8_t*)lhs_packed + group * lhs_packed_stride;

        for (size_t b = 0; b < num_blocks_per_row; ++b) {
            const float* block_src = src_row + b * bl;
            uint8_t* block_base = group_base + b * local_mr * num_bytes_per_block;

            float abs_max = 0.0F;
            for (size_t i = 0; i < bl; ++i) {
                const float v = fabsf(block_src[i]);
                if (v > abs_max) {
                    abs_max = v;
                }
            }

            const float scale = abs_max / ((1 << 7) - 1);
            const float rep_scale = scale != 0.0F ? 1.0F / scale : 0.0F;

            uint16_t* scale_ptr = (uint16_t*)(block_base + dst_x * kai_num_bytes_multiplier);
            *scale_ptr = kai_cast_f16_f32(scale);

            int8_t* data_base =
                (int8_t*)(block_base + local_mr * kai_num_bytes_multiplier + dst_x * k_block_len * sizeof(int8_t));

            for (size_t c = 0; c < num_sub_chunks; ++c) {
                int8_t* sub_chunk = data_base + c * local_mr * k_block_len;
                for (size_t e = 0; e < k_block_len; ++e) {
                    const float scaled = block_src[c * k_block_len + e] * rep_scale;
                    sub_chunk[e] = (int8_t)round_to_nearest_even_i32(scaled);
                }
            }
        }
    }
}
