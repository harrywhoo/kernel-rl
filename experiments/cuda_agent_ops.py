"""Run a random sample of CUDA-Agent-Ops-6K tasks on CPU (small batch) and
flag degenerate ones: non-finite, constant, tiny outputs, or nondeterminism.

usage: python experiments/cuda_agent_ops.py N OUT.jsonl
"""

import json
import random
import re
import sys
import signal

import torch
from datasets import load_dataset


def shrink(code: str) -> str:
    # CPU run: cap large module-level int constants (batch/size) at 4x smaller
    return re.sub(r"^(\w+)\s*=\s*(\d{3,})\s*$", lambda m: f"{m.group(1)} = {max(1, int(m.group(2)) // 4)}", code, flags=re.M)


def run(code: str) -> dict:
    ns = {}
    exec(shrink(code), ns)
    torch.manual_seed(0)
    model = ns["Model"](*ns["get_init_inputs"]()).eval()
    torch.manual_seed(1)
    inputs = ns["get_inputs"]()
    with torch.no_grad():
        a = model(*inputs)
        b = model(*inputs)
    a = a[0] if isinstance(a, (tuple, list)) else a
    b = b[0] if isinstance(b, (tuple, list)) else b
    flat = a.float().flatten()
    return {
        "numel_out": a.numel(),
        "numel_in": sum(x.numel() for x in inputs if torch.is_tensor(x)),
        "nonfinite": bool((~torch.isfinite(flat)).any()),
        "constant": bool(flat.numel() > 1 and (flat == flat[0]).all()),
        "nondeterministic": not torch.equal(a, b),
    }


def timeout(*_):
    raise TimeoutError


if __name__ == "__main__":
    n, out = int(sys.argv[1]), sys.argv[2]
    ds = load_dataset("BytedTsinghua-SIA/CUDA-Agent-Ops-6K", split="train")
    random.seed(0)
    signal.signal(signal.SIGALRM, timeout)
    with open(out, "w") as f:
        for i in random.sample(range(len(ds)), n):
            row = {"idx": i, "source": ds[i]["data_source"]}
            signal.alarm(60)
            try:
                row.update(run(ds[i]["code"]))
            except Exception as e:
                row["error"] = type(e).__name__
            signal.alarm(0)
            f.write(json.dumps(row) + "\n")
