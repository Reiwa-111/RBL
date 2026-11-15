param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $Root 'installer\windows_registry_policy.ps1')
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
foreach ($required in @('rbl-studio-wx.exe', 'rbl-studio-worker.exe', 'THIRD_PARTY_NOTICES.txt', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256', 'source/Windows-toolchain-source.tar.zst', 'source/Windows-toolchain-source.sha256', 'source/Python-3.14.10.tar.xz', 'source/Python-3.14.10.tar.xz.sha256', 'source/python-build-standalone-20261110.tar.gz', 'source/python-build-standalone-20261110.tar.gz.sha256')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $required) -PathType Leaf)) {
        throw "Acceptance requires an existing complete BuildDir; missing: $required"
    }
}
$sourceChecksum = (Get-Content -LiteralPath (Join-Path $BuildDir 'source/Rust-worker-source.sha256') -Raw).Trim()
if ($sourceChecksum -notmatch '^([0-9a-fA-F]{64})  Rust-worker-source\.tar\.zst$') { throw 'Rust source checksum manifest is malformed.' }
if ((Get-FileHash -LiteralPath (Join-Path $BuildDir 'source/Rust-worker-source.tar.zst') -Algorithm SHA256).Hash -ine $Matches[1]) { throw 'Rust source archive checksum mismatch.' }
$windowsSourceChecksum = (Get-Content -LiteralPath (Join-Path $BuildDir 'source/Windows-toolchain-source.sha256') -Raw).Trim()
if ($windowsSourceChecksum -notmatch '^([0-9a-fA-F]{64})  Windows-toolchain-source\.tar\.zst$') { throw 'Windows toolchain source checksum manifest is malformed.' }
if ((Get-FileHash -LiteralPath (Join-Path $BuildDir 'source/Windows-toolchain-source.tar.zst') -Algorithm SHA256).Hash -ine $Matches[1]) { throw 'Windows toolchain source archive checksum mismatch.' }
foreach ($pythonSourceName in @('Python-3.14.10.tar.xz', 'python-build-standalone-20261110.tar.gz')) {
    $pythonSourceChecksum = (Get-Content -LiteralPath (Join-Path $BuildDir ("source/" + $pythonSourceName + '.sha256')) -Raw).Trim()
    if ($pythonSourceChecksum -notmatch ('^([0-9a-fA-F]{64})  ' + [regex]::Escape($pythonSourceName) + '$')) { throw "Python source checksum manifest is malformed: $pythonSourceName" }
    if ((Get-FileHash -LiteralPath (Join-Path $BuildDir ("source/" + $pythonSourceName)) -Algorithm SHA256).Hash -ine $Matches[1]) { throw "Python source archive checksum mismatch: $pythonSourceName" }
}

$RunId = [guid]::NewGuid().ToString('D')
$GuidSubkey = "HKCU:\Software\RBLStudioAcceptance\$RunId"
$RegistryClassesRoot = "$GuidSubkey\Classes"
$RegistryRootInfo = Resolve-RblRegistryClassesRoot $RegistryClassesRoot
if (-not [string]::Equals($RegistryRootInfo.ProviderPath, $RegistryClassesRoot, [System.StringComparison]::Ordinal)) {
    throw 'Acceptance registry root did not round-trip exactly.'
}
if (Test-Path -LiteralPath $GuidSubkey) {
    throw "Refusing to reuse an existing acceptance GUID subkey: $GuidSubkey"
}

$OriginalAppData = $env:APPDATA
$OriginalTemp = $env:TEMP
$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("RBL Installer Acceptance $RunId")
$TestAppData = Join-Path $TempRoot 'AppData With Spaces'
$InstallOne = Join-Path $TempRoot 'RBL Studio Install One'
$InstallTwo = Join-Path $TempRoot 'RBL Studio Install Two'
$Installer = Join-Path $Root 'installer\install_windows.ps1'
$Uninstaller = Join-Path $Root 'installer\uninstall_windows.ps1'
$PowerShellExe = (Get-Process -Id $PID).Path
$ProgId = 'Reiwa.RBLStudio.RBL'
$ExtensionKey = "$RegistryClassesRoot\.rbl"
$ProgIdKey = "$RegistryClassesRoot\$ProgId"
$OriginalDefault = 'Acceptance.Original.ProgId'
$ChangedDefault = 'Acceptance.User.Changed.ProgId'
$Snapshot = Join-Path $TempRoot 'registry snapshot roundtrip.reg'
if (Test-Path -LiteralPath $TempRoot) { throw "Refusing to reuse an existing temporary directory: $TempRoot" }

function Invoke-AcceptancePowerShell([string]$Script, [string]$InstallPath, [switch]$ExpectFailure, [switch]$UseManifestRoot) {
    Write-Host ("Running {0} for '{1}' (expect-failure={2}, manifest-root={3})" -f `
        [System.IO.Path]::GetFileName($Script), $InstallPath, [bool]$ExpectFailure, [bool]$UseManifestRoot)
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 wraps native stderr as ErrorRecord objects;
        # with the harness-wide Stop preference, even an expected child failure
        # can abort before its process exit code is checked.
        $ErrorActionPreference = 'Continue'
        if ([string]::Equals($Script, $Installer, [System.StringComparison]::OrdinalIgnoreCase)) {
            $output = & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $Script `
                -InstallDir $InstallPath -BuildDir $BuildDir -RegistryClassesRoot $RegistryClassesRoot 2>&1
        } elseif ($UseManifestRoot) {
            $output = & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $Script -InstallDir $InstallPath 2>&1
        } else {
            $output = & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $Script `
                -InstallDir $InstallPath -RegistryClassesRoot $RegistryClassesRoot 2>&1
        }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($ExpectFailure) {
        if ($exitCode -eq 0) { throw "Expected command to fail but it succeeded: $Script (InstallDir: $InstallPath)" }
    } elseif ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $Script (InstallDir: $InstallPath)`n$($output -join "`n")"
    }
    return ,$output
}

function Assert-RegistryDefault([string]$Expected, [string]$Name) {
    $actual = if (Test-Path -LiteralPath $ExtensionKey) { (Get-Item -LiteralPath $ExtensionKey).GetValue('') } else { $null }
    if ($actual -cne $Expected) { throw "${Name}: expected '$Expected', got '$actual'" }
}

function Invoke-AcceptanceReg([string[]]$Arguments) {
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # reg.exe may print its successful status text on stderr. Keep the
        # harness's Stop preference, but decide success from the process code.
        $ErrorActionPreference = 'Continue'
        $output = & reg.exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "reg.exe failed ($exitCode): $($output -join "`n")"
    }
}

function Remove-AcceptanceJunction([string]$Path) {
    & cmd.exe /d /c "rmdir `"$Path`"" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not remove acceptance junction: $Path" }
}

try {
    New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $TestAppData -Force | Out-Null
    $env:APPDATA = $TestAppData
    $env:TEMP = $TempRoot

    if (-not (Test-Path -LiteralPath 'HKCU:\Software\RBLStudioAcceptance')) {
        New-Item -Path 'HKCU:\Software\RBLStudioAcceptance' -Force | Out-Null
    }
    New-Item -Path $GuidSubkey -Force | Out-Null
    New-Item -Path $RegistryClassesRoot -Force | Out-Null

    # Prove that export/import round-trips only the isolated acceptance hive.
    New-Item -Path $ExtensionKey -Force | Out-Null
    Set-Item -Path $ExtensionKey -Value $OriginalDefault
    New-ItemProperty -LiteralPath $ExtensionKey -Name 'AcceptanceMarker' -Value 'before-snapshot' -PropertyType String -Force | Out-Null
    Invoke-AcceptanceReg @('export', "$($RegistryRootInfo.RegExePath)\.rbl", $Snapshot, '/y')
    if (-not (Test-Path -LiteralPath $Snapshot -PathType Leaf)) { throw 'Could not export isolated acceptance registry snapshot.' }
    Set-ItemProperty -LiteralPath $ExtensionKey -Name 'AcceptanceMarker' -Value 'after-snapshot' -Force
    Assert-RblRegistrySnapshotRoot $Snapshot "$($RegistryRootInfo.RegExePath)\.rbl"
    Invoke-AcceptanceReg @('import', $Snapshot)
    Assert-RegistryDefault $OriginalDefault 'snapshot restore default'
    $marker = (Get-Item -LiteralPath $ExtensionKey).GetValue('AcceptanceMarker')
    if ($marker -cne 'before-snapshot') { throw "Snapshot restore did not restore marker; got '$marker'." }

    # A foreign ProgID must make registered install fail without replacing it.
    New-Item -Path $ProgIdKey -Force | Out-Null
    Set-Item -Path $ProgIdKey -Value 'Foreign owner'
    $null = Invoke-AcceptancePowerShell $Installer $InstallOne -ExpectFailure
    $foreignDescription = (Get-Item -LiteralPath $ProgIdKey).GetValue('')
    if ($foreignDescription -cne 'Foreign owner') { throw 'Foreign ProgID was modified by the refused installation.' }
    Assert-RegistryDefault $OriginalDefault 'foreign ProgID refusal'
    Remove-Item -LiteralPath $ProgIdKey -Recurse -Force

    # A reparse-point Start Menu parent must be rejected before the installer
    # can write a shortcut through it or mutate the isolated association.
    $ReparseAppData = Join-Path $TempRoot 'AppData Reparse'
    $ReparseOutside = Join-Path $TempRoot 'Outside Programs'
    $ReparsePrograms = Join-Path $ReparseAppData 'Microsoft\Windows\Start Menu\Programs'
    New-Item -ItemType Directory -Path (Split-Path -Parent $ReparsePrograms) -Force | Out-Null
    New-Item -ItemType Directory -Path $ReparseOutside -Force | Out-Null
    $OutsideSentinel = Join-Path $ReparseOutside 'do-not-touch.txt'
    Set-Content -LiteralPath $OutsideSentinel -Value 'outside sentinel' -NoNewline
    New-Item -ItemType Junction -Path $ReparsePrograms -Target $ReparseOutside | Out-Null
    $env:APPDATA = $ReparseAppData
    $null = Invoke-AcceptancePowerShell $Installer $InstallOne -ExpectFailure
    if (Test-Path -LiteralPath $InstallOne) { throw 'Installer left a partial installation after shortcut-parent reparse refusal.' }
    if ((Get-Content -LiteralPath $OutsideSentinel -Raw) -cne 'outside sentinel') {
        throw 'Installer modified a file behind the Start Menu reparse point.'
    }
    Assert-RegistryDefault $OriginalDefault 'shortcut-parent reparse refusal'
    Remove-AcceptanceJunction $ReparsePrograms
    $env:APPDATA = $TestAppData

    # Force a deterministic late failure after registry writes. The shortcut
    # path is absent during preflight, but its existing parent denies FILE_ADD_FILE
    # to the current user, so COM Save fails only after registration has begun.
    $RollbackAppData = Join-Path $TempRoot 'AppData Rollback'
    $RollbackPrograms = Join-Path $RollbackAppData 'Microsoft\Windows\Start Menu\Programs'
    $RollbackShortcut = Join-Path $RollbackPrograms 'RBL Studio wx.lnk'
    New-Item -ItemType Directory -Path $RollbackPrograms -Force | Out-Null
    $RollbackSentinel = Join-Path $RollbackPrograms 'preserve.txt'
    Set-Content -LiteralPath $RollbackSentinel -Value 'rollback sentinel' -NoNewline
    $CurrentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $RollbackPrograms /deny "*$($CurrentSid):(WD)" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not configure deterministic shortcut-save denial fixture.' }
    $env:APPDATA = $RollbackAppData
    try {
        $lateFailureOutput = Invoke-AcceptancePowerShell $Installer $InstallOne -ExpectFailure
        $lateFailureText = $lateFailureOutput -join "`n"
        if ($lateFailureText -notmatch '(?i)(access is denied|access denied|unauthorizedaccess|denied)') {
            throw "Expected COM shortcut-save access denial after registration writes; got: $lateFailureText"
        }
        if ($lateFailureText -notmatch '(?i)shortcut\.Save\(\)|install_windows\.ps1: line 344') {
            throw "Expected the late failure to originate at the installer COM shortcut Save call; got: $lateFailureText"
        }
        Assert-RegistryDefault $OriginalDefault 'late shortcut failure registry rollback'
        $restoredMarker = if (Test-Path -LiteralPath $ExtensionKey) { (Get-Item -LiteralPath $ExtensionKey).GetValue('AcceptanceMarker') } else { $null }
        if ($restoredMarker -cne 'before-snapshot') {
            throw "Late shortcut failure did not restore the pre-existing extension subtree marker; got '$restoredMarker'."
        }
        $OpenWithKey = "$ExtensionKey\OpenWithProgids"
        if (Test-Path -LiteralPath $OpenWithKey) {
            $openWithValue = (Get-Item -LiteralPath $OpenWithKey).GetValue($ProgId)
            if ($null -ne $openWithValue) { throw 'Late shortcut failure left the RBL ProgID in OpenWithProgids.' }
        }
        if (Test-Path -LiteralPath $ProgIdKey) { throw 'Late shortcut failure left the owned ProgID behind.' }
        if (Test-Path -LiteralPath $InstallOne) { throw 'Late shortcut failure left a partial install directory.' }
        if ((Get-Content -LiteralPath $RollbackSentinel -Raw) -cne 'rollback sentinel') {
            throw 'Late shortcut failure modified the user-owned shortcut directory.'
        }
    } finally {
        & icacls.exe $RollbackPrograms /remove:d "*$($CurrentSid)" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Could not remove the shortcut-save denial fixture ACL.' }
    }
    $env:APPDATA = $TestAppData

    # Normal install -> upgrade -> uninstall restores the pre-existing default.
    $null = Invoke-AcceptancePowerShell $Installer $InstallOne
    $manifestPath = Join-Path $InstallOne '.rblstudio-install.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.registry_classes_root -cne $RegistryClassesRoot) { throw 'Installer did not persist the validated registry root.' }
    $expectedShortcut = Join-Path $TestAppData 'Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk'
    if ($manifest.shortcut_path -cne $expectedShortcut) { throw 'Installer did not isolate its Start Menu shortcut under the temporary APPDATA.' }
    Assert-RegistryDefault $ProgId 'initial registered install'
    $installedWindowsSource = Join-Path $InstallOne 'source/Windows-toolchain-source.tar.zst'
    $installedWindowsSourceChecksum = (Get-Content -LiteralPath (Join-Path $InstallOne 'source/Windows-toolchain-source.sha256') -Raw).Trim()
    if ($installedWindowsSourceChecksum -notmatch '^([0-9a-fA-F]{64})  Windows-toolchain-source\.tar\.zst$') { throw 'Installed Windows source checksum manifest is malformed.' }
    if ((Get-FileHash -LiteralPath $installedWindowsSource -Algorithm SHA256).Hash -ine $Matches[1]) { throw 'Installed Windows source archive checksum mismatch.' }
    foreach ($pythonSourceName in @('Python-3.14.10.tar.xz', 'python-build-standalone-20261110.tar.gz')) {
        $installedPythonSource = Join-Path $InstallOne ("source/" + $pythonSourceName)
        $installedPythonChecksum = (Get-Content -LiteralPath (Join-Path $InstallOne ("source/" + $pythonSourceName + '.sha256')) -Raw).Trim()
        if ($installedPythonChecksum -notmatch ('^([0-9a-fA-F]{64})  ' + [regex]::Escape($pythonSourceName) + '$')) { throw "Installed Python source checksum manifest is malformed: $pythonSourceName" }
        if ((Get-FileHash -LiteralPath $installedPythonSource -Algorithm SHA256).Hash -ine $Matches[1]) { throw "Installed Python source checksum mismatch: $pythonSourceName" }
    }
    $savedPath = $env:PATH
    $savedRblPython = $env:RBL_PYTHON
    $savedToolchain = $env:RBL_TOOLCHAIN
    $savedNoBuildCache = $env:RBL_NO_BUILD_CACHE
    $savedMingw = $env:RBL_MINGW
    try {
        $mingwPath = Join-Path $InstallOne 'toolchain\windows-x86_64\msys64\ucrt64\bin'
        $appPython = Join-Path $InstallOne 'runtime\python\python.exe'
        if (-not (Test-Path -LiteralPath $appPython -PathType Leaf)) { throw 'Installed app-local CPython is missing.' }
        $env:PATH = "$env:SystemRoot\System32;$mingwPath"
        Remove-Item Env:RBL_PYTHON -ErrorAction SilentlyContinue
        $env:RBL_TOOLCHAIN = 'native'
        $env:RBL_NO_BUILD_CACHE = '1'
        $env:RBL_MINGW = $mingwPath
        & $appPython -V
        if ($LASTEXITCODE -ne 0) { throw 'Installed app-local CPython failed to start.' }
        $acceptanceLog = Join-Path $PSScriptRoot '..\build\verification\2026-11-12-clean-machine\windows_installed_cli.log'
        New-Item -ItemType Directory -Force -Path (Split-Path $acceptanceLog) | Out-Null
        $acceptanceErrorLog = Join-Path (Split-Path $acceptanceLog) 'windows_installed_cli.stderr.log'
        $acceptanceScript = Join-Path $InstallOne 'tests\installed_cli_acceptance.py'
        $acceptanceProcess = Start-Process -FilePath $appPython -ArgumentList @("`"$acceptanceScript`"", "`"$InstallOne`"") -WorkingDirectory $InstallOne -Wait -PassThru -NoNewWindow -RedirectStandardOutput $acceptanceLog -RedirectStandardError $acceptanceErrorLog
        if ($acceptanceProcess.ExitCode -ne 0) {
            Write-Output '--- installed CLI stdout ---'
            if (Test-Path -LiteralPath $acceptanceLog) { Get-Content -LiteralPath $acceptanceLog }
            Write-Output '--- installed CLI stderr ---'
            if (Test-Path -LiteralPath $acceptanceErrorLog) { Get-Content -LiteralPath $acceptanceErrorLog }
            throw "Installed CLI/worker acceptance failed ($($acceptanceProcess.ExitCode)); logs: $acceptanceLog, $acceptanceErrorLog"
        }
        $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
        & $cmdExe /d /c "`"$InstallOne\rbl.cmd`" version"
        if ($LASTEXITCODE -ne 0) { throw 'Installed rbl.cmd version wrapper failed with clean PATH.' }
        & $cmdExe /d /c "`"$InstallOne\rbl.cmd`" run `"$InstallOne\examples\test.rbl`""
        if ($LASTEXITCODE -ne 0) { throw 'Installed rbl.cmd run wrapper failed with clean PATH.' }
        & $cmdExe /d /c "`"$InstallOne\rbl.cmd`" clean"
        if ($LASTEXITCODE -ne 0) { throw 'Installed rbl.cmd clean wrapper failed with clean PATH.' }
    } finally {
        $env:PATH = $savedPath
        if ($null -ne $savedRblPython) { $env:RBL_PYTHON = $savedRblPython } else { Remove-Item Env:RBL_PYTHON -ErrorAction SilentlyContinue }
        if ($null -ne $savedToolchain) { $env:RBL_TOOLCHAIN = $savedToolchain } else { Remove-Item Env:RBL_TOOLCHAIN -ErrorAction SilentlyContinue }
        if ($null -ne $savedNoBuildCache) { $env:RBL_NO_BUILD_CACHE = $savedNoBuildCache } else { Remove-Item Env:RBL_NO_BUILD_CACHE -ErrorAction SilentlyContinue }
        if ($null -ne $savedMingw) { $env:RBL_MINGW = $savedMingw } else { Remove-Item Env:RBL_MINGW -ErrorAction SilentlyContinue }
    }
    $null = Invoke-AcceptancePowerShell $Installer $InstallOne
    Assert-RegistryDefault $ProgId 'registered upgrade'

    # Uninstall must fail closed if the owned shortcut's parent becomes a
    # junction after installation; the install and ProgID remain intact.
    $ProgramsPath = Join-Path $TestAppData 'Microsoft\Windows\Start Menu\Programs'
    $ProgramsBackup = Join-Path $TestAppData 'Microsoft\Windows\Start Menu\Programs Acceptance Backup'
    Move-Item -LiteralPath $ProgramsPath -Destination $ProgramsBackup
    New-Item -ItemType Junction -Path $ProgramsPath -Target $ReparseOutside | Out-Null
    $null = Invoke-AcceptancePowerShell $Uninstaller $InstallOne -ExpectFailure
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'Uninstaller removed the installation after shortcut-parent reparse refusal.'
    }
    Assert-RegistryDefault $ProgId 'shortcut-parent reparse uninstall refusal'
    if ((Get-Content -LiteralPath $OutsideSentinel -Raw) -cne 'outside sentinel') {
        throw 'Uninstaller modified a file behind the Start Menu reparse point.'
    }
    Remove-AcceptanceJunction $ProgramsPath
    Move-Item -LiteralPath $ProgramsBackup -Destination $ProgramsPath

    $manifestRaw = Get-Content -LiteralPath $manifestPath -Raw
    $tamperedManifest = $manifest | Select-Object *
    $tamperedManifest.registry_classes_root = 'HKLM:\Software\Classes'
    $tamperedManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $null = Invoke-AcceptancePowerShell $Uninstaller $InstallOne -ExpectFailure
    Assert-RegistryDefault $ProgId 'manifest-root mismatch refusal'
    Set-Content -LiteralPath $manifestPath -Value $manifestRaw -Encoding UTF8
    $null = Invoke-AcceptancePowerShell $Uninstaller $InstallOne -UseManifestRoot
    Assert-RegistryDefault $OriginalDefault 'uninstall previous-default restore'
    $restoredMarker = (Get-Item -LiteralPath $ExtensionKey).GetValue('AcceptanceMarker')
    if ($restoredMarker -cne 'before-snapshot') { throw 'Uninstall did not preserve the pre-existing .rbl registry data.' }
    if (Test-Path -LiteralPath $ProgIdKey) { throw 'Uninstall left the owned ProgID behind.' }

    # A user-changed default must survive both upgrade and uninstall.
    $null = Invoke-AcceptancePowerShell $Installer $InstallTwo
    Assert-RegistryDefault $ProgId 'second registered install'
    Set-Item -Path $ExtensionKey -Value $ChangedDefault
    $null = Invoke-AcceptancePowerShell $Installer $InstallTwo
    Assert-RegistryDefault $ChangedDefault 'upgrade preserves user-changed default'
    $null = Invoke-AcceptancePowerShell $Uninstaller $InstallTwo
    Assert-RegistryDefault $ChangedDefault 'uninstall preserves user-changed default'

    Write-Host "Registered installer acceptance checks passed in isolated root: $RegistryClassesRoot"
} finally {
    $env:APPDATA = $OriginalAppData
    $env:TEMP = $OriginalTemp
    # Remove only this run's GUID subtree; never remove the shared parent key.
    if (Test-Path -LiteralPath $GuidSubkey) { Remove-Item -LiteralPath $GuidSubkey -Recurse -Force }
    if (Test-Path -LiteralPath $TempRoot) { Remove-Item -LiteralPath $TempRoot -Recurse -Force }
}
