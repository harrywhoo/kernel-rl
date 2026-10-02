"""python -m kernelrl {mine,extract,audit,eval,solve} ..."""

import argparse
import json
from pathlib import Path


def _print(obj):
    print(json.dumps(obj, indent=2))


def main(argv=None):
    p = argparse.ArgumentParser(prog="kernelrl")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("mine", help="find translation-pair candidates")
    s.add_argument("repo")
    s.add_argument("-o", "--output", required=True, help="candidates .jsonl")

    s = sub.add_parser("extract", help="agent builds task dirs from candidates")
    s.add_argument("candidates")
    s.add_argument("--ids", required=True, help="comma-separated candidate names or ids")
    s.add_argument("-o", "--output", default="data/tasks")
    s.add_argument("--model", default="claude-sonnet-5")

    s = sub.add_parser("audit", help="check task correctness, coverage and timing")
    s.add_argument("tasks", nargs="+")

    s = sub.add_parser("eval", help="build and benchmark one candidate")
    s.add_argument("task")
    s.add_argument("--candidate")
    s.add_argument("--mode", default="all")
    s.add_argument("--brief", action="store_true")

    s = sub.add_parser("solve", help="run a baseline solver agent")
    s.add_argument("tasks", nargs="+")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--model", default="claude-haiku-4-5-20251001")
    s.add_argument("--max-turns", type=int, default=30)

    a = p.parse_args(argv)
    from .agent import UsageLimit
    try:
        _run(a)
    except UsageLimit as e:
        raise SystemExit(f"stopped: account usage limit reached ({e})")


def _run(a):
    if a.cmd == "mine":
        from .mine import translation_pairs
        cands = translation_pairs(a.repo)
        Path(a.output).write_text("".join(json.dumps(c) + "\n" for c in cands))
        print(f"{len(cands)} candidates -> {a.output}")
    elif a.cmd == "extract":
        from .agent import extract
        want = set(a.ids.split(","))
        for c in map(json.loads, open(a.candidates)):
            if c["name"] in want or c["id"] in want:
                r = extract(c, Path(a.output), a.model)
                print(json.dumps({k: r[k] for k in ("id", "outcome", "cost_usd", "proxies")}), flush=True)
    elif a.cmd == "audit":
        from .audit import audit
        for t in a.tasks:
            r = audit(t)
            print(r["task_id"], r["verdict"], json.dumps(r["proxies"]))
            for c in r["checks"]:
                if c["level"] != "ok":
                    print(f"   {c['check']} {c['level']}: {c['detail']}")
    elif a.cmd == "eval":
        from .task import Task, evaluate
        r = evaluate(Task.load(a.task), Path(a.candidate).resolve() if a.candidate else None, a.mode)
        if a.brief:
            r = {"status": r["status"], "geomean_speedup": r.get("geomean_speedup"),
                 "failing": [c for c in r.get("check", []) if not c["pass"]],
                 "time": [{k: t[k] for k in ("workload", "speedup")} for t in r.get("time", [])],
                 **{k: r[k] for k in ("error", "stderr") if k in r}}
        _print(r)
    elif a.cmd == "solve":
        from .agent import solve
        out = Path(a.output)
        out.mkdir(parents=True, exist_ok=True)
        with open(out / "results.jsonl", "a") as f:
            for t in a.tasks:
                r = solve(Path(t), out, a.model, a.max_turns)
                f.write(json.dumps(r) + "\n")
                print(json.dumps({k: r[k] for k in ("task", "status", "speedup", "gold_speedup", "error")}), flush=True)


if __name__ == "__main__":
    main()
