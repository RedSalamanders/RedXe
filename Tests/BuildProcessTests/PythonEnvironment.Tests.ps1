<# Exercises validation with divergent configured Python and launcher installations, without changing PATH. #>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture = Join-Path $repository ('.build/PythonEnvironment-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
try {
    $configured = Join-Path $fixture 'configured.ps1'
    $launcher = Join-Path $fixture 'launcher.ps1'
    [IO.File]::WriteAllText($configured, '[IO.File]::WriteAllText($PSCommandPath + ".json", (ConvertTo-Json -InputObject (@("configured") + $args) -Compress)); $global:LASTEXITCODE = 0')
    [IO.File]::WriteAllText($launcher, '[IO.File]::WriteAllText($PSCommandPath + ".json", (ConvertTo-Json -InputObject (@("launcher") + $args) -Compress)); $global:LASTEXITCODE = 0')
    foreach ($selection in @('configured', 'missing', 'store-placeholder')) {
        & {
            param($repository, $configured, $launcher, $selection)
            function Get-Command {
                param([string]$Name, $ErrorAction)
                if ($Name -eq 'python.exe') {
                    if ($selection -eq 'configured') { [pscustomobject]@{Source = $configured} }
                    elseif ($selection -eq 'store-placeholder') { [pscustomobject]@{Source = 'C:\Users\Fixture\AppData\Local\Microsoft\WindowsApps\python.exe'} }
                } elseif ($Name -eq 'py.exe') { [pscustomobject]@{Source = $launcher} }
                else { Microsoft.PowerShell.Core\Get-Command @PSBoundParameters }
            }
            & (Join-Path $repository 'validate-skills.ps1')
            $invoked = if ([IO.File]::Exists($configured + '.json')) { $configured } else { $launcher }
            $invocation = [IO.File]::ReadAllText($invoked + '.json') | ConvertFrom-Json
            [IO.File]::Delete($invoked + '.json')
            $validator = [IO.Path]::Combine($repository, 'Build', 'validate_skills.py')
            $expected = if ($selection -eq 'configured') { @('configured', $validator) }
                else { @('launcher', '-3', $validator) }
            if (($invocation -join '|') -ne ($expected -join '|')) {
                throw "Wrong validator Python invocation: $($invocation -join ' ')"
            }
        } $repository $configured $launcher $selection
        Write-Host "PASS Python environment: $selection"
    }
} finally {
    # This fixture contains only the two scripts created above; avoid recursive cleanup.
    foreach ($path in @($configured, $launcher, ($configured + '.json'), ($launcher + '.json'))) {
        if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
    }
    [IO.Directory]::Delete($fixture)
}
