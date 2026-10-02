# lhs_quant_pack_qsi8d32p4x8sb_f32 (KleidiAI)

Make `lhs_quant_pack_qsi8d32p4x8sb_f32_opt` in `solution.c` as fast as possible
on this AArch64 CPU (Apple M4 Pro: NEON, dotprod, i8mm, bf16, SME2 — no SVE)
while producing **bit-identical** output to `lhs_quant_pack_qsi8d32p4x8sb_f32_ref`
(`reference.c`) for every input the contract allows.

The kernel quantizes an M x K row-major f32 matrix to per-block symmetric
int8 (one scale per row per 32-or-more-wide block) and packs 4 rows at a
time into the interleaved layout consumed by KleidiAI's
`qsi8d32p4x8_qsi4c32p4x8` int4-weight matmul micro-kernels. This is the LHS
(activation) quantize+pack step of an int8-activation / int4-weight matmul,
run once per token batch before every matmul.

- Contract: see `kernel.h`.
- Only `solution.c` is graded; every other file is restored before grading.
- You may use NEON/i8mm/SME2 intrinsics (`<arm_neon.h>`) or inline assembly.
  Do not call the reference or any library other than libc/libm.
- Score: geometric-mean speedup over the reference compiled with `-O3`,
  across the timed workloads in `spec.c`. Incorrect output on any workload
  (timed or correctness-only) scores 0.

From this task directory: `python -m kernelrl eval . --brief`.
In a solver session, use the provided `./run.sh`.
