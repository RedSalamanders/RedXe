- Moved the vcpkg tool pin in `vcpkg-tool.json` from `9e593bb1` to `aeb162b7`, the same commit as the baseline and
  as RedSalamander's pin (RedHome CV-4-07, register C-07). The managed checkout's scripts and triplets now come from
  that commit and its bootstrap downloads vcpkg-tool 2026-09-26 instead of 2026-07-27; package ABI hashes change, so
  the first restore rebuilds the dependencies. Compiler-free checks pass; native qualification runs on `main` after
  merge.
