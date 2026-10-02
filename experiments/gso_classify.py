"""Run GSO's step-1 commit classifier (its exact prompt) on commits from
kernel repos, using Claude Code headless instead of OpenAI.

usage: python experiments/gso_classify.py REPO N_PERF N_OTHER OUT.jsonl
Samples N_PERF keyword-matched "perf-like" commits and N_OTHER others.
"""

import json
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "refs/gso/src"))
from gso.collect.analysis.prompt import PERF_ANALYSIS_MESSAGE  # noqa: E402

PERF_RE = re.compile(r"\b(perf|optimi[sz]|speed ?up|faster|vectori[sz]|simd|neon|sve|avx|sse|throughput|latency)", re.I)
MAX_DIFF_CHARS = 60_000  # GSO caps commits at 20k tokens


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, errors="replace").stdout


def classify(diff: str, message: str, model: str) -> str:
    prompt = PERF_ANALYSIS_MESSAGE.format(diff_text=diff[:MAX_DIFF_CHARS], message=message)
    out = subprocess.run(["claude", "-p", "--model", model, "--output-format", "json",
                          "--no-session-persistence", "--max-turns", "1", "--tools", ""],
                         input=prompt, capture_output=True, text=True, timeout=300)
    text = json.loads(out.stdout).get("result", "")
    m = re.search(r"\[ANSWER\]\s*(YES|NO)", text, re.I)
    return m.group(1).upper() if m else "PARSE_ERROR"


def main(repo, n_perf, n_other, out_path, model="claude-haiku-4-5-20251001"):
    repo = Path(repo)
    shas = git(repo, "log", "--first-parent", "--no-merges", "-n", "500", "--format=%H").split()
    msgs = {s: git(repo, "log", "-1", "--format=%B", s) for s in shas}
    perf = [s for s in shas if PERF_RE.search(msgs[s])]
    other = [s for s in shas if s not in set(perf)]
    random.seed(0)
    sample = [(s, True) for s in random.sample(perf, min(n_perf, len(perf)))] + \
             [(s, False) for s in random.sample(other, min(n_other, len(other)))]
    with open(out_path, "a") as f:
        for sha, kw in sample:
            ans = classify(git(repo, "show", "--format=", sha), msgs[sha], model)
            row = {"repo": repo.name, "sha": sha[:10], "keyword_perf": kw, "gso": ans,
                   "subject": msgs[sha].splitlines()[0][:100]}
            f.write(json.dumps(row) + "\n")
            f.flush()
            print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
