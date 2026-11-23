<#
.SYNOPSIS
Explains and runs the tests that a change affects, reusing identical successful local evidence.
.DESCRIPTION
Affected (the default) compares the checkout with its merge base with -BaseRef, or, in an ordinary local run, with the
last successful affected run of the profile for 24 hours. Full runs every scope of one profile. PrePush refuses an
uncommitted changelog, checks it against the base and runs every scope of each PrePush profile this repository lists.
An unmapped input selects every scope; interactive suites are listed as INTERACTIVE_NOT_RUN and never run.
Tests/test-scopes.json (version 2) holds this repository's scopes, rules and profiles.
Engine shim: runs Tools/Test-Changes.ps1 of the shared CI engine that .ci/project.json pins, from .ci-engine, and
refuses with one message when that checkout is missing, at another commit or changed. The engine's
Specs/ProjectCommands.md is the contract (Scoped testing).
.PARAMETER Root
Repository root; defaults to the repository containing this command.
.PARAMETER Mode
Affected, Full or PrePush.
.PARAMETER BaseRef
Comparison ref, by default origin/<defaultBranch>. Never fetched. An explicit value turns the iteration baseline off.
.PARAMETER Scopes
Explicit scopes of an Affected run, as names or comma-separated strings.
.PARAMETER Configuration
Debug, Release or ASan Debug, for Affected and Full.
.PARAMETER Platform
x64 or ARM64, for Affected and Full. Runtime tests need a host of the same architecture.
.PARAMETER Explain
Prints the plan, its reasons and the interactive obligations, then stops: no changelog gate, lease, build, test or write.
.PARAMETER Force
Runs every selected scope even when identical successful evidence exists, and ignores the iteration baseline.
.PARAMETER SkipBuild
Uses only build outputs whose attestation still matches.
.PARAMETER ParallelSuites
Suites test.ps1 may run at once (1 to 8).
.OUTPUTS
The plan with its reasons, REUSED/PASSED lines and one token: NOTHING_SELECTED, PROSE_CHECKS_PASSED, SELECTED_PASSED or
FULL_PASSED, followed by "; repository NOT_EVALUATED". Any failure is a terminating error.
.NOTES
Requires PowerShell 7, Git and the pinned engine at .ci-engine. Runs Tools/Test-Changelog.ps1, build.ps1, test.ps1 and
the scopes' commands; writes evidence under .build/reports/scoped-tests. No fetch, network or desktop focus.
.EXAMPLE
./Test-Changes.ps1 -Explain
.EXAMPLE
./Test-Changes.ps1 -Mode PrePush
#>
[CmdletBinding()]
param(
    [string]$Root = $PSScriptRoot,
    [ValidateSet('Affected', 'Full', 'PrePush')][string]$Mode = 'Affected',
    [string]$BaseRef = '',
    [string[]]$Scopes = @(),
    [ValidateSet('Debug', 'Release', 'ASan Debug')][string]$Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')][string]$Platform = 'x64',
    [switch]$Explain,
    [switch]$Force,
    [switch]$SkipBuild,
    [ValidateRange(1, 8)][int]$ParallelSuites = 1
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Engine shim (Specs/ProjectCommands.md in the engine); the same lines in every shim.
# A root shim's product root is its own folder, so a copy in a nested fixture never binds to the product around it.
$project = $PSScriptRoot
$binding = Join-Path $project '.ci-engine/Tools/Modules/Ci.EngineBinding.psm1'
if (-not [IO.File]::Exists($binding)) {
    throw "$($MyInvocation.MyCommand.Name) runs the shared CI engine that .ci/project.json pins from .ci-engine, and $project/.ci-engine is missing or predates engine shims. Check out the pinned commit (engine.ref in .ci/project.json): git -C .ci-engine fetch origin <engine.ref>; git -C .ci-engine checkout --detach <engine.ref> (first time: git clone https://github.com/RedSalamanders/.github.git .ci-engine)."
}
Import-Module $binding -Force
$command = Resolve-CiPinnedEngineCommand -ProjectRoot $project -Name $MyInvocation.MyCommand.Name
$PSBoundParameters['Root'] = $Root
& $command @PSBoundParameters
