- Retired two repository-local agent skills now folded into shared skills in `RedHome` (convergence CV-2-12):
  `build-redxe` into `native-build` and `settings-store` into `app-settings`. Every rule and code line of both is in
  the shared skills, with RedXe's commands, paths, templates and product rules in each one's
  `references/repository-facts.md`. Claude Code (the `redhome` plugin), Codex, and Copilot list the shared skills
  from this repository. `AGENTS.md` points at them. No build, test or product input changes.
