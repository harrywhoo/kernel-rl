# How it works

## Pipeline

Candidate → extraction → audit → optimization → grade.

1. **Find a candidate.** `kernelrl/mine.py` has two simple finders:
   - `translation_pairs`: functions that exist as a plain C version and a hand-written
     SIMD version with the same base name (`Foo_C` / `Foo_NEON`). Works for libyuv and libwebp.
   - `perf_commits`: a keyword filter over git history for kernel optimization commits.

   For the KleidiAI and llama.cpp runs, candidates were picked by hand from what these finders and
   a quick read of the repos turned up (the candidate list is local, not included in this checkout).
2. **Extraction agent.** A headless Claude Code session (Sonnet 5) reads the repo (the kernel, its
   tests and its callers) and writes a standalone task. Instructions: `kernelrl/prompts/extract.md` plus one
   of `prompts/sources/{translation,commit,current}.md`. It runs in a scratch directory outside this repo.
3. **Audit.** `kernelrl/audit.py` checks every task automatically (below). If the audit fails, the agent gets one
   repair round with the audit report, or it can reject the candidate.
4. **Baseline optimization agent.** See [Baseline agents](#baseline-optimization-agents).

## Task format

| File | Role | Solver sees it? |
|---|---|---|
| `reference.c` | known-correct original implementation | yes |
| `solution.c` | starting point; the only file that's graded | yes (edits it) |
| `gold.c` | existing human-optimized version, when one exists | **no** |
| `spec.c` | inputs: timed workloads + correctness-only edge cases | yes |
| `kernel.h` | function signatures and the contract | yes |
| `task.toml` | provenance (repo, commit, files), compiler flags, contract notes | yes |
| `README.md` | the instruction given to the solver | yes |

See `examples/` for four complete tasks.

## How a candidate is measured

`kernelrl/harness/driver.c` is linked with `spec.c`, `reference.c` and the candidate. Grading always
rebuilds from the original task files plus only the candidate's `solution.c`.

- **Correctness:** every workload (timed and edge cases) × 3 random seeds. Outputs are compared with
  the reference: using the tolerances in `spec.c` (zero for the included integer outputs). Output buffers are
  pre-filled with garbage, and allocations have guard bytes, to catch skipped writes and writes into the guard regions.
  The candidate can't call the reference (checked on the object file's symbols).
- **Speed:** for each timed workload, reference and candidate are timed in alternating batches
  (15 trials, ≥2 ms per batch, cycling through 4 different input sets to reduce reuse of a single input).
  Per-workload speedup = median reference time ÷ median candidate time.
  **Task speedup = weighted geometric mean over the timed workloads**. Weights are set in `spec.c`, usually 0.5–1.0.
- The baseline is the reference compiled with `-O3`, so the compiler's own auto-vectorization is already included.

## Where inputs come from

The extraction agent picks workloads by reading how the repo calls the kernel (callers, tests, model code),
and documents its reasoning in a comment in `spec.c`. Examples:
- `llama.upscale_bilinear_f32`: shapes from the two real callers in `tools/mtmd` (position-embedding resize, relative-position tables).
- `kleidiai.lhs_quant_pack_qsi8d32p4x8sb_f32`: LLM-style batch/hidden sizes, plus the exact `{77, 288}` shape from the repo's test.

Input values are seeded random, with edge values (0, max, negative) forced in. Shapes are informed by the code
and the agent's knowledge of common model sizes, **not** traced from a real run. Across 15 tasks:
**2–6 timed workloads (median 4)** and **4–11 correctness-only cases (median 6)**.

## Audit checks

`audit` writes `audit.json`. Verdicts can change between runs because timing is noisy.

| Check | Fails when |
|---|---|
| identity | the untouched `solution.c` is incorrect (warn if speedup is outside 0.85–1.15×) |
| no-op / corrupted-output probes | a candidate that skips work or corrupts one value is accepted |
| human version | `gold.c` (if present) disagrees with the reference |
| noise | timing spread (coefficient of variation) > 15% (warn > 5%) |
| coverage | no timed workloads (warn if < 2 timed or no edge cases) |

Verdict: FAIL (investigate or repair), WARN (inspect the reported caveats), PASS.
These checks screen the generated tasks; they do not establish correctness for all inputs
or provide a security sandbox for candidate code.

## Baseline optimization agents

- **Harness:** Claude Code in headless mode (`claude -p`), one session per task, in a scratch directory
  outside this repo. The prompt is `kernelrl/prompts/solve.md` plus the task's README.
- **Tools:** read/write/edit files in the workspace, and `./run.sh`, which builds `solution.c` and
  reports correctness and per-workload speedup. `./run.sh` is the only allowlisted shell command, though
  Claude Code also auto-approves read-only commands such as `ls` and `| head` (seen in transcripts).
  Web tools are disabled and `gold.c` is not copied into the workspace.
- **Budget:** 30 agent turns (`--max-turns 30`), one attempt per task.
- **Grading:** the final `solution.c` when the session ends (not the best version seen during the session).
- **Models:** Claude Haiku 4.5 so far. Stronger (Sonnet 5, Opus 5.5) runs use the same command with `--model`.
- **Transcripts** are kept in `runs/baselines/<task>__<model>__s0/transcript.jsonl` (local, not in git).

The solver is instructed to edit only `solution.c`; file tools can still edit the
other workspace files. Final grading uses the original task files. A scratch
directory avoids loading this repo’s instructions, but is not OS-level isolation.
