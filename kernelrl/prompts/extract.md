You are building one RL training environment for kernel-optimization agents.
Turn a kernel from an existing C/C++ repository into a **self-contained,
benchmarkable task** in the current directory (`{task_dir}`).

## The candidate
- Repository checkout (read-only): `{repo}` at commit `{commit}`
- Task name: `{name}`. Use `{name}_ref` and `{name}_opt` as the two symbols.
- Machine: {cpu} (AArch64: NEON, dotprod, i8mm, bf16, SME2; **no SVE**).

{source_section}

## Files to produce (see the complete example below)
- `kernel.h` declares `{name}_ref` and `{name}_opt` with the same signature, and
  documents the contract (layout, valid parameter ranges) in a comment.
- `reference.c` is the reference implementation, `{name}_ref`. Slice in only the macros, constants,
  tables and helpers it needs (make them `static`). Write C (gnu11); port C++ only where needed.
- `solution.c` is the solver's starting point, `{name}_opt`, as described above.
- `gold.c` (only if the section above names one) is the known-good fast version, `{name}_opt`.
  Keep its fast code verbatim. If the original only handles part of the input (e.g. multiples of 8),
  add the repo's own tail handling so it is valid for every input the contract allows.
- `spec.c` is the adapter for the trusted driver (`kr.h`, below). Timed workloads (weight > 0)
  should come from how the repo actually calls this kernel (read the call sites). Add
  correctness-only workloads (weight 0) for tails, tiny and zero sizes, and extreme values.
  Every buffer must come from `kr_alloc`. Each timed call should take at least ~200 ns.
- `task.toml` uses the example's keys. Set `[source] kind` to `"translation"`, `"commit"` or `"current"`.
  Put any ISA flags in `cflags`. Integer outputs are exact (atol 0). If the gold legitimately differs
  (e.g. rounding), use the smallest tolerance that passes and explain it in `tolerance_note`.
- `README.md` is the solver's instruction, in the example's style.

## Rules
- Only write inside this directory. No network. No libraries beyond libc/libm.
- Check your work with `{kernelrl} audit .`. `{kernelrl} eval . --candidate gold.c` shows gold details.
- Iterate until the audit verdict is PASS or WARN. WARN is fine for low headroom or no gold.
- If this cannot become a self-contained pure task (large global state, threads, I/O,
  a dependency that can't be sliced), write `REJECT.md` with a one-paragraph reason and stop.
- End with a 3–5 line summary: what the kernel does, the workloads and why, and the audit verdict.

{repair_section}
## Driver contract (`kr.h`)
```c
{kr_h}
```

## Complete example task (libyuv, translation pair)
{example}
