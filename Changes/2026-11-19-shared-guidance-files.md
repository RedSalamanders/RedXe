- Agent guidance now matches the other RedSalamanders repositories (`RedHome` convergence step CV-6-02).
  `.github/copilot-instructions.md` is the identical pointer to `AGENTS.md` that every repository uses. `AGENTS.md`
  is titled "RedXe development guidance", with the shared-skill pointer directly under `## Skills`. The drive-root,
  scoped-testing, Changes, native build and Unified CI sections use the text every repository shares, and a new
  shared "Interactive desktop tests" section states the focus rule. RedXe's own scoped-testing facts move into Build
  and validation, which no longer asks for `test.ps1 -Full` on every change: PrePush accounts for the remaining
  coverage. The common `.gitignore` block ignores `.claude/settings.local.json` and `.claude/scheduled_tasks.lock`,
  and a new `capabilities.json` records what RedXe supports and what is pending, each entry restating the docs or
  the WIP index. Nothing compiles differently.
