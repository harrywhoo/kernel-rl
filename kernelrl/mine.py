"""Candidate discovery.

translation_pairs: kernels with a scalar `Foo_C` and ISA variants such as
  `Foo_NEON` / `Foo_SSE2`. The scalar version is the oracle; an ARM variant is the gold.
perf_commits: commits that look like kernel performance changes. Unlike
  Repo2RLEnv/GSO filters, this keeps perf/GPU/ISA-specific commits and does not require new tests.
"""

import json
import re
import subprocess
from collections import defaultdict
from pathlib import Path

EXTS = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".S"}
ISAS = ["C", "NEON", "NEON64", "SVE", "SVE2", "SME", "SME2", "DOTPROD", "I8MM",
        "SSE2", "SSSE3", "SSE41", "SSE4_1", "AVX", "AVX2", "AVX512BW",
        "MSA", "LSX", "LASX", "RVV", "MIPS32", "MIPSdspR2"]
IDENT = re.compile(rf"\b([A-Za-z]\w*?)_({'|'.join(sorted(ISAS, key=len, reverse=True))})\b")
SKIP_DIRS = {".git", "third_party", "build", "tests", "test", "unit_test", "fuzz", "docs"}


def _find_def(root: Path, files: list[str], name: str) -> str | None:
    pat = re.compile(rf"^[A-Za-z_][\w\s\*]*\b{name}\s*\([^;]*$")
    for rel in sorted(files, key=lambda f: Path(f).suffix.startswith(".h")):  # sources first
        for i, line in enumerate((root / rel).read_text(errors="replace").splitlines(), 1):
            if pat.match(line):
                return f"{rel}:{i}"
    return None


def translation_pairs(root) -> list[dict]:
    root = Path(root).resolve()
    seen = defaultdict(lambda: defaultdict(set))  # base -> isa -> files
    for p in root.rglob("*"):
        rel = p.relative_to(root)
        if p.suffix in EXTS and p.is_file() and not SKIP_DIRS & set(rel.parts[:-1]):
            for m in IDENT.finditer(p.read_text(errors="replace")):
                if not m.group(1).endswith(("_Any", "_ANY")) and not m.group(1).startswith("HAS_"):
                    seen[m.group(1)][m.group(2)].add(str(rel))
    out = []
    for base, by_isa in sorted(seen.items()):
        if "C" in by_isa and len(by_isa) > 1:
            files = {isa: sorted(fs) for isa, fs in by_isa.items()}
            brief = (f"Scalar `{base}_C` (likely at {_find_def(root, files['C'], f'{base}_C')}; may be "
                     f"macro-generated). Variants: {', '.join(sorted(by_isa))}. Files: {json.dumps(files)}. "
                     "Use a 64-bit NEON variant as gold if one exists; otherwise omit gold.c.")
            out.append({"id": f"{root.name}.{base}", "name": base, "repo": str(root),
                        "source": "translation", "variants": sorted(by_isa), "brief": brief})
    return out


PERF_RE = re.compile(r"\b(perf|optimi[sz]|speed ?up|faster|vectori[sz]|simd|neon|sve|sme|dotprod|i8mm|unroll)", re.I)
NOT_KERNEL_RE = re.compile(r"\b(ci|build|cmake|docs?|test|bench(mark)?|readme|warning)\b", re.I)
KERNEL_EXTS = {".c", ".cc", ".cpp", ".h", ".hpp", ".S"}


def perf_commits(repo, paths=(), limit: int = 500, max_files: int = 5) -> list[dict]:
    """Recent first-parent commits that look like kernel optimizations."""
    git = lambda *a: subprocess.run(["git", "-C", str(repo), *a], capture_output=True, text=True).stdout
    out = []
    for line in git("log", "--first-parent", "--no-merges", f"-n{limit}", "--format=%H%x09%s", "--", *paths).splitlines():
        sha, subject = line.split("\t", 1)
        if not PERF_RE.search(subject) or NOT_KERNEL_RE.search(subject.split(":")[0]):
            continue
        files = git("show", "--format=", "--name-only", sha).split()
        if 0 < len(files) <= max_files and all(Path(f).suffix in KERNEL_EXTS for f in files):
            out.append({"sha": sha, "subject": subject, "files": files})
    return out
