# Pipeline probes

These scripts produced the earlier pipeline comparisons in
[results](../docs/results.md). They have separate dependencies and are not needed
for extracting or grading tasks. Run them from the repo root; outputs belong in
`runs/`.

| Script | Setup | Usage |
|---|---|---|
| `repo2rlenv_filter.py` | Install Repo2RLEnv and its dependencies (Python ≥ 3.12 for the local reference version). Uses private filter methods, so upstream changes may break it. | `python experiments/repo2rlenv_filter.py path/to/repo` |
| `gso_classify.py` | GSO source at `refs/gso/` for its classifier prompt; authenticated `claude` CLI. Consumes model quota. | `python experiments/gso_classify.py path/to/repo 25 25 runs/gso.jsonl` |
| `cuda_agent_ops.py` | `torch` and `datasets`; downloads CUDA-Agent-Ops-6K and executes sampled task code on CPU. | `python experiments/cuda_agent_ops.py 200 runs/cuda_agent_ops.jsonl` |
| `summarize_baselines.py` | none | `python experiments/summarize_baselines.py runs/baselines` (final vs. best speedup seen per run) |

Create `runs/` before running these commands. Reference checkouts and raw outputs
are local and are not included in git. These are exploratory scripts, not a
reproduction package for the upstream pipelines.
