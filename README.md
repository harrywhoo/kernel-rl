# kernel-rl

Early experiments in turning kernels from existing repositories into optimization
tasks for RL agents. The current loop finds a candidate, extracts a standalone C
task with an agent, checks correctness and timing, and runs a baseline optimizer.
There is no RL training loop yet.

The pilot has 15 ARM CPU tasks from libyuv, libwebp, KleidiAI and llama.cpp,
measured on an Apple M4 Pro. Four are included in `examples/`.
See [results](docs/results.md) for measurements and open questions, and
[how it works](docs/how-it-works.md) for the task format and grading details.

## Try one task

Use Python ≥ 3.11 and clang. The examples target AArch64; the tested setup is
macOS on an M4 Pro. Run these commands from this checkout:

```sh
python -m pip install -e . pytest
python -m pytest -q
python -m kernelrl eval examples/libyuv.ARGBAttenuateRow --brief
python -m kernelrl eval examples/libyuv.ARGBAttenuateRow --candidate examples/libyuv.ARGBAttenuateRow/gold.c --brief
```

`solution.c` is the starting implementation; `gold.c` is the existing optimized
version, when available. Evaluation checks outputs against `reference.c` and
measures speedup over it, both compiled with `-O3`.

## Extract and solve

`extract` and `solve` need an authenticated `claude` CLI and use its model quota.
Keep the editable install: extraction reads its example task from this checkout.
Replace `path/to/libyuv` with a local source checkout.

```sh
python -m kernelrl mine path/to/libyuv -o cands.jsonl
python -m kernelrl extract cands.jsonl --ids ARGBGrayRow -o data/tasks
python -m kernelrl audit examples/*
python -m kernelrl solve examples/* -o runs/baselines --model claude-haiku-4-5-20251001
```

Audit writes `audit.json` in each task directory. Run benchmarks one at a time
with other heavy jobs stopped; timing noise can change the verdict.

## Layout

- `kernelrl/`: candidate discovery, extraction/solver agents, audit and CLI.
- `kernelrl/harness/`: shared C benchmark driver; each task supplies `spec.c`.
- `kernelrl/prompts/`: extraction and optimization instructions.
- `examples/`: four tasks with source provenance and saved audit reports.
- `experiments/`: earlier probes of existing pipelines; [setup](experiments/README.md).
- `tests/`: grader checks for correct implementations and common failures.

Generated tasks and run logs go in `data/` and `runs/` (ignored by git).
