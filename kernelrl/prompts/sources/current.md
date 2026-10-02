## Source: current code
An existing kernel that looks unoptimized for AArch64 (plain C, no SIMD path for this CPU).
{brief}

- `reference.c`: the kernel as it is today.
- `solution.c`: a copy of `reference.c` renamed to `{name}_opt`, first comment replaced by
  `/* Candidate implementation. Starts as a copy of the reference. */`.
- No `gold.c`. Instead, in `task.toml` add `headroom_note`: one or two sentences on why a faster
  AArch64 version should exist (e.g. vectorizable loop, sibling ops that have NEON paths).
