# Agent guide

- Read `README.md` for layout and commands.
- If `notes/` exists (local-only, not in git), read `notes/README.md` first and add to it when you learn something the next agent needs.
- `refs/` (local-only) holds reference repos.
- `kernelrl/harness/` is the reward function. Keep `pytest -q` passing after any change there.
- Keep code and docs small and plain. Prefer the standard library.
- Timing results are noisy if other heavy jobs run at the same time; note concurrency when you record numbers.
