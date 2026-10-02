# ARGBAttenuateRow (libyuv)

Make `ARGBAttenuateRow_opt` in `solution.c` as fast as possible on this
AArch64 CPU while producing **bit-identical** output to `ARGBAttenuateRow_ref`
(`reference.c`) for every `width >= 0`.

- Contract: see `kernel.h`.
- Only `solution.c` is graded; every other file is restored before grading.
- You may use NEON intrinsics (`<arm_neon.h>`) or inline assembly. Do not call
  the reference or any library other than libc.
- Score: geometric-mean speedup over the reference compiled with `-O3`, across
  the timed workloads in `spec.c`. Incorrect output on any workload scores 0.

From this task directory: `python -m kernelrl eval . --brief`.
In a solver session, use the provided `./run.sh`.
