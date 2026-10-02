"""Summarize baseline solver runs as a markdown table.

For each run: final graded speedup, the best correct speedup the agent saw from
./run.sh during the session, number of ./run.sh calls, turns and cost.

usage: python experiments/summarize_baselines.py runs/baselines
"""

import json
import sys
from collections import defaultdict
from pathlib import Path


def best_seen(transcript: Path) -> tuple[float | None, int]:
    """Best correct geomean speedup reported by ./run.sh, and how often it ran."""
    pending, best, calls = set(), None, 0
    for line in transcript.read_text().splitlines():
        if not line.startswith("{"):
            continue
        msg = json.loads(line)
        content = msg.get("message", {}).get("content", [])
        if msg.get("type") == "assistant":
            for c in content:
                if c.get("type") == "tool_use" and c["name"] == "Bash" and "run.sh" in c["input"].get("command", ""):
                    pending.add(c["id"])
        elif msg.get("type") == "user" and isinstance(content, list):
            for c in content:
                if isinstance(c, dict) and c.get("tool_use_id") in pending:
                    calls += 1
                    text = c["content"] if isinstance(c["content"], str) else json.dumps(c["content"])
                    if '"status": "ok"' in text and '"geomean_speedup": ' in text:
                        s = float(text.split('"geomean_speedup": ')[1].split(",")[0].split("\\n")[0])
                        best = s if best is None else max(best, s)
    return best, calls


def main(root: Path) -> None:
    rows = defaultdict(dict)
    models = []
    for r in sorted(root.glob("*/result.json")):
        rec = json.loads(r.read_text())
        best, calls = best_seen(r.parent / "transcript.jsonl")
        if rec["model"] not in models:
            models.append(rec["model"])
        rows[rec["task"]]["gold"] = rec["gold_speedup"]
        rows[rec["task"]][rec["model"]] = (rec, best, calls)

    short = [m.replace("claude-", "").split("-2025")[0] for m in models]
    print("| task | human version | " + " | ".join(f"{s} final (best seen)" for s in short) + " |")
    print("|---|---|" + "---|" * len(models))
    for task, cols in rows.items():
        gold = f"{cols['gold']:.2f}×" if cols.get("gold") else "–"
        cells = []
        for m in models:
            if m not in cols:
                cells.append("")
                continue
            rec, best, _ = cols[m]
            final = f"{rec['speedup']:.2f}×" if rec["status"] == "ok" else rec["status"]
            cells.append(f"{final} ({best:.2f}×)" if best else final)
        print(f"| {task} | {gold} | " + " | ".join(cells) + " |")

    print("\n| model | runs | final correct | final > 1.1× | mean turns | mean run.sh calls | total cost |")
    print("|---|---|---|---|---|---|---|")
    for m, s in zip(models, short):
        recs = [cols[m] for cols in rows.values() if m in cols]
        ok = sum(r["status"] == "ok" for r, _, _ in recs)
        fast = sum(r["status"] == "ok" and r["speedup"] > 1.1 for r, _, _ in recs)
        turns = sum(r["turns"] or 0 for r, _, _ in recs) / len(recs)
        calls = sum(c for _, _, c in recs) / len(recs)
        cost = sum(r["cost_usd"] or 0 for r, _, _ in recs)
        print(f"| {s} | {len(recs)} | {ok} | {fast} | {turns:.0f} | {calls:.0f} | ${cost:.2f} |")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
