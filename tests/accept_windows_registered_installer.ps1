param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $Root 'installer\windows_registry_policy.ps1')
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
foreach ($required in @('rbl-studio-wx.exe', 'rbl-studio-worker.exe', 'THIRD_PARTY_NOTICES.txt', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $required) -PathType Leaf)) {
        throw "Acceptance requires an existing complete BuildDir; missing: $required"
    }
}
$sourceChecksum = (Get-Content -LiteralPath (Join-Path $BuildDir 'source/Rust-worker-source.sha256') -Raw).Trim()
if ($sourceChecksum -notmatch '^([0-9a-fA-F]{64})  Rust-worker-source\.tar\.zst$') { throw 'Rust source checksum manifest is malformed.' }
if ((Get-FileHash -LiteralPath (Join-Path $BuildDir 'source/Rust-worker-source.tar.zst') -Algorithm SHA256).Hash -ine $Matches[1]) { throw 'Rust source archive checksum mismatch.' }

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
    & reg.exe export "$($RegistryRootInfo.RegExePath)\.rbl" $Snapshot /y | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Snapshot -PathType Leaf)) { throw 'Could not export isolated acceptance registry snapshot.' }
    Set-ItemProperty -LiteralPath $ExtensionKey -Name 'AcceptanceMarker' -Value 'after-snapshot' -Force
    Assert-RblRegistrySnapshotRoot $Snapshot "$($RegistryRootInfo.RegExePath)\.rbl"
    & reg.exe import $Snapshot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not restore isolated acceptance registry snapshot.' }
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

    # Force a deterministic late failure after registry writes: the shortcut
    # leaf is an ordinary directory, so the preflight reparse checks pass but
    # COM cannot save the .lnk. The installer must roll back registry ownership
    # and the copied install while preserving the user's directory.
    $RollbackAppData = Join-Path $TempRoot 'AppData Rollback'
    $RollbackPrograms = Join-Path $RollbackAppData 'Microsoft\Windows\Start Menu\Programs'
    $RollbackShortcut = Join-Path $RollbackPrograms 'RBL Studio wx.lnk'
    New-Item -ItemType Directory -Path $RollbackShortcut -Force | Out-Null
    $RollbackSentinel = Join-Path $RollbackShortcut 'preserve.txt'
    Set-Content -LiteralPath $RollbackSentinel -Value 'rollback sentinel' -NoNewline
    $env:APPDATA = $RollbackAppData
    $null = Invoke-AcceptancePowerShell $Installer $InstallOne -ExpectFailure
    Assert-RegistryDefault $OriginalDefault 'late shortcut failure registry rollback'
    if (Test-Path -LiteralPath $ProgIdKey) { throw 'Late shortcut failure left the owned ProgID behind.' }
    if (Test-Path -LiteralPath $InstallOne) { throw 'Late shortcut failure left a partial install directory.' }
    if ((Get-Content -LiteralPath $RollbackSentinel -Raw) -cne 'rollback sentinel') {
        throw 'Late shortcut failure modified the user-owned shortcut directory.'
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
