- Adopted the shared 155-line `.clang-format` of DxUi, RedSalamander, and RedMonitor (SHA-256 `768439d2…` as
  committed) and reformatted every C and C++ source under the `formatRoots` of `.ci/project.json` with the pinned
  clang-format 22.1.3. The change is whitespace only, plus aligned consecutive assignments, `! x` spacing, and the
  160-column limit that the style sets; no include moved.
  `format.ps1` is now the shared script: it takes clang-format from `Install-CiFormatter` in the pinned
  `.ci-engine` instead of `PATH` or Visual Studio, covers the eight extensions CI formats under `formatRoots`
  (now including `RedXeLauncher`), and gains `-Check`, which verifies without writing.
  `Plugins/MatrixRain/GenerateGlyphAtlas.ps1` writes its constants aligned, so a regenerated atlas equals the
  formatted one, and the `Main.cpp` routing check in `BuildProcessTests.ps1` accepts the aligned assignment.
  In `AVControl.Tests.Module.cpp` the reformat moved one `GetProcAddress` cast below the line that its
  `#pragma warning(suppress : 4191)` covers, so the pragma now sits directly above the cast, as it does for
  the file's other exports (found by the zero-warning build; no other suppression in the tree moved).
  `Build_Process.md` gains the shared formatting paragraph, and the validation steps of `Core_CrashHandling.md`
  and `Plugins_API.md` run `format.ps1 -Check`.
  This is step CV-6-05 of the `RedHome` convergence plan (register D-01, D-02, D-05, D-06, E-02).
