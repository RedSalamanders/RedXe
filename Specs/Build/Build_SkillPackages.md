# Skill packages and guidance validation

Status: normative repository tooling contract
Last reviewed: 2026-11-24

## Ownership and verdicts

`AGENTS.md` owns repository guidance. Skills under `.agents/skills/` supply task-specific procedure and route to
current domain contracts; they do not maintain another schema, plugin layout specification or delivery history.
Current values belong in the owned catalog/project/schema/lock artifacts. Historical plans cannot override them.

`validate-skills.ps1` validates pending changelog fragments and runs the pinned CI engine's agent guidance
contract G1 through G13 in `Specs/ProjectCommands.md`. The shared validator checks skill metadata, package links,
PowerShell example signatures, instructions and plan indexes. RedXe declares its root guidance in
`.agents/guidance.json`.

```powershell
./validate-skills.ps1
```

The engine runs the shared guidance regression fixtures on Windows and Linux. RedXe uses the same command in
the independent tooling runner and compiler-free pull request checks. The exact `validate-skills.ps1` and
`Tools/Test-AgentGuidance.ps1` paths select `BuildProcess`. The scoped planning fixtures verify those paths and
the broader rules for neighboring sources.

Category decisions for ordinary, opt-in, Debug-only and service plugins
must agree with `RedXe/BundledPlugins.h`, the owning [settings contract](../Core/Core_Settings.md#selection-storage-and-deployed-defaults)
and the existing Settings template tests. Preserve valid templates when correcting stale guidance.
