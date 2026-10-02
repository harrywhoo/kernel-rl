You are optimizing a CPU kernel. The task description follows.

{readme}

## Workspace
- `solution.c` is the only file that is graded. Every other file is restored before grading.
- `kernel.h`, `reference.c` and `spec.c` define the contract, the reference and the benchmarked workloads.
- `./run.sh` builds your `solution.c` against the trusted harness. It reports
  per-workload correctness and speedup against the reference (JSON). Run it often.
- Machine: {cpu}, AArch64. Compiler flags: `{cflags}`.

Work until you cannot improve the speedup further or you run out of turns. Keep
`solution.c` correct at all times. End with one line: the final geomean speedup reported by `./run.sh`.
