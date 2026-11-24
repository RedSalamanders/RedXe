<#
.SYNOPSIS
Validates a repository's agent guidance without writing files.
.DESCRIPTION
Applies the agent-guidance contract G1 to G13: the skill grammar of every .agents/skills/<name>/SKILL.md, strict UTF-8
reading, Markdown fences and local references of the skill packages, the root and scoped AGENTS.md files and the
instruction pointers, reparse points below the root, harness files, the scoped-guidance inventory .agents/guidance.json,
the skill index and pointers, PowerShell examples in skill packages and the plan indexes under Specs/Plans.
Engine shim: runs Tools/Test-AgentGuidance.ps1 of the shared CI engine that .ci/project.json pins, from .ci-engine,
and refuses with one message when that checkout is missing, at another commit or changed. The engine's
Specs/ProjectCommands.md is the contract.
.PARAMETER Root
Repository root; defaults to the repository containing this command.
.PARAMETER Kind
Product (the default): every local reference stays inside the repository. Documentation (RedHome): references may name
sibling repositories in the folder that holds Root, and PowerShell examples may name product commands it lacks (G13).
.PARAMETER CheckSiblings
With Kind Documentation, also requires each sibling reference to exist, resolved against the folder that holds Root.
.OUTPUTS
One console summary line, "Agent guidance valid: <n> skills, <m> references."; no persistent files. Invalid guidance
raises a terminating error, "Agent guidance validation failed:" followed by one problem per line, and a nonzero process exit.
.NOTES
Requires PowerShell 7, Git, and the pinned engine at .ci-engine. Read-only; no network, build or desktop access.
Primary consumers: validate-skills.ps1, repository validation, PrePush and the compiler-free CI phase.
.EXAMPLE
./Tools/Test-AgentGuidance.ps1
.EXAMPLE
./Tools/Test-AgentGuidance.ps1 -Kind Documentation -CheckSiblings
#>
[CmdletBinding()]
param([string] $Root = (Split-Path $PSScriptRoot -Parent), [ValidateSet('Product', 'Documentation')][string] $Kind = 'Product', [switch] $CheckSiblings)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Engine shim (Specs/ProjectCommands.md in the engine); the same lines in every shim.
# The product root holds .ci/project.json: the shim's own folder, or the one above it for a shim under Tools/.
$project = if ([IO.File]::Exists((Join-Path $PSScriptRoot '.ci/project.json'))) { $PSScriptRoot } else { Split-Path $PSScriptRoot -Parent }
$binding = Join-Path $project '.ci-engine/Tools/Modules/Ci.EngineBinding.psm1'
if (-not [IO.File]::Exists($binding)) {
    throw "$($MyInvocation.MyCommand.Name) runs the shared CI engine that .ci/project.json pins from .ci-engine, and $project/.ci-engine is missing or predates engine shims. Check out the pinned commit (engine.ref in .ci/project.json): git -C .ci-engine fetch origin <engine.ref>; git -C .ci-engine checkout --detach <engine.ref> (first time: git clone https://github.com/RedSalamanders/.github.git .ci-engine)."
}
Import-Module $binding -Force
$command = Resolve-CiPinnedEngineCommand -ProjectRoot $project -Name $MyInvocation.MyCommand.Name
$PSBoundParameters['Root'] = $Root
& $command @PSBoundParameters
