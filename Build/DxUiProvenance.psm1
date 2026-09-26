# Bind ordinary product outputs to the exact restored source/archive and actual linker inputs.
Set-StrictMode -Version Latest

function Write-RedXeDxUiProvenance {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidateSet('x64','ARM64')][string] $Platform,
        [Parameter(Mandatory)][ValidateSet('Debug','Release','ASan Debug')][string] $Configuration
    )
    $ErrorActionPreference='Stop'
    $pin=Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'Dependencies/DxUi.lock.json') | ConvertFrom-Json
    $dependencyRoot=Join-Path $RepoRoot '.build/dependencies/DxUi'
    $identity=Get-Content -Raw -LiteralPath (Join-Path $dependencyRoot "DxUi.identity.$Platform.json") | ConvertFrom-Json
    if ($identity.Identity.commit -cne $pin.commit -or $identity.Identity.platform -cne $Platform) { throw 'Restored DxUi identity does not match the product pin/profile.' }
    # The archive sits under the output root MSBuild was given; restore-dxui.ps1 alone owns that folder name.
    [xml]$props=Get-Content -Raw -LiteralPath (Join-Path $dependencyRoot "DxUi.resolved.$Platform.props")
    $archive=Join-Path ([string]$props.Project.PropertyGroup.DxUiConsumerOutputRoot) "$Platform/$Configuration/DxUi.lib"
    $source=Join-Path $dependencyRoot "source/$($pin.commit)"
    & (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile (Join-Path $RepoRoot 'Dependencies/DxUi.lock.json')
    $output=Join-Path $RepoRoot ".build/$Platform/$Configuration"
    $modules=foreach ($module in @(@{name='RedXe';path='RedXe.exe'},@{name='AVControl';path='Plugins/AVControl.dll'},@{name='AVControlTests';path='AVControlTests.exe'})) {
        $tlog=Join-Path $RepoRoot ".build/Intermediate/$Platform/$Configuration/$($module.name)/$($module.name).tlog/link.command.1.tlog"
        $linkCommand=[IO.File]::ReadAllText($tlog)
        if ($linkCommand.IndexOf($archive.Replace('/','\'),[StringComparison]::OrdinalIgnoreCase) -lt 0) {
            throw "The $($module.name) linker input does not name the current pinned archive: $archive"
        }
        $path=Join-Path $output $module.path
        [ordered]@{path=$module.path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;linkCommandSha256=(Get-FileHash -LiteralPath $tlog -Algorithm SHA256).Hash}
    }
    $headers=(& git -C $source rev-parse HEAD:include).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the pinned public headers.' }
    $record=[ordered]@{repository=$pin.repository;commit=$pin.commit;apiRevision=$pin.apiRevision;platform=$Platform;configuration=$Configuration;
        publicHeadersGitTree=$headers;buildIdentity=$identity;archiveSha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash;modules=@($modules)}
    $path=Join-Path $output 'DxUi.provenance.json'
    $json=$record | ConvertTo-Json -Depth 7
    if (-not (Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -ne $json) { [IO.File]::WriteAllText($path,$json,[Text.UTF8Encoding]::new($false)) }
    return $path
}

function Assert-RedXeDxUiProvenance {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $OutputRoot,
        [Parameter(Mandatory)][string] $LockFile,
        [Parameter(Mandatory)][ValidateSet('x64','ARM64')][string] $Platform,
        [Parameter(Mandatory)][ValidateSet('Debug','Release','ASan Debug')][string] $Configuration
    )
    $pin=Get-Content -Raw -LiteralPath $LockFile | ConvertFrom-Json
    $record=Get-Content -Raw -LiteralPath (Join-Path $OutputRoot 'DxUi.provenance.json') | ConvertFrom-Json
    if ($record.repository -cne $pin.repository -or $record.commit -cne $pin.commit -or $record.apiRevision -ne $pin.apiRevision -or
        $record.platform -cne $Platform -or $record.configuration -cne $Configuration) { throw 'Stale DxUi product provenance.' }
    $expected=@('RedXe.exe','Plugins/AVControl.dll','AVControlTests.exe')
    if (@($record.modules).Count -ne $expected.Count) { throw 'Incomplete DxUi module provenance.' }
    foreach ($name in $expected) {
        $entry=@($record.modules | Where-Object path -CEQ $name)
        if ($entry.Count -ne 1 -or (Get-FileHash -LiteralPath (Join-Path $OutputRoot $name) -Algorithm SHA256).Hash -cne $entry[0].sha256) {
            throw "Stale or missing DxUi consumer module: $name"
        }
    }
}

Export-ModuleMember -Function Write-RedXeDxUiProvenance, Assert-RedXeDxUiProvenance
