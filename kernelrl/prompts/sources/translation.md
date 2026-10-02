## Source: translation pair
The same computation exists as a plain reference and as a hand-optimized AArch64 version.
{brief}

- `reference.c`: the plain version, faithfully copied.
- `solution.c`: a copy of `reference.c` renamed to `{name}_opt`, first comment replaced by
  `/* Candidate implementation. Starts as a copy of the reference. */`.
- `gold.c`: the optimized version that runs on this machine.
