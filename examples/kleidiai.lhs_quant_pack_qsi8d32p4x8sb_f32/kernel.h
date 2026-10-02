#ifndef TASK_KERNEL_H
#define TASK_KERNEL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Quantizes an M x K row-major f32 LHS matrix to per-block symmetric int8
 * (qsi8d32) and packs it into the "4x8sb" layout used by the
 * qsi8d32p4x8_qsi4c32p4x8 NEON/i8mm matmul micro-kernel.
 *
 * Contract (fixed by this packing format, ported from KleidiAI's
 * kai_lhs_quant_pack_qsi8d32p4x8sb_f32_neon):
 *   - mr = 4, kr = 16, sr = 2 are the only values this layout supports.
 *   - bl (quantization block length) must be a positive multiple of 32.
 *   - k (number of columns) must be a positive multiple of bl.
 *   - m (number of rows) may be any value >= 0 (0 is a no-op).
 *   - m_idx_start must be a multiple of mr (this is the m_step of the
 *     format: it says at which row index a caller may resume packing).
 *   - lhs points to the un-packed matrix; row m_idx_start + i starts at
 *     byte offset (m_idx_start + i) * lhs_stride from `lhs`.
 *   - lhs_packed must have at least
 *       ceil((m + m_idx_start % mr), mr) ... in practice
 *       kai_get_lhs_packed_size(m, k, bl, mr) bytes available from the
 *     start of the row group containing m_idx_start (see spec.c for the
 *     exact size formula), and must not alias lhs.
 *
 * Packed layout: rows are grouped by 4 (mr). Within a group, each
 * quantization block of bl columns is stored as:
 *   - 4 x f16 scale (one per row in the group, row-major),
 *   - bl x 4 int8 data bytes, laid out as bl/8 sub-chunks of 8 (kr/sr)
 *     values, each sub-chunk holding all 4 rows back to back.
 * Groups of 4 rows, and blocks within a group, are stored consecutively.
 */
void lhs_quant_pack_qsi8d32p4x8sb_f32_ref(
    size_t m, size_t k, size_t bl, size_t mr, size_t kr, size_t sr, size_t m_idx_start, const float* lhs,
    size_t lhs_stride, void* lhs_packed);

void lhs_quant_pack_qsi8d32p4x8sb_f32_opt(
    size_t m, size_t k, size_t bl, size_t mr, size_t kr, size_t sr, size_t m_idx_start, const float* lhs,
    size_t lhs_stride, void* lhs_packed);

#endif
