"""A task directory and how to build and benchmark a candidate for it.

Task files:
  task.toml    metadata (source, signature, build flags)
  README.md    instruction for the solver
  kernel.h     declares the reference and candidate symbols
  reference.c  scalar reference
  solution.c   the candidate; the only file a solver may edit
  gold.c       optional human expert version (hidden from solvers)
  spec.c       adapter for the driver: workloads, inputs, outputs

Grading always rebuilds from the pristine task files plus one candidate file.
"""

import json
import platform
import shutil
import subprocess
import tempfile
import tomllib
from dataclasses import dataclass
from pathlib import Path

HARNESS = Path(__file__).parent / "harness"
SOLVER_FILES = ("task.toml", "README.md", "kernel.h", "reference.c", "spec.c", "solution.c")

# Undefined symbols a candidate may use. Anything else, including the
# reference symbol, fails the build.
ALLOWED_SYMBOLS = {
    "memcpy", "memmove", "memset", "memcmp", "bzero", "malloc", "free", "calloc", "abort",
    "sqrtf", "sqrt", "expf", "exp", "logf", "log", "powf", "pow", "fabsf", "fabs",
    "floorf", "floor", "ceilf", "ceil", "roundf", "round", "lrintf", "fmaf", "fma",
    "sinf", "cosf", "tanhf", "exp2f", "log2f",
    "__stack_chk_fail", "__stack_chk_guard", "__memcpy_chk", "__memset_chk", "__memmove_chk",
}


@dataclass
class Task:
    root: Path
    meta: dict

    @classmethod
    def load(cls, root) -> "Task":
        root = Path(root).resolve()
        with open(root / "task.toml", "rb") as f:
            return cls(root, tomllib.load(f))

    @property
    def id(self) -> str:
        return self.meta.get("id", self.root.name)

    @property
    def cflags(self) -> list[str]:
        return self.meta.get("build", {}).get("cflags", ["-O3", "-std=gnu11"])

    @property
    def gold(self) -> Path | None:
        p = self.root / "gold.c"
        return p if p.exists() else None


def _undefined_symbols(obj: Path) -> set[str]:
    out = subprocess.run(["nm", "-u", str(obj)], capture_output=True, text=True, check=True).stdout.split()
    strip = platform.system() == "Darwin"  # Mach-O prefixes C symbols with "_"
    return {s[1:] if strip and s.startswith("_") else s for s in out if s != "U"}


def build(task: Task, candidate: Path, out: Path) -> Path:
    """Compile driver + spec + reference with `candidate`. Raises RuntimeError."""
    for f in ("kernel.h", "reference.c", "spec.c"):
        shutil.copy2(task.root / f, out / f)
    shutil.copy2(candidate, out / "candidate.c")
    flags = [*task.cflags, "-Wall", "-I", str(out), "-I", str(HARNESS)]
    srcs = {"driver": HARNESS / "driver.c", "spec": out / "spec.c",
            "reference": out / "reference.c", "candidate": out / "candidate.c"}
    for name, src in srcs.items():
        p = subprocess.run(["cc", *flags, "-c", str(src), "-o", str(out / f"{name}.o")],
                           capture_output=True, text=True)
        if p.returncode:
            raise RuntimeError(f"compile {src.name} failed:\n{p.stderr[-4000:]}")
    bad = _undefined_symbols(out / "candidate.o") - ALLOWED_SYMBOLS
    if bad:
        raise RuntimeError(f"candidate uses disallowed symbols: {sorted(bad)}")
    exe = out / "bench"
    p = subprocess.run(["cc", *[str(out / f"{n}.o") for n in srcs], "-lm", "-o", str(exe)],
                       capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f"link failed:\n{p.stderr[-4000:]}")
    return exe


def evaluate(task: Task, candidate: Path | None = None, mode: str = "all",
             trials: int = 15, timeout: float = 600) -> dict:
    """Build and run one candidate. `status` is one of
    build_error | crash | timeout | incorrect | ok."""
    candidate = candidate or task.root / "solution.c"
    with tempfile.TemporaryDirectory(prefix="kr-") as tmp:
        try:
            exe = build(task, candidate, Path(tmp))
        except RuntimeError as e:
            return {"status": "build_error", "error": str(e)}
        try:
            p = subprocess.run([str(exe), "--mode", mode, "--trials", str(trials)],
                               capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return {"status": "timeout"}
    try:
        report = json.loads(p.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {"status": "crash", "returncode": p.returncode, "stderr": p.stderr[-4000:]}
    return {"status": "ok" if report.get("correct", True) else "incorrect", **report}
