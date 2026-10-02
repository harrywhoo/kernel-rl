"""How many performance commits in kernel repos survive Repo2RLEnv's
commit_runtime filters? Runs only its cheap filters (no Docker, no LLM).

usage: python experiments/repo2rlenv_filter.py REPO [REPO ...]
"""

import re
import sys
from collections import Counter
from pathlib import Path
from types import SimpleNamespace

from repo2rlenv.git_local import list_commits, show_diff
from repo2rlenv.pipelines.commit_runtime import CommitRuntimePipeline
from repo2rlenv.pipelines.pr_runtime import split_patch_and_test_patch
from repo2rlenv.spec.options import CommitRuntimeOptions

# Our rough label for "this commit is a performance change".
PERF_RE = re.compile(r"\b(perf|optimi[sz]|speed ?up|faster|vectori[sz]|simd|neon|sve|avx|sse|throughput|latency)", re.I)


def run(repo: Path, limit: int = 500) -> dict:
    fake = SimpleNamespace(options=CommitRuntimeOptions())
    stats = {"all": Counter(), "perf": Counter()}
    for c in list_commits(repo, limit=limit):
        group = "perf" if PERF_RE.search(c.message or "") else "other"
        reason = CommitRuntimePipeline._metadata_filter(fake, c)
        if reason is None:
            src, tests = split_patch_and_test_patch(show_diff(repo, c.sha))
            reason = CommitRuntimePipeline._structural_filter(fake, src, tests) or "kept"
        stats["all"][reason] += 1
        if group == "perf":
            stats["perf"][reason] += 1
    return stats


if __name__ == "__main__":
    for r in sys.argv[1:]:
        s = run(Path(r))
        n, p = sum(s["all"].values()), sum(s["perf"].values())
        print(f"{Path(r).name}: {n} commits, {p} perf-like")
        print(f"  all : kept {s['all']['kept']}  {dict(s['all'].most_common())}")
        print(f"  perf: kept {s['perf']['kept']}  {dict(s['perf'].most_common())}")
