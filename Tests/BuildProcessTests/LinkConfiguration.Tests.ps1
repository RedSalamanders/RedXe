[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repoRoot 'Build/DxUiRestore.psm1') -Force
$msbuildPath = Find-RedXeMSBuild
# Independent tooling runs before a product restore writes platform-specific resolved props.
# Supply the exact pinned source for project evaluation without building DxUi or its dependencies.
$pin = Read-RedXeDxUiLock -LockFile (Join-Path $repoRoot 'Dependencies/DxUi.lock.json')
$null = Restore-RedXeDxUiPin -RepoRoot $repoRoot
$dxUiSource = Get-RedXeDxUiSourcePath -RepoRoot $repoRoot -Commit $pin.commit
$testParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build/BuildProcessTests'))
$testRoot = Join-Path $testParent ([guid]::NewGuid().ToString('N'))
$expectedPrefix = $testParent.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $testRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing a link-configuration fixture outside '$testParent'."
}
[void](New-Item -ItemType Directory -Path $testRoot -Force)

try {
    $projectPath = Join-Path $testRoot 'LinkEvaluation.vcxproj'
    $projects = @(& git -C $repoRoot ls-files -- '*.vcxproj')
    if ($LASTEXITCODE -ne 0 -or $projects.Count -eq 0) {
        throw 'Could not enumerate the native projects for link-configuration validation.'
    }
    $checked = 0
    foreach ($project in $projects) {
        $nativeProject = [Security.SecurityElement]::Escape((Join-Path $repoRoot $project))
        # Import the real project and query evaluated Link metadata without compiling or linking anything.
        # A .vcxproj wrapper preserves the extension conditions used by Directory.Build.targets.
        @"
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="$nativeProject" />
  <ItemGroup><Link Include="RedXeLinkConfigurationProbe" /></ItemGroup>
  <Target Name="ReadLinkConfiguration">
    <Message Importance="High" Text="REDXE_LINK_MODE=`$(WholeProgramOptimization)|%(Link.LinkTimeCodeGeneration)" />
  </Target>
</Project>
"@ | Set-Content -LiteralPath $projectPath -Encoding utf8
        $configurations = if ($project -eq 'RedXe/RedXe.vcxproj') { @('Debug', 'Release', 'ASan Debug') } else { @('Release') }
        foreach ($configuration in $configurations) {
            foreach ($platform in @('x64', 'ARM64')) {
                $result = @(& $msbuildPath $projectPath /nologo /v:minimal /t:ReadLinkConfiguration "/p:Configuration=$configuration" "/p:Platform=$platform" "/p:DxUiRoot=$dxUiSource" 2>&1)
                if ($LASTEXITCODE -ne 0) {
                    throw "Could not evaluate $project ($platform $configuration): $($result -join "`n")"
                }
                $records = @($result | Where-Object { $_ -match 'REDXE_LINK_MODE=' })
                if ($records.Count -ne 1 -or $records[0] -notmatch 'REDXE_LINK_MODE=([^|]*)\|(\S*)\s*$') {
                    throw "Missing evaluated link configuration for $project ($platform $configuration)."
                }
                $optimization = $matches[1]
                $mode = $matches[2]
                if ($configuration -eq 'Release') {
                    if ($mode -eq 'UseFastLinkTimeCodeGeneration' -or
                        ($optimization -in @('true', 'FASTLTCG') -and $mode -ne 'UseLinkTimeCodeGeneration')) {
                        throw "$project ($platform Release) permits stale incremental-LTCG code: $mode."
                    }
                }
                elseif ($mode -notin @('', 'Default')) {
                    throw "$project ($platform $configuration) unexpectedly enables Release link-time code generation: $mode."
                }
                ++$checked
            }
        }
    }
    Write-Host "Native link configuration: $checked evaluations pass; optimized Release uses full LTCG." -ForegroundColor Green
}
finally {
    $resolvedCleanupTarget = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedCleanupTarget.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing cleanup outside '$testParent': $resolvedCleanupTarget"
    }
    Remove-Item -LiteralPath $resolvedCleanupTarget -Recurse -Force -ErrorAction Stop
}
exit 0
