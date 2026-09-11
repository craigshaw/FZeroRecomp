[CmdletBinding()]
param(
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ManifestPath = Join-Path $Root 'vcpkg.json'
if (-not (Test-Path $ManifestPath -PathType Leaf)) {
    throw "Missing vcpkg manifest: $ManifestPath"
}
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$Baseline = [string]$Manifest.'builtin-baseline'
if ($Baseline -notmatch '^[0-9a-f]{40}$') {
    throw 'vcpkg.json must declare a 40-character builtin-baseline.'
}
$VcpkgRepository = 'https://github.com/microsoft/vcpkg.git'

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $Root '.tools\vcpkg'
}
$Destination = [System.IO.Path]::GetFullPath($Destination)

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($Arguments -join ' ')"
    }
}

$Git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $Git) {
    $Git = Get-Command git -ErrorAction SilentlyContinue
}
if (-not $Git) {
    throw 'Git is required to bootstrap vcpkg. Install Git for Windows and retry.'
}

$GitRoot = Split-Path -Parent (Split-Path -Parent $Git.Source)
$GitUnixTools = Join-Path $GitRoot 'usr\bin'
if (Test-Path $GitUnixTools) {
    $env:Path = "$GitUnixTools;$env:Path"
}

$GitDirectory = Join-Path $Destination '.git'
if (-not (Test-Path $GitDirectory)) {
    if (Test-Path $Destination) {
        $existing = @(Get-ChildItem -LiteralPath $Destination -Force)
        if ($existing.Count -ne 0) {
            throw "Refusing to initialize vcpkg in non-empty directory: $Destination"
        }
    } else {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }

    Write-Host "Initializing pinned vcpkg checkout in $Destination"
    Invoke-External $Git.Source -C $Destination init
}

$remoteNames = @(& $Git.Source -C $Destination remote)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect Git remotes in $Destination"
}
if ($remoteNames -notcontains 'origin') {
    Invoke-External $Git.Source -C $Destination remote add origin $VcpkgRepository
}
$originUrl = (& $Git.Source -C $Destination remote get-url origin).Trim()
if ($LASTEXITCODE -ne 0 -or $originUrl -ne $VcpkgRepository) {
    throw "Unexpected vcpkg origin URL: $originUrl`nExpected: $VcpkgRepository"
}

$dirty = & $Git.Source -C $Destination status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect the vcpkg checkout at $Destination"
}
if ($dirty) {
    throw "Refusing to update a modified vcpkg checkout: $Destination"
}

$headFile = Join-Path $GitDirectory 'HEAD'
$current = ''
if (Test-Path $headFile -PathType Leaf) {
    $headValue = (Get-Content -LiteralPath $headFile -Raw).Trim()
    if ($headValue -notlike 'ref: *') {
        $current = $headValue
    }
}
if ($current -ne $Baseline) {
    Write-Host "Fetching pinned vcpkg baseline $Baseline"
    Invoke-External $Git.Source -C $Destination fetch --depth 1 origin $Baseline
    Invoke-External $Git.Source -C $Destination checkout --detach FETCH_HEAD
}

$Bootstrap = Join-Path $Destination 'bootstrap-vcpkg.bat'
$Vcpkg = Join-Path $Destination 'vcpkg.exe'
if (-not (Test-Path $Bootstrap)) {
    throw "Pinned vcpkg checkout is missing bootstrap-vcpkg.bat: $Destination"
}
if (-not (Test-Path $Vcpkg)) {
    Write-Host 'Bootstrapping vcpkg (telemetry disabled).'
    Invoke-External $Bootstrap -disableMetrics
}

Write-Output $Destination
