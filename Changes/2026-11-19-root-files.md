- Converged the root files with every RedSalamanders repository (convergence CV-6-03, owner decision D18 and the
  B-12 default): `LICENSE.txt` is now only the common MIT text, and its files-under-other-terms section and library
  list, full license texts included, move verbatim to `THIRD-PARTY-NOTICES.md`, whose DxUi entry now also names what
  that revision compiles in. The duplicate `LICENSE` is removed. The portable package stages and requires the notices
  file; its README.txt, `Build_Packaging.md` and `README.md` say where each part is. The common `CONTRIBUTING.md` and
  pull request template are added. Validated with `validate-skills.ps1`, the packaging tooling tests and
  `Tools/Test-Changelog.ps1`; native packaging runs in main qualification.
