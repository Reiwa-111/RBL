$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $HOME '.cargo' }
$RegistrySources = Join-Path $CargoHome 'registry\src'
if (-not (Test-Path $RegistrySources)) {
    throw "Cargo registry sources were not found: $RegistrySources"
}

$RequiredLibraries = @(
    'libwindows.0.52.0.a',
    'libwinapi_gdi32.a',
    'libwinapi_kernel32.a',
    'libwinapi_msimg32.a',
    'libwinapi_opengl32.a',
    'libwinapi_user32.a',
    'libwinapi_winspool.a'
)

$PackageDirectories = Get-ChildItem $RegistrySources -Directory | ForEach-Object {
    Get-ChildItem $_.FullName -Directory | Where-Object {
        $_.Name -like 'windows_x86_64_gnu-*' -or
        $_.Name -like 'winapi-x86_64-pc-windows-gnu-*'
    }
}

$Destination = Join-Path $Root 'build\rust-link-libs'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Get-ChildItem $Destination -Filter '*.a' -File -ErrorAction SilentlyContinue | Remove-Item -Force
$Missing = @()
foreach ($Library in $RequiredLibraries) {
    $Source = $PackageDirectories | ForEach-Object {
        $Candidate = Join-Path $_.FullName (Join-Path 'lib' $Library)
        if (Test-Path $Candidate) { Get-Item $Candidate }
    } | Select-Object -First 1

    if (-not $Source) {
        $Missing += $Library
        continue
    }
    Copy-Item -LiteralPath $Source.FullName -Destination (Join-Path $Destination $Library) -Force
}

if ($Missing.Count -gt 0) {
    throw "Missing GNU import libraries in Cargo registry: $($Missing -join ', '). Run cargo build once with the Windows GNU target installed, then retry."
}

# Keep stdout machine-readable for the CMD launcher.
Write-Output ($Destination -replace '\\', '/')
