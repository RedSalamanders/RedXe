- Aligned agent guidance and dependency automation with the other RedSalamanders repositories (convergence step 1,
  planned in `RedHome`): `CLAUDE.md` is the same thin pointer to `AGENTS.md` in every repository,
  `.claude/settings.json` enables the shared `redhome` Claude Code skill plugin from the `RedHome` marketplace, and
  Dependabot now ignores `RedSalamanders/.github/*` so it stops opening one notice per shared-workflow caller; engine
  pins keep moving through `Update-ProjectPin.ps1` only. No build, test or product input changes.
