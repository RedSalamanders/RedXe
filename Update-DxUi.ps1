<#
.SYNOPSIS
Update RedXe's DxUi lock to the current successfully validated DxUi main commit.
.DESCRIPTION
Updates only Dependencies/DxUi.lock.json after confirming the exact DxUi main commit has a completed successful
CI validation. By default it then runs RedXe's test.ps1. -UpdateOnly retains the lock change without running local
product validation, for use when equivalent validation was completed in another environment.
.PARAMETER UpdateOnly
Skip RedXe validation after changing the lock. This never bypasses DxUi's own successful CI requirement.
.EXAMPLE
.\Update-DxUi.ps1
.EXAMPLE
.\Update-DxUi.ps1 -UpdateOnly
#>
[CmdletBinding(SupportsShouldProcess)]
param([switch] $UpdateOnly)
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
Invoke-RedXeDxUiUpdate -RepoRoot $repoRoot -UpdateOnly:$UpdateOnly -ValidationAction $validationAction -WhatIf:$WhatIfPreference
