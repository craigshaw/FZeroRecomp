[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$RomPath = $env:FZERO_ROM,
    [switch]$SkipGenerate,
    [switch]$SkipSubmoduleUpdate,
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PatchedSnesrecompRevision = 'cfc70ad071e385db2d779d39f2cf04a684cff77a'
$Preset = 'windows-msvc-x64-release'

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($Arguments -join ' ')"
    }
}

function Import-VisualStudioEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere -PathType Leaf)) {
        throw 'Visual Studio Build Tools were not found. Install the C++ build tools workload and CMake tools for Windows.'
    }

    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) {
        throw 'Visual Studio C++ x64 build tools were not found.'
    }

    $vsDevCmd = Join-Path $installation.Trim() 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $vsDevCmd -PathType Leaf)) {
        throw "Visual Studio environment script not found: $vsDevCmd"
    }

    $commandLine = "call `"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    $environmentLines = & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to initialize the Visual Studio x64 developer environment.'
    }

    $developerPath = $null
    foreach ($line in $environmentLines) {
        if ($line -notmatch '^([^=]+)=(.*)$') {
            continue
        }
        $name = $matches[1]
        $value = $matches[2]
        if ($name -ieq 'PATH') {
            if ($value -match '\\VC\\Tools\\MSVC\\') {
                $developerPath = $value
            }
            continue
        }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
    if (-not $developerPath) {
        throw 'Visual Studio initialized without an x64 compiler path.'
    }
    $env:Path = $developerPath
}

if ($env:OS -ne 'Windows_NT') {
    throw 'build.ps1 is the native Windows build entry point. Use build.sh on macOS or Linux.'
}

$Git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $Git) {
    $Git = Get-Command git -ErrorAction SilentlyContinue
}
if (-not $Git) {
    throw 'Git for Windows is required.'
}

# Some non-interactive Windows shells expose git.exe without Git's Unix helper
# directory. git-submodule itself is a shell script and needs those helpers.
$GitRoot = Split-Path -Parent (Split-Path -Parent $Git.Source)
$GitUnixTools = Join-Path $GitRoot 'usr\bin'
if (Test-Path $GitUnixTools) {
    $env:Path = "$GitUnixTools;$env:Path"
}

if (-not $SkipSubmoduleUpdate) {
    Write-Host 'Initializing pinned submodules.'
    Invoke-External $Git.Source -C $Root submodule update --init --recursive
}

$RunnerCmake = Join-Path $Root 'snesrecomp\runner\runner.cmake'
$UiCmake = Join-Path $Root 'recomp-ui\recomp_ui.cmake'
if (-not (Test-Path $RunnerCmake -PathType Leaf) -or
    -not (Test-Path $UiCmake -PathType Leaf)) {
    throw 'Required submodules are missing. Run git submodule update --init --recursive.'
}

$snesrecompRevision = (& $Git.Source -C (Join-Path $Root 'snesrecomp') rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $snesrecompRevision -ne $PatchedSnesrecompRevision) {
    throw "Unexpected snesrecomp revision: $snesrecompRevision`nExpected: $PatchedSnesrecompRevision"
}

if (-not $SkipGenerate) {
    & (Join-Path $Root 'tools\regenerate.ps1') -RomPath $RomPath
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
if (-not (Test-Path (Join-Path $Root 'generated\dispatch_v2.c') -PathType Leaf)) {
    throw 'Missing generated code. Supply -RomPath or regenerate before using -SkipGenerate.'
}

Import-VisualStudioEnvironment
foreach ($tool in @('cl.exe', 'cmake.exe', 'ninja.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was not found after initializing Visual Studio. Install CMake tools for Windows."
    }
}

Write-Host 'Bootstrapping the pinned vcpkg toolchain.'
$vcpkgRoot = & (Join-Path $Root 'tools\bootstrap-vcpkg.ps1')
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    throw 'vcpkg bootstrap failed.'
}
$env:VCPKG_ROOT = $vcpkgRoot.Trim()

Write-Host "Configuring preset: $Preset"
$Cmake = (Get-Command cmake.exe).Source
if ($Fresh) {
    Invoke-External $Cmake --fresh --preset $Preset
} else {
    Invoke-External $Cmake --preset $Preset
}
Invoke-External $Cmake --build --preset $Preset

$BuildDirectory = Join-Path $Root 'build\windows-msvc-x64-release'
$Desktop = Join-Path $BuildDirectory 'fzero_recomp.exe'
foreach ($output in @($Desktop, (Join-Path $BuildDirectory 'SDL3.dll'),
                      (Join-Path $BuildDirectory 'assets\img\boxart.png'))) {
    if (-not (Test-Path $output -PathType Leaf)) {
        throw "Expected build output was not produced: $output"
    }
}
Write-Host "Built: $Desktop"
exit 0
