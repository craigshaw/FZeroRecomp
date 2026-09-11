[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$RomPath = $env:FZERO_ROM,
    [string]$Python
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ExpectedSha256 = 'bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2'

if ([string]::IsNullOrWhiteSpace($RomPath)) {
    throw 'Usage: tools\regenerate.ps1 -RomPath "C:\path\to\F-Zero (USA).sfc"'
}
if (-not (Test-Path -LiteralPath $RomPath -PathType Leaf)) {
    throw "ROM not found: $RomPath"
}

$RomPath = (Resolve-Path -LiteralPath $RomPath).Path
$Config = Join-Path $Root 'config\bank00.cfg'
$Emitter = Join-Path $Root 'snesrecomp\tools\v2_emit.py'
if (-not (Test-Path $Config -PathType Leaf)) {
    throw 'Missing config\bank00.cfg. Restore the tracked file before regenerating.'
}
if (-not (Test-Path $Emitter -PathType Leaf)) {
    throw 'Missing patched snesrecomp dependency. Initialize the submodules first.'
}

$actualSha256 = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $ExpectedSha256) {
    throw "Unsupported ROM SHA-256: $actualSha256`nExpected: $ExpectedSha256"
}

$candidates = @()
if (-not [string]::IsNullOrWhiteSpace($Python)) {
    if (Test-Path -LiteralPath $Python -PathType Leaf) {
        $candidates += [pscustomobject]@{
            Executable = (Resolve-Path -LiteralPath $Python).Path
            Prefix = @()
        }
    } else {
        $resolvedPython = Get-Command $Python -ErrorAction SilentlyContinue
        if ($resolvedPython) {
            $candidates += [pscustomobject]@{
                Executable = $resolvedPython.Source
                Prefix = @()
            }
        }
    }
} else {
    $resolvedPython = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $resolvedPython) {
        $resolvedPython = Get-Command python -ErrorAction SilentlyContinue
    }
    if ($resolvedPython) {
        $candidates += [pscustomobject]@{
            Executable = $resolvedPython.Source
            Prefix = @()
        }
    }
    $pyLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
    if (-not $pyLauncher) {
        $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    }
    if ($pyLauncher) {
        $candidates += [pscustomobject]@{
            Executable = $pyLauncher.Source
            Prefix = @('-3')
        }
    }
}

$PythonExecutable = $null
$PythonPrefix = @()
$versionText = $null
foreach ($candidate in $candidates) {
    $candidateExecutable = [string]$candidate.Executable
    $candidatePrefix = @($candidate.Prefix)
    $candidateVersion = $null
    $candidateExitCode = -1
    $savedErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $candidateVersion = & $candidateExecutable @candidatePrefix --version 2>&1
        $candidateExitCode = $LASTEXITCODE
    } catch {
        $candidateExitCode = -1
    } finally {
        $ErrorActionPreference = $savedErrorPreference
    }

    $candidateVersion = ($candidateVersion | Out-String).Trim()
    if ($candidateExitCode -eq 0 -and
        $candidateVersion -match '^Python (\d+)\.(\d+)(?:\.(\d+))?') {
        $major = [int]$matches[1]
        $minor = [int]$matches[2]
        if ($major -gt 3 -or ($major -eq 3 -and $minor -ge 11)) {
            $PythonExecutable = $candidateExecutable
            $PythonPrefix = $candidatePrefix
            $versionText = $candidateVersion.Substring(7)
            break
        }
    }
}
if (-not $PythonExecutable) {
    throw 'Python 3.11 or newer is required. Install Python and retry.'
}

Write-Host "Generating with Python $versionText"

& $PythonExecutable @PythonPrefix (Join-Path $Root 'snesrecomp\tools\v2_sync_funcs_h.py') `
    --cfg-dir (Join-Path $Root 'config') --out (Join-Path $Root 'config\funcs.h')
if ($LASTEXITCODE -ne 0) { throw "Function declaration generation failed: $LASTEXITCODE" }

& $PythonExecutable @PythonPrefix $Emitter `
    --rom $RomPath `
    --cfg-dir (Join-Path $Root 'config') `
    --out-dir (Join-Path $Root 'generated') `
    --cfg-roots `
    --analysis-backend python
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
