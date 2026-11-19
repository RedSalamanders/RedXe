- Moved the vcpkg `builtin-baseline` in `vcpkg.json` from `9eec002d` to `aeb162b7` (microsoft/vcpkg, 2026-10-22),
  RedSalamander's baseline, so every repository resolves ports from one baseline (RedHome CV-4-07, register C-06).
  Between the two baselines only `7zip` and `sqlite3` change, and this repository uses neither, so no dependency
  version changes. Compiler-free checks pass; native qualification runs on `main` after merge.
