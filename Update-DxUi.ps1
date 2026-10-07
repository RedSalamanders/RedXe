<#
.SYNOPSIS
Update RedXe's DxUi lock to the current successfully validated DxUi main commit.
.DESCRIPTION
Updates only Dependencies/DxUi.lock.json after confirming the exact DxUi main commit has a completed successful
CI validation. By default it then runs RedXe's test.ps1. -UpdateOnly retains the lock change without running local
product validation, for use when equivalent validation was completed in another environment.
.PARAMETER UpdateOnly
Skip RedXe validation after changing the lock. This never bypasses DxUi's own successful CI requirement.
.PARAMETER AllowApiRevisionChange
Accept a DxUi main commit whose capabilities.json names another API revision than the one RedXe is adapted to. Without it
such a commit is refused and the lock stays unchanged. With it the lock records the commit and its revision, which the build
refuses until the supported revision in Build/DxUiRestore.psm1 and the adapters change in the same reviewed change; product
validation is not run.
.EXAMPLE
.\Update-DxUi.ps1
.EXAMPLE
.\Update-DxUi.ps1 -UpdateOnly
#>
[CmdletBinding(SupportsShouldProcess)]
param([switch] $UpdateOnly, [switch] $AllowApiRevisionChange)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
Import-Module (Join-Path $repoRoot 'Build/DxUiUpdate.psm1') -Force -ErrorAction Stop
$validationAction = if ($UpdateOnly) {
    $null
}
else {
    {
        & (Join-Path $repoRoot 'test.ps1')
        if ($LASTEXITCODE -ne 0) { throw "RedXe validation failed with exit code $LASTEXITCODE." }
    }
}
Invoke-RedXeDxUiUpdate -RepoRoot $repoRoot -UpdateOnly:$UpdateOnly -ValidationAction $validationAction -AllowApiRevisionChange:$AllowApiRevisionChange -WhatIf:$WhatIfPreference
