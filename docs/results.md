# Results so far (2026-10-02)

All numbers are from one Apple M4 Pro laptop. Speedups are over the reference compiled with `-O3`.

## 1. Extraction

15 tasks from 4 repos: one hand-built example and 14 extracted with Sonnet 5.
Re-audited one at a time on an idle machine on 2026-10-02.
Four tasks and their audit reports are included in `examples/`; the remaining
tasks, extraction logs and full audit log are local artifacts, not included here.
Timing spread is the maximum reference/candidate coefficient of variation across workloads.

| Task | Kind | Audit | Human version speedup | Timing spread | Extraction cost |
|---|---|---|---|---|---|
| libyuv.ARGBAttenuateRow | translation (hand-written) | PASS | 5.08× | 2.8% | – |
| libyuv.I422ToARGBRow | translation | WARN (noise) | 2.69× | 9.1% | ~$1.2 |
| libyuv.ScaleRowDown2Box | translation | PASS | 1.84× | 1.6% | ~$0.6 |
| libyuv.ARGBGrayRow | translation | PASS | 1.27× | 2.3% | ~$0.9 |
| libyuv.HalfFloatRow | translation | WARN (no headroom) | 0.99× | 2.2% | ~$0.8 |
| libwebp.SSE16x16 | translation | WARN (no headroom) | 1.02× | 3.3% | ~$0.6 |
| libwebp.CollectHistogram | translation | WARN (no headroom) | 0.93× | 2.9% | ~$1.1 |
| libwebp.AlphaReplace | translation (no ARM version) | FAIL (noise) | – | 15.1% | ~$0.6 |
| kleidiai.lhs_quant_pack_qai8dxp_bf16 | translation | PASS | 3.43× | 1.9% | ~$2.2 |
| kleidiai.lhs_quant_pack_qsi8d32p4x8sb_f32 | translation | PASS | 6.04× | 1.6% | ~$2.1 |
| kleidiai.rhs_pack_nxk_qsi4c32pnrx8_qsu4c32s1s0 | translation | PASS | 6.53× | 2.1% | ~$3.7 |
| llama.upscale_bilinear_f32 | current code | WARN (no human version) | – | 1.5% | ~$1.2 |
| llama.im2col_f32 | current code | WARN (noise, no human version) | – | 5.3% | ~$2.0 |
| llama.timestep_embedding_f32 | current code | WARN (no human version) | – | 2.8% | ~$1.0 |
| llama.pool_2d | current code | WARN (noise, no human version) | – | 13.7% | ~$1.6 |

Costs are Claude Code's API-equivalent estimates (the runs used a subscription).

Observations
- 14 of 15 tasks avoided FAIL (6 PASS, 8 WARN). The agent sliced out dependencies and reproduced edge-case handling
  (e.g. libyuv's tail wrappers) on its own. KleidiAI tasks cost about 2× more to build.
- **A human-optimized version doesn't guarantee headroom.** 3 of 7 libyuv/libwebp human SIMD versions are
  no faster than the compiler's `-O3` output. KleidiAI's hand-tuned kernels are 3.4–6.5× faster.
- **Timing noise isn't stable per task.** I422ToARGBRow measured 1.3%, 9.1% and 1.8% spread in three audits (WARN in one, PASS in the others).
  A single noise measurement isn't enough to decide whether a task's reward is reliable.
- Agents write task metadata inconsistently (field names drift between tasks), so `task.toml` needs a stricter schema.

## 2. Baseline optimization agents

(See [how-it-works.md](how-it-works.md#baseline-optimization-agents) for the setup.)

Claude Haiku 4.5, one attempt per task, 30-turn cap, isolated workspace (2026-10-02). Run on 5 of the 8 planned
tasks; Sonnet 5 and Opus 5.5 were not run this round. "Best seen" is the best correct speedup `./run.sh` reported
during the session; the grade uses the final `solution.c`.

| Task | Human version | Haiku final | Haiku best seen |
|---|---|---|---|
| kleidiai.lhs_quant_pack_qai8dxp_bf16 | 3.43× | 2.57× | 2.56× |
| libyuv.ScaleRowDown2Box | 1.84× | 1.70× | 1.70× |
| libyuv.I422ToARGBRow | 2.69× | 1.15× | 1.15× |
| libyuv.ARGBGrayRow | 1.27× | incorrect (0) | 1.48× |
| libyuv.ARGBAttenuateRow | 5.08× | incorrect (0) | 1.05× |

- Every run used the full turn budget (about 31 turns, ~10 `./run.sh` calls); total cost ~$2.
- 3 of 5 final solutions are correct and faster than the reference. None matched the human version.
- **Two runs ended on a broken edit.** On ARGBGrayRow the agent reached 1.48×, beating the human version's 1.27×,
  then broke correctness on its last edit. Grading the best version seen instead of the final one changes the result.
- An earlier Haiku run on the libyuv/libwebp pilot (agents inside this repo, now replaced) was correct on 8/8 tasks
  but faster on only 1.

`python experiments/summarize_baselines.py runs/baselines` regenerates this table from the run directories.

## 3. Can human-optimized implementations be mined at scale?

Partly.
- **Name-based pairing works within one convention.** `mine.translation_pairs` automatically finds 231 candidates
  in libyuv (208 with an ARM version) and 145 in libwebp. It finds 0 in KleidiAI, which names its variants differently.
- **Optimization commits are rare.** In recent history, about 6 of ~380 llama.cpp CPU-backend commits and 1 of 300
  KleidiAI commits are real, runnable ARM optimizations, and one llama.cpp optimization was later reverted.
- **The filters we tried excluded kernel optimizations.** Repo2RLEnv's commit filters kept 0 of 216 performance-related commits from
  llama.cpp, simdjson and libwebp (they require bug fixes with new tests). GSO's classifier prompt excludes hardware-specific changes.
  (`experiments/`)
- **What a human version exists for isn't the same as where there's headroom** (section 1). Mined pairs need a measured speedup filter.
- **Next:** an agent that reads a repo's dispatch code (function-pointer tables, `#ifdef __ARM_NEON` blocks) to find
  implementation families across naming conventions, then keeps only pairs with measured headroom.

## 4. Limitations
- One machine, one sample per task, one model (Haiku 4.5) so far. Shapes are chosen by the agent from reading code, not traced from real workloads.
- Grading uses the agent's final `solution.c`, not its best intermediate version.
- Earlier runs (before 2026-10-02) ran agents inside this repo, so they could see `AGENTS.md`. The baselines above used scratch directories outside the repo.
- No RL training or held-out generalization results yet. These are extraction and grading experiments.
