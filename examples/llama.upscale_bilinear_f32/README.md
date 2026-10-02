# upscale_bilinear_f32 (llama.cpp / ggml)

Make `upscale_bilinear_f32_opt` in `solution.c` as fast as possible on this
AArch64 CPU (NEON, dotprod, i8mm, bf16, SME2; no SVE) while matching
`upscale_bilinear_f32_ref` (`reference.c`) for every input the contract
allows.

- Contract: see `kernel.h`. `src` is a stack of contiguous [ne01][ne00] F32
  planes; `dst` is a stack of contiguous [ne1][ne0] F32 planes, one output
  plane per input plane. Each output pixel is a bilinear blend of 4 source
  taps (half-pixel-center convention, edge-clamped, no align-corners).
- Only `solution.c` is graded; every other file is restored before grading.
- You may use NEON intrinsics (`<arm_neon.h>`) or inline assembly. Do not call
  the reference or any library other than libc/libm.
- Matching is within atol=rtol=1e-5 (a vectorized reassociation of the 4-term
  weighted sum can round differently in the last bit or two; see `task.toml`
  for details). Incorrect output on any workload scores 0.
- Score: geometric-mean speedup over the reference compiled with `-O3`,
  across the timed workloads in `spec.c`.

From this task directory: `python -m kernelrl eval . --brief`.
In a solver session, use the provided `./run.sh`.
