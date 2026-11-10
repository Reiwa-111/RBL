$ErrorActionPreference = 'Stop'

$Target = if ($env:RUSTUP_HOME) { $env:RUSTUP_HOME } else { Join-Path $HOME '.rustup' }
$Toolchain = $env:RUSTUP_TOOLCHAIN
if (-not (Test-Path -LiteralPath $Target)) {
    throw "Rustup home was not found: $Target"
}
if (-not $Toolchain) {
    throw 'RUSTUP_TOOLCHAIN must be selected before preparing the linker-safe Rustup home.'
}

if ($Target -notmatch '[^\x00-\x7F]') {
    if (-not (Test-Path (Join-Path $Target "toolchains\$Toolchain\bin\rustc.exe"))) {
        throw "The selected toolchain '$Toolchain' is not installed under '$Target'."
    }
    Write-Output ($Target -replace '\\', '/')
    exit 0
}

# GNU ld on Windows may fail to consume rustc's Unicode sysroot paths. A
# junction does not solve this because rustc canonicalizes it to the target.
# Use a small, independent Rustup home on an ASCII path instead.
$Drive = [IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $Target).Path)
$Candidates = @(
    (Join-Path $Drive 'Rustup'),
    (Join-Path $Drive 'RBLRustupHome'),
    (Join-Path $Drive 'RBLRustup')
)

foreach ($Destination in $Candidates) {
    if (Test-Path -LiteralPath $Destination) {
        $Existing = Get-Item -LiteralPath $Destination -Force
        if ($Existing.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if (Test-Path (Join-Path $Destination "toolchains\$Toolchain\bin\rustc.exe")) {
            Write-Output ($Destination -replace '\\', '/')
            exit 0
        }
        continue
    }

    try {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        $PreviousRustupHome = $env:RUSTUP_HOME
        $PreviousToolchain = $env:RUSTUP_TOOLCHAIN
        $PreviousUpdateCheck = $env:RUSTUP_NO_UPDATE_CHECK
        $env:RUSTUP_HOME = $Destination
        $env:RUSTUP_NO_UPDATE_CHECK = '1'
        Remove-Item Env:RUSTUP_TOOLCHAIN -ErrorAction SilentlyContinue
        try {
            $Rustup = Get-Command rustup -ErrorAction Stop
            & $Rustup.Source toolchain install $Toolchain --profile minimal | Out-Host
            if ($LASTEXITCODE -ne 0) {
                throw "rustup toolchain install failed with exit code $LASTEXITCODE"
            }
        } finally {
            if ($null -eq $PreviousRustupHome) {
                Remove-Item Env:RUSTUP_HOME -ErrorAction SilentlyContinue
            } else {
                $env:RUSTUP_HOME = $PreviousRustupHome
            }
            $env:RUSTUP_TOOLCHAIN = $PreviousToolchain
            if ($null -eq $PreviousUpdateCheck) {
                Remove-Item Env:RUSTUP_NO_UPDATE_CHECK -ErrorAction SilentlyContinue
            } else {
                $env:RUSTUP_NO_UPDATE_CHECK = $PreviousUpdateCheck
            }
        }
        if (Test-Path (Join-Path $Destination "toolchains\$Toolchain\bin\rustc.exe")) {
            Write-Output ($Destination -replace '\\', '/')
            exit 0
        }
        throw "The isolated Rustup home did not receive toolchain '$Toolchain'."
    } catch {
        Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
}

throw "Could not prepare an ASCII Rustup home for '$Target'. Use an ASCII Rustup path or the native MSVC toolchain."
