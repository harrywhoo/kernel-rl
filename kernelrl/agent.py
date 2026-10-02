"""Headless Claude Code sessions: build a task from a candidate (extract) and
attempt a task as a baseline optimizer (solve).

Each session starts in a fresh scratch directory outside this repo to avoid
loading repo instructions. This is not a sandbox. Results are copied back.
"""

import json
import math
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from .audit import audit
from .task import HARNESS, SOLVER_FILES, Task, evaluate

PROMPTS = Path(__file__).parent / "prompts"
EXAMPLE = Path(__file__).resolve().parents[1] / "examples" / "libyuv.ARGBAttenuateRow"
KERNELRL = f"{sys.executable} -m kernelrl"
EXTRACT_TOOLS = ["Read", "Write", "Edit", "Glob", "Grep", "Bash(cc:*)", "Bash(nm:*)", "Bash(ls:*)",
                 "Bash(cat:*)", "Bash(grep:*)", "Bash(git show:*)", "Bash(git log:*)", "Bash(git -C:*)",
                 f"Bash({KERNELRL}:*)"]
# The solver only gets path-restricted file tools and its benchmark script.
SOLVE_TOOLS = ["Read", "Write", "Edit", "Glob", "Grep", "Bash(./run.sh)"]


class UsageLimit(RuntimeError):
    pass


def run_claude(prompt, cwd, model, max_turns, tools, add_dirs=(), log=None) -> dict:
    cmd = ["claude", "-p", prompt, "--model", model, "--output-format", "stream-json", "--verbose",
           "--max-turns", str(max_turns), "--permission-mode", "acceptEdits", "--no-session-persistence",
           "--allowedTools", *tools, "--disallowedTools", "WebFetch", "WebSearch", "Agent"]
    for d in add_dirs:
        cmd += ["--add-dir", str(d)]
    t0 = time.time()
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=3600)
    if log:
        log.write_text(p.stdout + p.stderr)
    lines = [json.loads(l) for l in p.stdout.splitlines() if l.startswith("{")]
    result = next((l for l in reversed(lines) if l.get("type") == "result"), {})
    final = result.get("result", "")
    if "hit your" in final and "limit" in final:  # the CLI reports this as a normal reply
        raise UsageLimit(final)
    return {"error": result.get("subtype") if result.get("is_error") else "",
            "cost_usd": result.get("total_cost_usd"), "turns": result.get("num_turns"),
            "seconds": round(time.time() - t0)}


def cpu_name() -> str:
    p = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True)
    return p.stdout.strip() or "unknown CPU"


def _example() -> str:
    files = ("task.toml", "kernel.h", "reference.c", "solution.c", "gold.c", "spec.c", "README.md")
    return "\n\n".join(f"### `{f}`\n```\n{(EXAMPLE / f).read_text()}```" for f in files)


def extract(cand: dict, out: Path, model: str, repairs: int = 1, max_turns: int = 80) -> dict:
    """Candidate {id, name, repo, source, brief} -> task dir in `out`, checked by
    an independent audit, with up to `repairs` repair rounds."""
    repo = Path(cand["repo"])
    commit = subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    section = (PROMPTS / "sources" / f"{cand['source']}.md").read_text().format(name=cand["name"], brief=cand["brief"])
    rec, report = {"id": cand["id"], "source": cand["source"], "model": model, "attempts": []}, None
    with tempfile.TemporaryDirectory(prefix="kr-extract-") as tmp:
        ws = Path(tmp)
        for i in range(repairs + 1):
            repair = "" if report is None else (
                "## Repair round\nThe previous attempt failed this independent audit. Fix it, or write REJECT.md.\n"
                f"```json\n{json.dumps(report, indent=2)}\n```\n")
            prompt = (PROMPTS / "extract.md").read_text().format(
                task_dir=ws, repo=repo, commit=commit, name=cand["name"], cpu=cpu_name(), source_section=section,
                kernelrl=KERNELRL, repair_section=repair, kr_h=(HARNESS / "kr.h").read_text(), example=_example())
            att = run_claude(prompt, ws, model, max_turns, EXTRACT_TOOLS, add_dirs=[repo], log=ws / f".extract_{i}.log")
            if (ws / "REJECT.md").exists():
                att["outcome"] = "rejected"
                rec["attempts"].append(att)
                break
            report = audit(ws)
            att["outcome"] = report["verdict"]
            rec["attempts"].append(att)
            if report["verdict"] != "FAIL":
                break
        rec.update(outcome=rec["attempts"][-1]["outcome"], cost_usd=sum(a["cost_usd"] or 0 for a in rec["attempts"]),
                   proxies=(report or {}).get("proxies", {}))
        (ws / "extraction.json").write_text(json.dumps(rec, indent=2) + "\n")
        shutil.copytree(ws, out / cand["id"], dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("*.o", "bench", "a.out"))
    return rec


def solve(task_root: Path, out: Path, model: str, max_turns: int = 30, sample: int = 0) -> dict:
    """Run a solver on a copy of the task without the hidden fast version,
    then grade its solution.c against the pristine task."""
    task = Task.load(task_root)
    with tempfile.TemporaryDirectory(prefix="kr-solve-") as tmp:
        ws = Path(tmp)
        for f in SOLVER_FILES:
            shutil.copy2(task.root / f, ws / f)
        (ws / "run.sh").write_text(f'#!/bin/sh\nexec {KERNELRL} eval "$(dirname "$0")" --brief\n')
        (ws / "run.sh").chmod(0o755)
        prompt = (PROMPTS / "solve.md").read_text().format(
            readme=(task.root / "README.md").read_text(), cpu=cpu_name(), cflags=" ".join(task.cflags))
        run = run_claude(prompt, ws, model, max_turns, SOLVE_TOOLS, log=ws / "transcript.jsonl")
        g = evaluate(task, ws / "solution.c")
        dest = out / f"{task.id}__{model}__s{sample}"
        shutil.copytree(ws, dest, dirs_exist_ok=True)
    speedup = g.get("geomean_speedup", 0.0) if g["status"] == "ok" else 0.0
    audit_file = task.root / "audit.json"
    gold = json.loads(audit_file.read_text())["proxies"].get("gold_speedup") if audit_file.exists() else None
    rec = {"task": task.id, "model": model, "sample": sample, **run, "status": g["status"],
           "speedup": speedup, "gold_speedup": gold,
           # share of the human log-speedup reached; undefined when the human version has little headroom
           "frac_of_gold": math.log(speedup) / math.log(gold) if gold and gold >= 1.2 and speedup > 0 else None}
    (dest / "result.json").write_text(json.dumps(rec, indent=2) + "\n")
    return rec
