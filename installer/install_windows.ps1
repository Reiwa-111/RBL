param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\RBLStudio'),
    [switch]$NoRegister,
    [switch]$Portable,
    [string]$BuildDir,
    [string]$RegistryClassesRoot = 'HKCU:\Software\Classes'
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# Portable staging produces the archive payload, not an installation: no
# install manifest, no uninstaller, no registration and no user-specific paths.
if ($Portable) { $NoRegister = $true }
function Assert-RustWorkerSourceBundle {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $sourcePath = Join-Path $Directory 'source/Rust-worker-source.tar.zst'
    $checksumPath = Join-Path $Directory 'source/Rust-worker-source.sha256'
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or -not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        throw "Rust worker corresponding-source bundle is missing from $Directory"
    }
    $line = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    if ($line -notmatch '^([0-9a-fA-F]{64})  Rust-worker-source\.tar\.zst$') {
        throw "Rust worker source checksum manifest is malformed: $checksumPath"
    }
    $actual = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($actual -ine $Matches[1]) { throw "Rust worker source checksum mismatch in $Directory" }
}
function Assert-WindowsToolchainSourceBundle {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $sourcePath = Join-Path $Directory 'source/Windows-toolchain-source.tar.zst'
    $checksumPath = Join-Path $Directory 'source/Windows-toolchain-source.sha256'
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or -not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        throw "Windows MinGW corresponding-source bundle is missing from $Directory"
    }
    $line = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    if ($line -notmatch '^([0-9a-fA-F]{64})  Windows-toolchain-source\.tar\.zst$') {
        throw "Windows toolchain source checksum manifest is malformed: $checksumPath"
    }
    $actual = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($actual -ine $Matches[1]) { throw "Windows toolchain source checksum mismatch in $Directory" }
}
function Assert-PythonRuntimeSourceBundle {
    param([Parameter(Mandatory = $true)][string]$Directory)
    foreach ($filename in @('Python-3.14.10.tar.xz', 'python-build-standalone-20261110.tar.gz')) {
        $sourcePath = Join-Path $Directory ("source/" + $filename)
        $checksumPath = Join-Path $Directory ("source/" + $filename + '.sha256')
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or -not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
            throw "CPython corresponding-source archive is missing from ${Directory}: $filename"
        }
        $line = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
        $escapedName = [regex]::Escape($filename)
        if ($line -notmatch ('^([0-9a-fA-F]{64})  ' + $escapedName + '$')) {
            throw "CPython source checksum manifest is malformed: $checksumPath"
        }
        $actual = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        if ($actual -ine $Matches[1]) { throw "CPython corresponding-source checksum mismatch: $sourcePath" }
    }
}
. (Join-Path $PSScriptRoot 'windows_registry_policy.ps1')
$RegistryRoot = Resolve-RblRegistryClassesRoot $RegistryClassesRoot
$RegistryClassesRoot = $RegistryRoot.ProviderPath
$RegistryRelativeRoot = $RegistryRoot.RelativePath
$RegistryRegExeRoot = $RegistryRoot.RegExePath
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$InstallPathRoot = [System.IO.Path]::GetPathRoot($InstallDir)
if ($InstallDir.TrimEnd([char[]]@([char]92, [char]47)) -eq $InstallPathRoot.TrimEnd([char[]]@([char]92, [char]47))) {
    throw 'Refusing to install into a volume root.'
}
$ProgId = 'Reiwa.RBLStudio.RBL'
$ManifestName = '.rblstudio-install.json'

function Assert-NoReparsePath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $current = [System.IO.Path]::GetPathRoot($full)
    $tail = $full.Substring($current.Length)
    foreach ($part in ($tail -split '[\\/]+' | Where-Object { $_ })) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Install path contains a symlink/junction/reparse point: $current"
            }
        }
    }
}

Assert-NoReparsePath $InstallDir
$ExistingManifest = $null
if (Test-Path -LiteralPath $InstallDir -PathType Container) {
    $existingManifestPath = Join-Path $InstallDir $ManifestName
    if (-not (Test-Path -LiteralPath $existingManifestPath -PathType Leaf)) {
        throw "Refusing to overwrite a non-RBLStudio directory: $InstallDir"
    }
    Assert-NoReparsePath $existingManifestPath
    $ExistingManifest = Get-Content -LiteralPath $existingManifestPath -Raw | ConvertFrom-Json
    if ($ExistingManifest.schema -ne 1 -or $ExistingManifest.app -ne 'RBL Studio wxWidgets' -or -not $ExistingManifest.files) {
        throw 'Existing installation manifest is invalid; refusing to replace this directory.'
    }
    if ($ExistingManifest.registered) {
        $manifestRegistryRoot = if ($ExistingManifest.registry_classes_root) { [string]$ExistingManifest.registry_classes_root } else { 'HKCU:\Software\Classes' }
        $validatedManifestRoot = Resolve-RblRegistryClassesRoot $manifestRegistryRoot
        if (-not [string]::Equals($validatedManifestRoot.ProviderPath, $RegistryClassesRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'The existing installation manifest uses a different registry classes root; refusing to upgrade across registry roots.'
        }
    }
}

function Copy-Tree([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "Required payload directory is missing: $Source" }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

if (-not $BuildDir) {
    $Pointer = Join-Path $Root 'build\output\wx-ui-current-win.path'
    if (Test-Path -LiteralPath $Pointer -PathType Leaf) {
        $BuildDir = (Get-Content -LiteralPath $Pointer -Raw).Trim()
    }
}
if ($BuildDir -and -not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $Root $BuildDir
}
if (-not $BuildDir -or -not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    $BuildScript = Join-Path $Root 'tools\build_wx_ide.ps1'
    & $BuildScript
    if ($LASTEXITCODE -ne 0) { throw "wx build failed ($LASTEXITCODE)" }
    $BuildDir = (Get-Content -LiteralPath (Join-Path $Root 'build\output\wx-ui-current-win.path') -Raw).Trim()
    if ($BuildDir -and -not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $Root $BuildDir }
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
foreach ($required in @('rbl-studio-wx.exe', 'rbl-studio-worker.exe', 'THIRD_PARTY_NOTICES.txt', 'runtime/python/python.exe', 'runtime/python/rbl-python-runtime.json', 'runtime/python/UPSTREAM.md', 'runtime/python/LICENSE.txt', 'runtime/python/licenses/LICENSE.cpython.txt', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256', 'source/Windows-toolchain-source.tar.zst', 'source/Windows-toolchain-source.sha256', 'source/Python-3.14.10.tar.xz', 'source/Python-3.14.10.tar.xz.sha256', 'source/python-build-standalone-20261110.tar.gz', 'source/python-build-standalone-20261110.tar.gz.sha256')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $required) -PathType Leaf)) {
        throw "wx build payload is incomplete: $BuildDir\$required"
    }
}
Assert-RustWorkerSourceBundle -Directory $BuildDir
Assert-WindowsToolchainSourceBundle -Directory $BuildDir
Assert-PythonRuntimeSourceBundle -Directory $BuildDir

$PythonManifestPath = Join-Path $BuildDir 'runtime\python\rbl-python-runtime.json'
$PythonManifest = Get-Content -LiteralPath $PythonManifestPath -Raw | ConvertFrom-Json
if ($PythonManifest.python_version -ne '3.14.10' -or $PythonManifest.platform -ne 'windows-x86_64' -or $PythonManifest.archive_sha256 -ne '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159') {
    throw 'The staged app-local CPython manifest does not match the pinned Windows 3.14.10 runtime.'
}$BundledPython = Join-Path $BuildDir 'runtime\python\python.exe'
$PythonCommand = if ($env:RBL_PYTHON) { $env:RBL_PYTHON } else { $BundledPython }
$PythonPath = if (Test-Path -LiteralPath $PythonCommand -PathType Leaf) { $PythonCommand } else {
    $resolvedPython = Get-Command $PythonCommand -ErrorAction SilentlyContinue
    if ($resolvedPython) { $resolvedPython.Source } else { $null }
}
if (-not $PythonPath) { throw 'The build payload must include app-local CPython 3.14.10; rebuild the wx payload or set RBL_PYTHON explicitly.' }
& $PythonPath -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)'
if ($LASTEXITCODE -ne 0) { throw "Python >=3.9 is required (selected interpreter: $PythonPath)" }

$Parent = Split-Path -Parent $InstallDir
New-Item -ItemType Directory -Path $Parent -Force | Out-Null
Assert-NoReparsePath $InstallDir
$Stage = Join-Path $Parent ('.RBLStudio-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Stage -Force | Out-Null
Assert-NoReparsePath $Stage
try {
    Copy-Item -LiteralPath (Join-Path $BuildDir 'rbl-studio-wx.exe') -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $BuildDir 'rbl-studio-worker.exe') -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $Root 'RBLStudio.cmd') -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $Root 'rbl.cmd') -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $Root 'RBLStudio.vbs') -Destination $Stage
    if (-not $Portable) {
        Copy-Item -LiteralPath (Join-Path $Root 'installer/uninstall_windows.ps1') -Destination (Join-Path $Stage 'Uninstall-RBLStudio.ps1')
        Copy-Item -LiteralPath (Join-Path $Root 'installer/windows_registry_policy.ps1') -Destination $Stage
    }
    Get-ChildItem -LiteralPath $BuildDir -Filter '*.dll' -File | Copy-Item -Destination $Stage -Force
    Copy-Tree (Join-Path $Root 'tools') (Join-Path $Stage 'tools')
    Copy-Tree (Join-Path $Root 'compiler') (Join-Path $Stage 'compiler')
    Copy-Tree (Join-Path $Root 'runtime') (Join-Path $Stage 'runtime')
    Copy-Tree (Join-Path $BuildDir 'runtime/python') (Join-Path $Stage 'runtime/python')
    Copy-Tree (Join-Path $Root 'bin') (Join-Path $Stage 'bin')
    # A fresh payload stages its own runtime toolchain from the host MSYS2
    # install. Keep the repository bundle as a fallback for older payloads.
    $PayloadToolchain = Join-Path $BuildDir 'toolchain\windows-x86_64'
    if (Test-Path -LiteralPath (Join-Path $PayloadToolchain 'msys64\ucrt64\bin\as.exe') -PathType Leaf) {
        Copy-Tree $PayloadToolchain (Join-Path $Stage 'toolchain/windows-x86_64')
    } else {
        Copy-Tree (Join-Path $Root 'toolchain/windows-x86_64') (Join-Path $Stage 'toolchain/windows-x86_64')
    }
    Copy-Tree (Join-Path $Root 'tests') (Join-Path $Stage 'tests')
    Copy-Tree (Join-Path $Root 'benchmarks') (Join-Path $Stage 'benchmarks')
    Copy-Tree (Join-Path $Root 'examples') (Join-Path $Stage 'examples')
    Copy-Tree (Join-Path $Root 'docs') (Join-Path $Stage 'docs')
    Copy-Tree (Join-Path $Root 'assets') (Join-Path $Stage 'assets')
    foreach ($file in @('rbl.py', 'VERSION', 'LICENSE', 'README.md', 'RELEASE_MANIFEST.txt')) {
        Copy-Item -LiteralPath (Join-Path $Root $file) -Destination $Stage -Force
    }
    Copy-Item -LiteralPath (Join-Path $BuildDir 'THIRD_PARTY_NOTICES.txt') -Destination $Stage -Force
    if (Test-Path -LiteralPath (Join-Path $BuildDir 'licenses') -PathType Container) {
        Copy-Tree (Join-Path $BuildDir 'licenses') (Join-Path $Stage 'licenses')
    }
    Copy-Tree (Join-Path $BuildDir 'source') (Join-Path $Stage 'source')
    foreach ($marker in @('VERSION', 'rbl.py', 'rbl.cmd', 'tools/rbltool.py', 'compiler', 'runtime/python/python.exe', 'runtime/python/rbl-python-runtime.json', 'runtime/python/UPSTREAM.md', 'runtime/python/LICENSE.txt', 'runtime/python/licenses/LICENSE.cpython.txt', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256', 'source/Windows-toolchain-source.tar.zst', 'source/Windows-toolchain-source.sha256', 'source/Python-3.14.10.tar.xz', 'source/Python-3.14.10.tar.xz.sha256', 'source/python-build-standalone-20261110.tar.gz', 'source/python-build-standalone-20261110.tar.gz.sha256', 'toolchain/windows-x86_64/SOURCE_CODE.md', 'toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md', 'toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md', 'toolchain/windows-x86_64/msys64/ucrt64/rbl-toolchain-manifest.json', 'toolchain/windows-x86_64/msys64/ucrt64/bin/as.exe', 'toolchain/windows-x86_64/msys64/ucrt64/x86_64-w64-mingw32/bin/ld.exe', 'tests/run_tests.py', 'benchmarks/run_benchmarks.py')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Stage $marker))) { throw "Staged application root is missing required marker: $marker" }
    }
    Assert-RustWorkerSourceBundle -Directory $Stage
    Assert-WindowsToolchainSourceBundle -Directory $Stage
    Assert-PythonRuntimeSourceBundle -Directory $Stage

    $Previous = $null
    $ExtensionKey = "$RegistryClassesRoot\.rbl"
    $HadOwnedRegistration = [bool]($ExistingManifest -and $ExistingManifest.registered)
    $ExtensionExisted = if ($HadOwnedRegistration) { [bool]$ExistingManifest.extension_key_existed } else { Test-Path -LiteralPath $ExtensionKey }
    if ($HadOwnedRegistration) {
        $Previous = $ExistingManifest.previous_extension_default
    } elseif (-not $NoRegister -and $ExtensionExisted) {
        $Previous = (Get-Item -LiteralPath $ExtensionKey).GetValue('')
    }
    $Files = @(Get-ChildItem -LiteralPath $Stage -File -Recurse | ForEach-Object {
        $_.FullName.Substring($Stage.TrimEnd('\').Length + 1).Replace('\', '/')
    })
    $ShortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk'
    if (-not $NoRegister) { Assert-NoReparsePath $ShortcutPath }
    if (-not $NoRegister -and (Test-Path -LiteralPath $ShortcutPath)) {
        $existingShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($ShortcutPath)
        if ($existingShortcut.TargetPath -ne (Join-Path $InstallDir 'RBLStudio.vbs')) {
            throw "Refusing to overwrite unrelated Start Menu shortcut: $ShortcutPath"
        }
    }
    $ProgIdKeyPath = "$RegistryClassesRoot\$ProgId"
    $OpenWithKeyPath = "$RegistryClassesRoot\.rbl\OpenWithProgids"
    $OpenWithKeyExisted = if ($HadOwnedRegistration -and $null -ne $ExistingManifest.openwith_key_existed) {
        [bool]$ExistingManifest.openwith_key_existed
    } elseif ($HadOwnedRegistration) { $true } else { Test-Path -LiteralPath $OpenWithKeyPath }
    $ExistingProgIdKey = Test-Path -LiteralPath $ProgIdKeyPath
    $ExistingOpenWithValue = $false
    $ExistingOpenWithOwned = $false
    if (Test-Path -LiteralPath $OpenWithKeyPath) {
        $openWithReadKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$RegistryRelativeRoot\.rbl\OpenWithProgids", $false)
        if ($openWithReadKey) {
            try {
                $ExistingOpenWithValue = $openWithReadKey.GetValueNames() -contains $ProgId
                if ($ExistingOpenWithValue) {
                    $ExistingOpenWithOwned = Test-RblOpenWithValueOwnership ([string]$openWithReadKey.GetValueKind($ProgId)) ($openWithReadKey.GetValue($ProgId, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames))
                }
            }
            finally { $openWithReadKey.Dispose() }
        }
    }
    $SkipOpenWithWrite = $false
    if (-not $NoRegister -and $HadOwnedRegistration) {
        if (-not $ExistingProgIdKey) { throw 'The manifest-owned ProgID is missing; refusing to recreate or overwrite it during upgrade.' }
        $existingClass = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$RegistryRelativeRoot\$ProgId", $false)
        try {
            $existingDescription = [string]$existingClass.GetValue('')
            $existingIconKey = $existingClass.OpenSubKey('DefaultIcon', $false)
            $existingCommandKey = $existingClass.OpenSubKey('shell\open\command', $false)
            try {
                $existingIcon = if ($existingIconKey) { [string]$existingIconKey.GetValue('') } else { '' }
                $existingCommand = if ($existingCommandKey) { [string]$existingCommandKey.GetValue('') } else { '' }
            } finally {
                if ($existingCommandKey) { $existingCommandKey.Dispose() }
                if ($existingIconKey) { $existingIconKey.Dispose() }
            }
            $existingTreeOwned = Test-RblProgIdTreeOwnership (Get-RblProgIdTreeShape $existingClass)
        } finally { $existingClass.Dispose() }
        $expectedIcon = '"' + (Join-Path $InstallDir 'assets\rblstudio.ico') + '",0'
        $expectedCommand = Get-RblExpectedOpenCommand $InstallDir $env:WINDIR
        if (-not $existingTreeOwned -or -not (Test-RblProgIdOwnership $existingDescription $existingIcon $existingCommand $expectedIcon $expectedCommand)) {
            throw 'The manifest-owned ProgID has been changed or replaced; refusing to overwrite user/foreign registry state during upgrade.'
        }
        if ($ExistingOpenWithValue -and -not $ExistingOpenWithOwned) {
            throw 'The existing OpenWithProgids value is not the expected empty REG_NONE value; refusing to overwrite it.'
        }
        $SkipOpenWithWrite = -not $ExistingOpenWithValue
    } elseif (-not $NoRegister -and ($ExistingProgIdKey -or $ExistingOpenWithValue)) {
        throw 'A registry entry for the RBL Studio ProgID already exists but is not owned by this installation; refusing to overwrite it.'
    }
    if (-not $Portable) {
        $Manifest = [ordered]@{
            schema = 1
            app = 'RBL Studio wxWidgets'
            release_status = 'UNRELEASED'
            registered = ((-not $NoRegister) -or $HadOwnedRegistration)
            registry_classes_root = $RegistryClassesRoot
            prog_id = $ProgId
            extension_key_existed = $ExtensionExisted
            previous_extension_default = $Previous
            openwith_key_existed = $OpenWithKeyExisted
            shortcut_path = $ShortcutPath
            files = $Files
        }
        $Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Stage $ManifestName) -Encoding UTF8
    }

    $BackupDir = $null
    if (Test-Path -LiteralPath $InstallDir -PathType Container) {
        $BackupDir = "$InstallDir.backup-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
        if (Test-Path -LiteralPath $BackupDir) { throw "Backup path already exists: $BackupDir" }
        Move-Item -LiteralPath $InstallDir -Destination $BackupDir
    }
    try {
        Move-Item -LiteralPath $Stage -Destination $InstallDir
        $ResolvedInstallRoot = (Resolve-Path -LiteralPath $InstallDir).Path
        if ($ResolvedInstallRoot -ine $InstallDir) { throw "Installed root resolved unexpectedly: $ResolvedInstallRoot" }
        foreach ($marker in @('VERSION', 'rbl.py', 'rbl.cmd', 'tools/rbltool.py', 'compiler', 'runtime/python/python.exe', 'runtime/python/rbl-python-runtime.json', 'runtime/python/UPSTREAM.md', 'runtime/python/LICENSE.txt', 'runtime/python/licenses/LICENSE.cpython.txt', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256', 'toolchain/windows-x86_64/SOURCE_CODE.md', 'toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md', 'toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md', 'toolchain/windows-x86_64/msys64/ucrt64/rbl-toolchain-manifest.json', 'toolchain/windows-x86_64/msys64/ucrt64/bin/as.exe', 'toolchain/windows-x86_64/msys64/ucrt64/x86_64-w64-mingw32/bin/ld.exe', 'tests/run_tests.py', 'benchmarks/run_benchmarks.py')) {
            if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $marker))) { throw "Installed application root is missing required marker: $marker" }
        }
        Assert-RustWorkerSourceBundle -Directory $InstallDir
        Assert-WindowsToolchainSourceBundle -Directory $InstallDir
        Assert-PythonRuntimeSourceBundle -Directory $InstallDir
    } catch {
        if (Test-Path -LiteralPath $InstallDir -PathType Container) {
            $failedInstall = "$InstallDir.failed-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
            Move-Item -LiteralPath $InstallDir -Destination $failedInstall
        }
        if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) {
            Move-Item -LiteralPath $BackupDir -Destination $InstallDir
        }
        throw
    }

    if (-not $NoRegister) {
        $RegistryBackupDir = Join-Path $env:TEMP ('RBLStudio-reg-' + [guid]::NewGuid().ToString('N'))
        try { New-Item -ItemType Directory -Path $RegistryBackupDir -Force | Out-Null }
        catch {
            if (Test-Path -LiteralPath $InstallDir -PathType Container) { Move-Item -LiteralPath $InstallDir -Destination "$InstallDir.failed-registry-snapshot-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
            if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) { Move-Item -LiteralPath $BackupDir -Destination $InstallDir }
            throw
        }
        $ExtensionRegBackup = Join-Path $RegistryBackupDir 'extension.reg'
        $ProgIdRegBackup = Join-Path $RegistryBackupDir 'progid.reg'
        $HadExtensionRegistryKey = Test-Path -LiteralPath $ExtensionKey
        $HadProgIdRegistryKey = Test-Path -LiteralPath $ProgIdKeyPath
        $HadShortcut = Test-Path -LiteralPath $ShortcutPath -PathType Leaf
        $ShortcutBackup = Join-Path $RegistryBackupDir 'shortcut.lnk'
        if ($HadExtensionRegistryKey) {
            & reg.exe export "$RegistryRegExeRoot\.rbl" $ExtensionRegBackup /y | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force
                if (Test-Path -LiteralPath $InstallDir -PathType Container) {
                    Move-Item -LiteralPath $InstallDir -Destination "$InstallDir.failed-registry-snapshot-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
                }
                if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) { Move-Item -LiteralPath $BackupDir -Destination $InstallDir }
                throw 'Could not snapshot existing .rbl registry key; registration was not attempted.'
            }
        }
        if ($HadProgIdRegistryKey) {
            & reg.exe export "$RegistryRegExeRoot\$ProgId" $ProgIdRegBackup /y | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force
                if (Test-Path -LiteralPath $InstallDir -PathType Container) {
                    Move-Item -LiteralPath $InstallDir -Destination "$InstallDir.failed-registry-snapshot-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
                }
                if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) { Move-Item -LiteralPath $BackupDir -Destination $InstallDir }
                throw 'Could not snapshot existing RBL Studio ProgID; registration was not attempted.'
            }
        }
        if ($HadShortcut) {
            try { Copy-Item -LiteralPath $ShortcutPath -Destination $ShortcutBackup -Force }
            catch {
                Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force
                if (Test-Path -LiteralPath $InstallDir -PathType Container) { Move-Item -LiteralPath $InstallDir -Destination "$InstallDir.failed-registry-snapshot-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
                if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) { Move-Item -LiteralPath $BackupDir -Destination $InstallDir }
                throw
            }
        }
        $KeepRegistryBackup = $false
        try {
            Assert-NoReparsePath $ShortcutPath
            New-Item -ItemType Directory -Path (Split-Path -Parent $ShortcutPath) -Force | Out-Null
            # Directory creation may have raced with another process changing
            # an ancestor. Re-walk immediately before the COM shortcut write.
            Assert-NoReparsePath $ShortcutPath
            # New-Item -Force against an existing Registry provider key can
            # replace its default value. Preserve user/owned association data
            # by creating the key only when it does not already exist.
            if (-not (Test-Path -LiteralPath $ExtensionKey)) {
                New-Item -Path $ExtensionKey -Force | Out-Null
            }
            $currentExtensionDefault = if (Test-Path -LiteralPath $ExtensionKey) { (Get-Item -LiteralPath $ExtensionKey).GetValue('') } else { $null }
            if (Should-RblSetExtensionDefault $HadOwnedRegistration $currentExtensionDefault $ProgId) {
                Set-Item -Path $ExtensionKey -Value $ProgId
            }
            if (-not $SkipOpenWithWrite) {
                $OpenWithRegistryKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$RegistryRelativeRoot\.rbl\OpenWithProgids")
                try { $OpenWithRegistryKey.SetValue($ProgId, [byte[]]@(), [Microsoft.Win32.RegistryValueKind]::None) }
                finally { $OpenWithRegistryKey.Dispose() }
            }
            $ClassKey = $ProgIdKeyPath
            if (-not (Test-Path -LiteralPath $ClassKey)) {
                New-Item -Path $ClassKey -Force | Out-Null
            }
            if (-not $HadOwnedRegistration) {
                Set-Item -Path $ClassKey -Value 'RBL source file'
                $IconKey = Join-Path $ClassKey 'DefaultIcon'
                New-Item -Path $IconKey -Force | Out-Null
                Set-Item -Path $IconKey -Value "`"$InstallDir\assets\rblstudio.ico`",0"
                $OpenCommandKey = Join-Path $ClassKey 'shell\open\command'
                New-Item -Path $OpenCommandKey -Force | Out-Null
                Set-Item -Path $OpenCommandKey -Value (Get-RblExpectedOpenCommand $InstallDir $env:WINDIR)
            }
            $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($ShortcutPath)
            $shortcut.TargetPath = Join-Path $InstallDir 'RBLStudio.vbs'
            $shortcut.WorkingDirectory = $InstallDir
            $shortcut.IconLocation = "$InstallDir\assets\rblstudio.ico,0"
            $shortcut.Save()
        } catch {
            $registrationError = $_
            $rollbackFailures = [System.Collections.Generic.List[string]]::new()
            if ($HadExtensionRegistryKey) {
                try {
                    if (Test-Path -LiteralPath $ExtensionKey) { Remove-Item -LiteralPath $ExtensionKey -Recurse -Force -ErrorAction Stop }
                    Assert-RblRegistrySnapshotRoot $ExtensionRegBackup "$RegistryRegExeRoot\.rbl"
                    & reg.exe import $ExtensionRegBackup | Out-Null
                    if ($LASTEXITCODE -ne 0) { throw "reg.exe import exit code $LASTEXITCODE" }
                } catch { $rollbackFailures.Add(".rbl registry restore: $($_.Exception.Message)") }
            } else {
                try { if (Test-Path -LiteralPath $ExtensionKey) { Remove-Item -LiteralPath $ExtensionKey -Recurse -Force -ErrorAction Stop } }
                catch { $rollbackFailures.Add("removal of newly-created .rbl key: $($_.Exception.Message)") }
            }
            if ($HadProgIdRegistryKey) {
                try {
                    if (Test-Path -LiteralPath $ProgIdKeyPath) { Remove-Item -LiteralPath $ProgIdKeyPath -Recurse -Force -ErrorAction Stop }
                    Assert-RblRegistrySnapshotRoot $ProgIdRegBackup "$RegistryRegExeRoot\$ProgId"
                    & reg.exe import $ProgIdRegBackup | Out-Null
                    if ($LASTEXITCODE -ne 0) { throw "reg.exe import exit code $LASTEXITCODE" }
                } catch { $rollbackFailures.Add("ProgID registry restore: $($_.Exception.Message)") }
            } else {
                try { if (Test-Path -LiteralPath $ProgIdKeyPath) { Remove-Item -LiteralPath $ProgIdKeyPath -Recurse -Force -ErrorAction Stop } }
                catch { $rollbackFailures.Add("removal of newly-created ProgID: $($_.Exception.Message)") }
            }
            if ($HadShortcut) {
                try { Copy-Item -LiteralPath $ShortcutBackup -Destination $ShortcutPath -Force -ErrorAction Stop }
                catch { $rollbackFailures.Add("Start Menu shortcut restore: $($_.Exception.Message)") }
            } else {
                try { if (Test-Path -LiteralPath $ShortcutPath) { Remove-Item -LiteralPath $ShortcutPath -Force -ErrorAction Stop } }
                catch { $rollbackFailures.Add("removal of newly-created Start Menu shortcut: $($_.Exception.Message)") }
            }
            if ((Get-RblRollbackDisposition $rollbackFailures.ToArray()) -eq 'KeepCurrentInstallAndSnapshots') {
                $KeepRegistryBackup = $true
                $details = $rollbackFailures -join '; '
                throw "REGISTRY ROLLBACK INCOMPLETE. The new application files were deliberately left in place because registry state may still point to them. Snapshot artifacts retained at '$RegistryBackupDir'. Restore the listed registry/shortcut state manually before changing the installation directory. Original registration error: $($registrationError.Exception.Message). Rollback failures: $details"
            }
            if (Test-Path -LiteralPath $InstallDir -PathType Container) {
                $failedInstall = "$InstallDir.failed-registration-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
                Move-Item -LiteralPath $InstallDir -Destination $failedInstall
            }
            if ($BackupDir -and (Test-Path -LiteralPath $BackupDir) -and -not (Test-Path -LiteralPath $InstallDir)) {
                Move-Item -LiteralPath $BackupDir -Destination $InstallDir
            }
            throw $registrationError
        } finally {
            if (-not $KeepRegistryBackup -and (Test-Path -LiteralPath $RegistryBackupDir)) { Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force }
        }
    }
} finally {
    if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
}

Write-Host "RBL Studio wxWidgets installed to: $InstallDir"
Write-Host 'Release status: 0.8.0 package prepared for publication and NOT YET PUBLISHED. Corresponding-source archives and notices are included; formal legal sign-off remains pending.'
if ($Portable) { Write-Host 'Portable staging: no install manifest, uninstaller or file association was created.' }
elseif ($NoRegister) { Write-Host 'File association was not changed (-NoRegister).' }
else { Write-Host 'ProgID/OpenWithProgids registered for .rbl. Windows UserChoice is untouched; select RBL Studio in Default Apps if Windows keeps another default.' }
if ($BackupDir) { Write-Host "Previous installation preserved at: $BackupDir" }
