## Source: performance commit
A historical commit made this kernel faster without changing its results.
{brief}

- `reference.c`: the kernel as it was **before** the commit (`git show <parent>:<file>`).
- `solution.c`: the same pre-commit code renamed to `{name}_opt`, first comment replaced by
  `/* Candidate implementation. Starts as the pre-commit version. */`.
- `gold.c`: the kernel **after** the commit. It must match the reference on every workload;
  if it does not, the commit changed semantics, so write REJECT.md.
- Choose workloads where the commit's improvement shows up, plus the usual edge cases.
