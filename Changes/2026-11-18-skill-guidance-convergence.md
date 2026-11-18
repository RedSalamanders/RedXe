- The `plugin-development` skill description now says when to use it ("Use when ..."), so every local skill
  description says what the skill does and then when to use it, the form the shared `RedHome` skills use, because an
  agent picks a skill from its description alone (`RedHome` convergence step CV-2-13). Only its front-matter
  description changes. The three `agents/openai.yaml` Codex UI metadata files are removed (CV-2-11, register A-11
  decided "none"), and `Specs/Build/Build_SkillPackages.md` says no skill carries one; the validator still treats the
  file as optional. `validate-skills.ps1` passes; no build, test or product input changes.
