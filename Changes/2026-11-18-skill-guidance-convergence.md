- The `plugin-development` skill description now says when to use it ("Use when ..."), so every local skill
  description says what the skill does and then when to use it, the form the shared `RedHome` skills use, because an
  agent picks a skill from its description alone (`RedHome` convergence step CV-2-13). Only its front-matter
  description changes. The three `agents/openai.yaml` Codex UI metadata files are removed (CV-2-11, register A-11
  decided "none"), and `Specs/Build/Build_SkillPackages.md` says no skill carries one; the validator still treats the
  file as optional. `AGENTS.md` shrinks from 29,214 to under 21,000 bytes, below the 24,000-byte limit past which
  Antigravity truncates a rule file (CV-3-08): its source-tree map moves unchanged into the new
  `Specs/Core/Core_SourceLayout.md`, indexed in `Specs/README.md`, and `AGENTS.md` links to it and keeps every
  ownership rule. `validate-skills.ps1` passes; no build, test or product input changes.
