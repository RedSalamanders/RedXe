- Run the changelog commands from the pinned shared CI engine (convergence CV-5-02). `Tools/Test-Changelog.ps1`,
  `Fold-Changelog.ps1`, `Submit-ChangelogFold.ps1`, `Test-ChangelogWorkflow.ps1` and the new `Release-Changelog.ps1`
  are now copies of the engine's shims (`Templates/Project/Tools` at `f965c79`): the same names and parameters, running
  the engine implementation in `.ci-engine`, and refusing with one message when that checkout is missing, at another
  commit or changed. `Tools/Modules/Documentation/Changelog.psm1` is removed. The behavior suite now also proves these
  shims equal the pinned templates and that `Test-Changes.ps1 -Mode PrePush` checks the changelog first. Two engine
  changes reach this repository: two pending fragments with the same new text are refused by the fold, and a Git error
  while reading the base changelog fails `-Base` instead of skipping it. The scoped-testing runner fixture gets its own
  manifest and `.ci-engine` clone; `Specs/Core/Core_Changes.md` now points to the engine contract. Tooling-only;
  native runtime coverage is unchanged.
