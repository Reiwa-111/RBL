param(
    [string]$BuildDir = "build/output/wx-ui-win",
    [switch]$IgnoreRepoToolchain,
    [switch]$StageToolchainOnly
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$msysRoot = "C:\msys64\ucrt64"
$msysBin = Join-Path $msysRoot "bin"
$outputDir = Join-Path $root "build/output"
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
}
$workerTargetName = [System.IO.Path]::GetFileName($buildPath.TrimEnd('\','/'))
$workerTargetPath = Join-Path $outputDir "cargo-target-$workerTargetName"

# The pinned repository bundle is the 0.8.0 release input and wins when present.
# Otherwise stage the minimal as/ld runtime closure from the host MSYS2 install
# into build/output, so a source checkout does not need tracked tool binaries.
# -IgnoreRepoToolchain forces the host-staged path (used to verify that the
# repository bundle is not required).
$repoToolchainRoot = Join-Path $root "toolchain/windows-x86_64"
$outputToolchainRoot = Join-Path $outputDir "toolchain/windows-x86_64"
$toolchainRoot = if ((-not $IgnoreRepoToolchain) -and (Test-Path -LiteralPath (Join-Path $repoToolchainRoot "msys64/ucrt64/bin/as.exe") -PathType Leaf)) {
    $repoToolchainRoot
} else {
    $outputToolchainRoot
}
$toolchainUcrt = Join-Path $toolchainRoot "msys64/ucrt64"
$toolchainBin = Join-Path $toolchainUcrt "bin"
$toolchainTargetBin = Join-Path $toolchainUcrt "x86_64-w64-mingw32/bin"
if (-not (Test-Path -LiteralPath (Join-Path $toolchainBin "as.exe") -PathType Leaf)) {
    $python = (Get-Command python -ErrorAction Stop).Source
    & $python (Join-Path $root "tools/package_windows_toolchain.py") --source $msysRoot --dest $toolchainUcrt
    if ($LASTEXITCODE -ne 0) { throw "Windows runtime toolchain staging failed ($LASTEXITCODE)" }
}
$pythonForCheck = (Get-Command python -ErrorAction Stop).Source
& $pythonForCheck (Join-Path $root "tools/package_windows_toolchain.py") --check --dest $toolchainUcrt
if ($LASTEXITCODE -ne 0) { throw "Windows runtime toolchain verification failed ($LASTEXITCODE)" }
if ($StageToolchainOnly) {
    Write-Host "Toolchain staging verified: $toolchainRoot"
    exit 0
}
$cmake = (Get-Command cmake -ErrorAction Stop).Source

if (-not (Test-Path (Join-Path $msysBin "g++.exe"))) {
    throw "MSYS2 UCRT64 compiler not found at $msysBin"
}
foreach ($required in @((Join-Path $toolchainBin "as.exe"),
                         (Join-Path $toolchainTargetBin "ld.exe"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Pinned Binutils executable is missing: $required"
    }
}

$env:PATH = "$msysBin;$env:PATH"
# Use the host GCC frontend (16.2.0-4) but resolve assembler/linker from the
# pinned/staged Binutils 2.47-4 overlay. COMPILER_PATH is inherited by both
# CMake and Cargo's GNU linker invocation.
$repoCompilerPaths = "$(($toolchainBin -replace '\\','/'));$(($toolchainTargetBin -replace '\\','/'))"
$env:COMPILER_PATH = if ($env:COMPILER_PATH) {
    "$repoCompilerPaths;$env:COMPILER_PATH"
} else {
    $repoCompilerPaths
}
$gcc = Join-Path $msysBin "gcc.exe"
$expectedAssembler = [System.IO.Path]::GetFullPath((Join-Path $toolchainBin "as.exe"))
$expectedLinker = [System.IO.Path]::GetFullPath((Join-Path $toolchainTargetBin "ld.exe"))
$resolvedAssembler = [System.IO.Path]::GetFullPath((& $gcc -print-prog-name=as).Trim())
$resolvedLinker = [System.IO.Path]::GetFullPath((& $gcc -print-prog-name=ld).Trim())
if ($resolvedAssembler -ine $expectedAssembler -or $resolvedLinker -ine $expectedLinker) {
    throw "GCC did not resolve the pinned Binutils overlay. as=$resolvedAssembler ld=$resolvedLinker"
}
$env:RUSTUP_TOOLCHAIN = "stable-x86_64-pc-windows-gnu"
$env:RUSTUP_HOME = "C:\Rustup"
& (Join-Path $root "tools/prepare_windows_rust_link.ps1") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Windows Rust import-library staging failed ($LASTEXITCODE)" }
$env:RUSTFLAGS = "-L native=$($root.Replace('\','/'))/build/output/rust-link-libs"

& $cmake -S (Join-Path $root "rbl-ide/wx-ui") -B $buildPath `
    -G "MinGW Makefiles" `
    -DCMAKE_MAKE_PROGRAM="$msysBin/mingw32-make.exe" `
    -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

& $cmake --build $buildPath --parallel
if ($LASTEXITCODE -ne 0) { throw "GUI build failed ($LASTEXITCODE)" }

Push-Location $root
try {
    # Capture native streams outside PowerShell's error pipeline. Quote the
    # target-dir argument explicitly because Start-Process joins ArgumentList.
    $cargoStdout = Join-Path $env:TEMP ("rbl-worker-build-$PID.stdout.log")
    $cargoStderr = Join-Path $env:TEMP ("rbl-worker-build-$PID.stderr.log")
    try {
        $cargoExe = (Get-Command cargo -ErrorAction Stop).Source
        $cargoArguments = 'build --locked --release --bin rbl-studio-worker --target x86_64-pc-windows-gnu --target-dir "{0}"' -f $workerTargetPath
        $cargoProcess = Start-Process -FilePath $cargoExe -ArgumentList $cargoArguments `
            -WorkingDirectory $root -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $cargoStdout -RedirectStandardError $cargoStderr
        if (Test-Path -LiteralPath $cargoStdout) { Get-Content -LiteralPath $cargoStdout | ForEach-Object { Write-Output $_ } }
        if (Test-Path -LiteralPath $cargoStderr) { Get-Content -LiteralPath $cargoStderr | ForEach-Object { Write-Output $_ } }
        if ($cargoProcess.ExitCode -ne 0) { throw "Rust worker build failed ($($cargoProcess.ExitCode))" }
    } finally {
        Remove-Item -LiteralPath $cargoStdout, $cargoStderr -Force -ErrorAction SilentlyContinue
    }
} finally {
    Pop-Location
}

Copy-Item -LiteralPath (Join-Path $workerTargetPath "x86_64-pc-windows-gnu/release/rbl-studio-worker.exe") `
    -Destination (Join-Path $buildPath "rbl-studio-worker.exe") -Force

# wxWidgets and its MinGW runtime dependencies; copied beside the executable
# so the portable build does not require MSYS2 on the user's PATH.
$runtimeDlls = @(
    "libdeflate.dll", "libgcc_s_seh-1.dll", "libjbig-0.dll", "libjpeg-8.dll",
    "libLerc.dll", "liblzma-5.dll", "libpcre2-16-0.dll", "libpng16-16.dll",
    "libsharpyuv-0.dll", "libstdc++-6.dll", "libtiff-6.dll", "libwebp-7.dll",
    "libwinpthread-1.dll", "libzstd.dll", "wxbase32u_gcc_custom.dll",
    "wxmsw32u_aui_gcc_custom.dll", "wxmsw32u_core_gcc_custom.dll",
    "wxmsw32u_stc_gcc_custom.dll"
)
foreach ($dll in $runtimeDlls) {
    $source = Join-Path $msysBin $dll
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing runtime dependency: $source" }
    Copy-Item -LiteralPath $source -Destination $buildPath -Force
}

# Use the repository-pinned, signature-verified zlib runtime rather than the
# host MSYS2 installation's independently managed zlib version.
$toolchainZlib = Join-Path $toolchainBin "zlib1.dll"
$expectedZlibHash = "aa658771fb152b095eda1614d8bde798efb92324b6f4a0c99c743cd96b67ac90"
if (-not (Test-Path -LiteralPath $toolchainZlib)) {
    throw "Pinned zlib1.dll is missing from the staged toolchain: $toolchainZlib"
}
$actualZlibHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $toolchainZlib).Hash.ToLowerInvariant()
if ($actualZlibHash -ne $expectedZlibHash) {
    throw "Pinned zlib1.dll hash mismatch (expected 1.4-2): $actualZlibHash"
}
Copy-Item -LiteralPath $toolchainZlib -Destination (Join-Path $buildPath "zlib1.dll") -Force

# Preserve the license texts for wxWidgets and the redistributable MinGW/runtime
# DLL dependencies in the portable directory.
$licenseNames = @(
    "wxWidgets3.2", "gcc", "libgcc", "libstdc++", "crt", "libwinpthread", "winpthreads", "zlib",
    "zstd", "xz", "pcre2", "libpng", "libjpeg-turbo", "libtiff",
    "libwebp", "brotli", "lerc", "jbigkit", "libdeflate"
)
$licenseSource = Join-Path $msysBin "..\share\licenses"
$licenseDestination = Join-Path $buildPath "licenses"
New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
foreach ($licenseName in $licenseNames) {
    $source = Join-Path $licenseSource $licenseName
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing third-party license directory: $source"
    }
    Copy-Item -LiteralPath $source -Destination $licenseDestination -Recurse -Force
}
$scintillaLicense = Join-Path $Root 'rbl-ide\wx-ui\licenses\Scintilla-LICENSE.txt'
if (-not (Test-Path -LiteralPath $scintillaLicense -PathType Leaf)) {
    throw "Missing Scintilla license notice: $scintillaLicense"
}
$scintillaLicenseDir = Join-Path $licenseDestination 'Scintilla'
New-Item -ItemType Directory -Path $scintillaLicenseDir -Force | Out-Null
Copy-Item -LiteralPath $scintillaLicense -Destination (Join-Path $scintillaLicenseDir 'License.txt') -Force
$rustLicenseSource = Join-Path $Root 'rbl-ide\wx-ui\licenses\Rust'
$rustNoticeSource = Join-Path $Root 'rbl-ide\wx-ui\RUST_DEPENDENCY_NOTICES.md'
if (-not (Test-Path -LiteralPath $rustLicenseSource -PathType Container)) { throw 'Missing Rust worker license payload: ' + $rustLicenseSource }
if (-not (Test-Path -LiteralPath $rustNoticeSource -PathType Leaf)) { throw 'Missing Rust worker license inventory: ' + $rustNoticeSource }
$rustLicenseDestination = Join-Path $licenseDestination 'Rust'
Copy-Item -LiteralPath $rustLicenseSource -Destination $licenseDestination -Recurse -Force
Copy-Item -LiteralPath $rustNoticeSource -Destination (Join-Path $rustLicenseDestination 'THIRD_PARTY_NOTICES.md') -Force
$pacman = Join-Path $msysBin "..\..\usr\bin\pacman.exe"
$packageIds = @(
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-common",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-common-libs",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-msw",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-msw-libs",
    "mingw-w64-ucrt-x86_64-gcc", "mingw-w64-ucrt-x86_64-libgcc",
    "mingw-w64-ucrt-x86_64-libstdc++",
    "mingw-w64-ucrt-x86_64-crt",
    "mingw-w64-ucrt-x86_64-libwinpthread", "mingw-w64-ucrt-x86_64-winpthreads",
    "mingw-w64-ucrt-x86_64-brotli", "mingw-w64-ucrt-x86_64-jbigkit",
    "mingw-w64-ucrt-x86_64-lerc", "mingw-w64-ucrt-x86_64-libdeflate",
    "mingw-w64-ucrt-x86_64-libjpeg-turbo", "mingw-w64-ucrt-x86_64-libpng",
    "mingw-w64-ucrt-x86_64-libtiff", "mingw-w64-ucrt-x86_64-libwebp",
    "mingw-w64-ucrt-x86_64-pcre2", "mingw-w64-ucrt-x86_64-xz",
    "mingw-w64-ucrt-x86_64-zstd"
)
if (-not (Test-Path -LiteralPath $pacman)) { throw "MSYS2 pacman not found: $pacman" }
$installedPackages = & $pacman -Q
if ($LASTEXITCODE -ne 0) { throw "Could not query installed MSYS2 package versions" }
$packageInventory = foreach ($packageId in $packageIds) {
    $packageRecord = $installedPackages | Where-Object {
        $_ -match "^$([regex]::Escape($packageId))\s+"
    } | Select-Object -First 1
    if (-not $packageRecord) {
        throw "Missing package metadata for redistributed dependency: $packageId"
    }
    $packageRecord
}
$packageInventory += "mingw-w64-ucrt-x86_64-zlib 1.4-2 (repository-pinned runtime DLL)"
$pythonExe = (Get-Command python.exe -ErrorAction Stop).Source
& $pythonExe (Join-Path $root "tools/package_python_runtime.py") --platform windows --output (Join-Path $buildPath "runtime/python")
if ($LASTEXITCODE -ne 0) { throw "App-local CPython packaging failed ($LASTEXITCODE)" }
$pythonRuntimeNotice = @"
App-local CPython 3.14.10 embeddable runtime is included under runtime/python.
The verified official Python.org package SHA-256 is
90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159.
The upstream LICENSE.txt and full CPython/third-party license inventory under
runtime/python/licenses are retained. See runtime/python/UPSTREAM.md.
"@

@"
RBL Studio Windows build - third-party runtime notices

This directory redistributes wxWidgets and the runtime/dependency DLLs listed
by the exact MSYS2 UCRT64 package versions below. Corresponding license texts
are preserved in the licenses subdirectory, organized by package/component.
wxWidgets uses the wxWindows Library Licence 3.1. Each bundled dependency
retains its own upstream terms; the copied license texts are not a legal review.

Package inventory (captured from pacman -Q during this build):
$($packageInventory -join "`n")

The editor uses wxStyledTextCtrl, wxWidgets' Scintilla integration. Scintilla's
upstream copyright and permission notice is preserved at
licenses/Scintilla/License.txt. No Riverbank QScintilla component is included.

The Rust worker dependency closure is listed in licenses/Rust/THIRD_PARTY_NOTICES.md;
package-specific license texts are preserved under licenses/Rust/<crate>-<version>/.

The mingw-w64-ucrt-x86_64-libstdc++ split package is covered by the same
mingw-w64-gcc package inventory. The libwinpthread and winpthreads
license directories are copied separately; their COPYING files are byte-identical.

$pythonRuntimeNotice
This inventory is not legal certification.
"@ | Set-Content -LiteralPath (Join-Path $buildPath "THIRD_PARTY_NOTICES.txt") -Encoding UTF8

# Stage the minimal runtime toolchain inside the build payload. The installers
# must be able to assemble/link RBL programs from the installed copy without a
# developer MSYS2 installation; the repository bundle is only a build input.
$payloadToolchain = Join-Path $buildPath "toolchain/windows-x86_64"
New-Item -ItemType Directory -Path $payloadToolchain -Force | Out-Null
Get-ChildItem -LiteralPath $toolchainRoot -File -Filter '*.md' | Copy-Item -Destination $payloadToolchain -Force
Copy-Item -LiteralPath (Join-Path $toolchainRoot "msys64") -Destination $payloadToolchain -Recurse -Force
foreach ($toolchainMarker in @('SOURCE_CODE.md', 'SOURCE_ARCHIVE_AUDIT.md', 'THIRD_PARTY_NOTICES.md', 'msys64/ucrt64/rbl-toolchain-manifest.json', 'msys64/ucrt64/bin/as.exe', 'msys64/ucrt64/x86_64-w64-mingw32/bin/ld.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $payloadToolchain $toolchainMarker) -PathType Leaf)) {
        throw "Staged payload toolchain is missing required marker: $toolchainMarker"
    }
}

# Publish the last successful build directory for the repository launcher.
$pointerDir = $outputDir
New-Item -ItemType Directory -Path $pointerDir -Force | Out-Null
$pointerTemp = Join-Path $pointerDir "wx-ui-current-win.path.tmp"
$pointer = Join-Path $pointerDir "wx-ui-current-win.path"
[System.IO.File]::WriteAllText($pointerTemp, [System.IO.Path]::GetFullPath($buildPath), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $pointerTemp -Destination $pointer -Force

Write-Host "Built RBL Studio: $buildPath"

