"""Task correctness and timing audit. Each check is ok / warn / fail.

  identity  solution.c (a copy of the reference) is correct and times ~1.0x
  probes    the driver rejects a no-op candidate and a perturbed output
  noise     timing CV is low enough to use speedup as a reward
  coverage  at least 2 timed workloads plus correctness-only edge cases
  gold      the human version (if any) matches the reference; its speedup = headroom
"""

import json
import re
from pathlib import Path

from .task import Task, evaluate

CV_WARN, CV_FAIL = 0.05, 0.15
MIN_HEADROOM = 1.2


def _code_lines(path: Path) -> int:
    text = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
    return sum(1 for l in text.splitlines() if l.strip() and not l.strip().startswith("//"))


def _detail(r: dict) -> str:
    failing = [c["workload"] for c in r.get("check", []) if not c["pass"]]
    return str(r.get("error") or r.get("stderr") or f"failing: {failing}")[:1500]


def audit(root, trials: int = 15) -> dict:
    task = Task.load(root)
    checks, proxies = [], {}

    def add(name, level, detail=""):
        checks.append({"check": name, "level": level, "detail": detail})

    ident = evaluate(task, trials=trials)
    if ident["status"] != "ok":
        add("identity", "fail", f"{ident['status']}: {_detail(ident)}")
    else:
        s = ident["geomean_speedup"]
        add("identity", "ok" if 0.85 <= s <= 1.15 else "warn", f"speedup {s:.3f}")

        for probe in ("probe-noop", "probe-perturb"):
            caught = evaluate(task, mode=probe)["status"] == "incorrect"
            add(probe, "ok" if caught else "fail", "" if caught else "driver accepted a bad candidate")

        timed = ident.get("time", [])
        cv = max((max(t["ref_cv"] or 0, t["opt_cv"] or 0) for t in timed), default=0.0)
        add("noise", "fail" if cv > CV_FAIL else "warn" if cv > CV_WARN else "ok", f"max CV {cv:.3f}")
        n_edge = len(ident.get("check", [])) - len(timed)
        add("coverage", "fail" if not timed else "warn" if len(timed) < 2 or n_edge < 1 else "ok",
            f"{len(timed)} timed, {n_edge} correctness-only")
        proxies.update(max_cv=cv, ref_ns={t["workload"]: t["ref_ns"] for t in timed})

        if task.gold:
            g = evaluate(task, task.gold, trials=trials)
            if g["status"] != "ok":
                add("gold", "fail", f"{g['status']}: {_detail(g)}")
            else:
                proxies["gold_speedup"] = g["geomean_speedup"]
                add("gold", "ok" if g["geomean_speedup"] >= MIN_HEADROOM else "warn",
                    f"gold {g['geomean_speedup']:.2f}x")
        else:
            add("gold", "warn", "no gold; headroom unknown")

    proxies["reference_loc"] = _code_lines(task.root / "reference.c")
    levels = {c["level"] for c in checks}
    verdict = "FAIL" if "fail" in levels else "WARN" if "warn" in levels else "PASS"
    result = {"task_id": task.id, "verdict": verdict, "checks": checks, "proxies": proxies}
    (task.root / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
