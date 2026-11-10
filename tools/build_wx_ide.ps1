param(
    [string]$BuildDir = "build/wx-ui-win"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$msysBin = "C:\msys64\ucrt64\bin"
$repoBinutilsBin = Join-Path $root "toolchain/windows-x86_64/msys64/ucrt64/bin"
$repoBinutilsTargetBin = Join-Path $root "toolchain/windows-x86_64/msys64/ucrt64/x86_64-w64-mingw32/bin"
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
}
$workerTargetName = [System.IO.Path]::GetFileName($buildPath.TrimEnd('\','/'))
$workerTargetPath = Join-Path $root "build/cargo-target-$workerTargetName"

if (-not (Test-Path (Join-Path $msysBin "g++.exe"))) {
    throw "MSYS2 UCRT64 compiler not found at $msysBin"
}
foreach ($required in @((Join-Path $repoBinutilsBin "as.exe"),
                         (Join-Path $repoBinutilsTargetBin "ld.exe"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Repository-pinned Binutils executable is missing: $required"
    }
}

$env:PATH = "$msysBin;$env:PATH"
# Use the host GCC frontend (16.2.0-4) but resolve assembler/linker from the
# repository's signature-verified Binutils 2.47-4 overlay. COMPILER_PATH is
# inherited by both CMake and Cargo's GNU linker invocation.
$repoCompilerPaths = "$(($repoBinutilsBin -replace '\\','/'));$(($repoBinutilsTargetBin -replace '\\','/'))"
$env:COMPILER_PATH = if ($env:COMPILER_PATH) {
    "$repoCompilerPaths;$env:COMPILER_PATH"
} else {
    $repoCompilerPaths
}
$gcc = Join-Path $msysBin "gcc.exe"
$expectedAssembler = [System.IO.Path]::GetFullPath((Join-Path $repoBinutilsBin "as.exe"))
$expectedLinker = [System.IO.Path]::GetFullPath((Join-Path $repoBinutilsTargetBin "ld.exe"))
$resolvedAssembler = [System.IO.Path]::GetFullPath((& $gcc -print-prog-name=as).Trim())
$resolvedLinker = [System.IO.Path]::GetFullPath((& $gcc -print-prog-name=ld).Trim())
if ($resolvedAssembler -ine $expectedAssembler -or $resolvedLinker -ine $expectedLinker) {
    throw "GCC did not resolve repository-pinned Binutils. as=$resolvedAssembler ld=$resolvedLinker"
}
$env:RUSTUP_TOOLCHAIN = "stable-x86_64-pc-windows-gnu"
$env:RUSTUP_HOME = "C:\Rustup"
$env:RUSTFLAGS = "-L native=$($root.Replace('\','/'))/build/rust-link-libs"

& $cmake -S (Join-Path $root "native/wx-ui") -B $buildPath `
    -G "MinGW Makefiles" `
    -DCMAKE_MAKE_PROGRAM="$msysBin/mingw32-make.exe" `
    -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

& $cmake --build $buildPath --parallel
if ($LASTEXITCODE -ne 0) { throw "GUI build failed ($LASTEXITCODE)" }

Push-Location $root
try {
    cargo build --release --bin rbl-studio-worker --target x86_64-pc-windows-gnu `
        --target-dir $workerTargetPath
    if ($LASTEXITCODE -ne 0) { throw "Rust worker build failed ($LASTEXITCODE)" }
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
$toolchainZlib = Join-Path $root "toolchain/windows-x86_64/msys64/ucrt64/bin/zlib1.dll"
$expectedZlibHash = "aa658771fb152b095eda1614d8bde798efb92324b6f4a0c99c743cd96b67ac90"
if (-not (Test-Path -LiteralPath $toolchainZlib)) {
    throw "Repository-pinned zlib1.dll is missing: $toolchainZlib"
}
$actualZlibHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $toolchainZlib).Hash.ToLowerInvariant()
if ($actualZlibHash -ne $expectedZlibHash) {
    throw "Repository-pinned zlib1.dll hash mismatch: $actualZlibHash"
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
$scintillaLicense = Join-Path $Root 'native\wx-ui\licenses\Scintilla-LICENSE.txt'
if (-not (Test-Path -LiteralPath $scintillaLicense -PathType Leaf)) {
    throw "Missing Scintilla license notice: $scintillaLicense"
}
$scintillaLicenseDir = Join-Path $licenseDestination 'Scintilla'
New-Item -ItemType Directory -Path $scintillaLicenseDir -Force | Out-Null
Copy-Item -LiteralPath $scintillaLicense -Destination (Join-Path $scintillaLicenseDir 'License.txt') -Force
$rustLicenseSource = Join-Path $Root 'native\wx-ui\licenses\Rust'
$rustNoticeSource = Join-Path $Root 'native\wx-ui\RUST_DEPENDENCY_NOTICES.md'
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
$sourceMaterialDir = Join-Path $root "build\source-material"
$sourceMaterialArchive = Get-ChildItem -LiteralPath $sourceMaterialDir -Filter "RBL-0.8.0-Windows-Source-Materials-*.tar.zst" -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($sourceMaterialArchive) {
    $sourceMaterialRelativePath = $sourceMaterialArchive.FullName.Substring($root.Length).TrimStart([char[]]@([char]92, [char]47)).Replace([char]92, [char]47)
    $sourceMaterialHash = (Get-FileHash -LiteralPath $sourceMaterialArchive.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceMaterialNotice = @"
A separate source-material archive is available at $sourceMaterialRelativePath
(SHA-256: $sourceMaterialHash). This archive is not copied into this portable
directory by the build script and must accompany any binary distribution.
Legal review of the selected source-distribution procedure or written offer
remains incomplete.
"@
} else {
    $sourceMaterialNotice = @"
No separate source-material archive is present under build/source-material.
Do not distribute this binary build until corresponding source materials are
provided or an applicable written offer has been legally reviewed.
"@
}
$rustSourceMaterialArchive = Get-ChildItem -LiteralPath $sourceMaterialDir -Filter "RBL-0.8.0-Rust-Worker-Source-Materials-*.tar.zst" -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $rustSourceMaterialArchive) {
    throw "Required Rust worker corresponding-source archive is missing under $sourceMaterialDir"
}
$pythonExe = (Get-Command python.exe -ErrorAction Stop).Source
$zstdExe = Join-Path $msysBin "zstd.exe"
if (-not (Test-Path -LiteralPath $zstdExe -PathType Leaf)) {
    throw "zstd is required to validate the Rust worker source archive: $zstdExe"
}
& $pythonExe (Join-Path $root "tools/verify_rust_source_bundle.py") --root $root --archive $rustSourceMaterialArchive.FullName --zstd $zstdExe
if ($LASTEXITCODE -ne 0) { throw "Rust worker source archive verification failed ($LASTEXITCODE)" }
$rustSourcePayloadDir = Join-Path $buildPath "source"
New-Item -ItemType Directory -Path $rustSourcePayloadDir -Force | Out-Null
$rustSourcePayloadName = "Rust-worker-source.tar.zst"
$rustSourcePayloadPath = Join-Path $rustSourcePayloadDir $rustSourcePayloadName
Copy-Item -LiteralPath $rustSourceMaterialArchive.FullName -Destination $rustSourcePayloadPath -Force
$rustSourceMaterialHash = (Get-FileHash -LiteralPath $rustSourcePayloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText((Join-Path $rustSourcePayloadDir "Rust-worker-source.sha256"), "$rustSourceMaterialHash  $rustSourcePayloadName`n", [System.Text.UTF8Encoding]::new($false))
$rustSourceNotice = @"
Rust worker corresponding-source archive is included at
source/$rustSourcePayloadName (SHA-256: $rustSourceMaterialHash).
The archive contains worker source, Cargo manifests and exact active crate
source archives; verify it with source/Rust-worker-source.sha256.
"@

@"
RBL Studio portable build - third-party runtime notices

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
mingw-w64-gcc source archive listed below. The libwinpthread and winpthreads
license directories are copied separately; their COPYING files are byte-identical.

$sourceMaterialNotice
$rustSourceNotice
This inventory is not legal certification.
"@ | Set-Content -LiteralPath (Join-Path $buildPath "THIRD_PARTY_NOTICES.txt") -Encoding UTF8

# Publish the last successful build directory for the repository launcher.
$pointerDir = Join-Path $root "build"
New-Item -ItemType Directory -Path $pointerDir -Force | Out-Null
$pointerTemp = Join-Path $pointerDir "wx-ui-current-win.path.tmp"
$pointer = Join-Path $pointerDir "wx-ui-current-win.path"
[System.IO.File]::WriteAllText($pointerTemp, [System.IO.Path]::GetFullPath($buildPath), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $pointerTemp -Destination $pointer -Force

Write-Host "Built portable RBL Studio prototype: $buildPath"

