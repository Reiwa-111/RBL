param(
    [string]$InstallDir,
    [switch]$NoRegister,
    [string]$RegistryClassesRoot = 'HKCU:\Software\Classes'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'windows_registry_policy.ps1')
$RegistryRoot = Resolve-RblRegistryClassesRoot $RegistryClassesRoot
if (-not $InstallDir) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '.rblstudio-install.json') -PathType Leaf) {
        $InstallDir = $PSScriptRoot
    } else {
        $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\RBLStudio'
    }
}
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$PathRoot = [System.IO.Path]::GetPathRoot($InstallDir)
if ($InstallDir.TrimEnd([char[]]@([char]92, [char]47)) -eq $PathRoot.TrimEnd([char[]]@([char]92, [char]47))) {
    throw 'Refusing to uninstall from a volume root.'
}
$InstallDir = $InstallDir.TrimEnd([char[]]@([char]92, [char]47))
function Assert-NoReparsePath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $current = [System.IO.Path]::GetPathRoot($full)
    $tail = $full.Substring($current.Length)
    foreach ($part in ($tail -split '[\\/]+' | Where-Object { $_ })) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Uninstall path contains a symlink/junction/reparse point: $current"
            }
        }
    }
}
Assert-NoReparsePath $InstallDir
$ManifestPath = Join-Path $InstallDir '.rblstudio-install.json'
Assert-NoReparsePath $ManifestPath
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Write-Host "No RBL Studio install manifest found at: $InstallDir"
    exit 0
}
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($Manifest.schema -ne 1 -or $Manifest.app -ne 'RBL Studio wxWidgets' -or $Manifest.prog_id -ne 'Reiwa.RBLStudio.RBL') {
    throw 'Unrecognized or invalid RBL Studio install manifest.'
}
$manifestRegistryRoot = if ($Manifest.registry_classes_root) { [string]$Manifest.registry_classes_root } else { 'HKCU:\Software\Classes' }
$validatedManifestRoot = Resolve-RblRegistryClassesRoot $manifestRegistryRoot
if (-not $PSBoundParameters.ContainsKey('RegistryClassesRoot')) {
    $RegistryRoot = $validatedManifestRoot
} elseif (-not [string]::Equals($RegistryRoot.ProviderPath, $validatedManifestRoot.ProviderPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The requested registry classes root does not match the install manifest; refusing to uninstall against a different registry root.'
}
$RegistryClassesRoot = $RegistryRoot.ProviderPath
$RegistryRelativeRoot = $RegistryRoot.RelativePath
$RegistryRegExeRoot = $RegistryRoot.RegExePath
if (-not $Manifest.files -or @($Manifest.files).Count -eq 0) {
    throw 'Install manifest contains no owned-file list; refusing to uninstall.'
}
if ($NoRegister -and $Manifest.registered) {
    throw 'This install owns file associations; refusing -NoRegister because it would leave broken integration entries.'
}
$RootPrefix = $InstallDir + '\'
$SeenPaths = @{}
$OwnedDirectories = @{}
foreach ($relative in @($Manifest.files)) {
    if (-not $relative -or [System.IO.Path]::IsPathRooted([string]$relative) -or ([string]$relative -match '(^|[\\/])\.\.?([\\/]|$)|:|[\*\?]')) {
        throw "Refusing invalid manifest-relative path: $relative"
    }
    $normalizedRelative = ([string]$relative).Replace('/', '\')
    if ($SeenPaths.ContainsKey($normalizedRelative)) { throw "Duplicate manifest path: $relative" }
    $SeenPaths[$normalizedRelative] = $true
    $ownedParent = Split-Path -Parent $normalizedRelative
    while ($ownedParent) {
        $OwnedDirectories[$ownedParent] = $true
        $ownedParent = Split-Path -Parent $ownedParent
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $InstallDir $normalizedRelative))
    if (-not $candidate.StartsWith($RootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the installation directory: $relative"
    }
    Assert-NoReparsePath $candidate
}

if (-not $NoRegister -and $Manifest.registered) {
    $ProgId = [string]$Manifest.prog_id
    $ShortcutPath = [string]$Manifest.shortcut_path
    $ExpectedShortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk'
    if (-not (Test-RblOwnedShortcutPath $ShortcutPath $ExpectedShortcutPath)) {
        throw 'Refusing to inspect or snapshot a shortcut outside the exact RBL Studio-owned Start Menu path.'
    }
    Assert-NoReparsePath $ShortcutPath
    $RegistryBackupDir = Join-Path $env:TEMP ('RBLStudio-unreg-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $RegistryBackupDir -Force | Out-Null
    $ExtensionRegBackup = Join-Path $RegistryBackupDir 'extension.reg'
    $ProgIdRegBackup = Join-Path $RegistryBackupDir 'progid.reg'
    $ExtensionKey = "$RegistryClassesRoot\.rbl"
    $ProgIdKey = "$RegistryClassesRoot\$ProgId"
    $HadExtensionRegistryKey = Test-Path -LiteralPath $ExtensionKey
    $HadProgIdRegistryKey = Test-Path -LiteralPath $ProgIdKey
    if ($HadExtensionRegistryKey) {
        & reg.exe export "$RegistryRegExeRoot\.rbl" $ExtensionRegBackup /y | Out-Null
        if ($LASTEXITCODE -ne 0) { Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force; throw 'Could not snapshot .rbl registry key; uninstall integration was not changed.' }
    }
    if ($HadProgIdRegistryKey) {
        & reg.exe export "$RegistryRegExeRoot\$ProgId" $ProgIdRegBackup /y | Out-Null
        if ($LASTEXITCODE -ne 0) { Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force; throw 'Could not snapshot ProgID registry key; uninstall integration was not changed.' }
    }
    $HadShortcut = $ShortcutPath -and (Test-Path -LiteralPath $ShortcutPath -PathType Leaf)
    $ShortcutBackup = Join-Path $RegistryBackupDir 'shortcut.lnk'
    if ($HadShortcut) {
        Assert-NoReparsePath $ShortcutPath
        Copy-Item -LiteralPath $ShortcutPath -Destination $ShortcutBackup -Force
    }
    $KeepRegistryBackup = $false
    try {
    $OpenWithKey = Join-Path $ExtensionKey 'OpenWithProgids'
    $ProgIdOwned = $false
    if (Test-Path -LiteralPath $ProgIdKey) {
        $classItem = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$RegistryRelativeRoot\$ProgId", $false)
        try {
            $description = [string]$classItem.GetValue('')
            $iconKey = $classItem.OpenSubKey('DefaultIcon', $false)
            $commandKey = $classItem.OpenSubKey('shell\open\command', $false)
            try {
                $icon = if ($iconKey) { [string]$iconKey.GetValue('') } else { '' }
                $command = if ($commandKey) { [string]$commandKey.GetValue('') } else { '' }
            } finally {
                if ($commandKey) { $commandKey.Dispose() }
                if ($iconKey) { $iconKey.Dispose() }
            }
            $treeOwned = Test-RblProgIdTreeOwnership (Get-RblProgIdTreeShape $classItem)
        } finally { $classItem.Dispose() }
        $expectedIcon = '"' + (Join-Path $InstallDir 'assets\rblstudio.ico') + '",0'
        $expectedCommand = Get-RblExpectedOpenCommand $InstallDir $env:WINDIR
        $ProgIdOwned = ($treeOwned -and (Test-RblProgIdOwnership $description $icon $command $expectedIcon $expectedCommand))
        if (-not $ProgIdOwned) {
            throw 'The registered ProgID has been modified or replaced; refusing to remove integration or payload. Restore the expected RBL Studio ProgID or remove it manually after reviewing its owner.'
        }
    }
    $OpenWithOwned = $false
    $ExistingOpenWithValue = $false
    if (Test-Path -LiteralPath $OpenWithKey) {
        $openWithReadKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$RegistryRelativeRoot\.rbl\OpenWithProgids", $false)
        if ($openWithReadKey) {
            try {
                if ($openWithReadKey.GetValueNames() -contains $ProgId) {
                    $ExistingOpenWithValue = $true
                    $OpenWithOwned = Test-RblOpenWithValueOwnership ([string]$openWithReadKey.GetValueKind($ProgId)) ($openWithReadKey.GetValue($ProgId, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames))
                    if (-not $OpenWithOwned) { throw 'The OpenWithProgids value was modified; refusing to remove the value or its ProgID integration.' }
                }
            } finally { $openWithReadKey.Dispose() }
        }
    }
    $Current = if (Test-Path -LiteralPath $ExtensionKey) { (Get-Item -LiteralPath $ExtensionKey).GetValue('') } else { $null }
    if (-not (Test-RblAssociationMutationAllowed $ProgIdOwned $ExistingOpenWithValue $Current $ProgId)) {
        throw 'The ProgID key is missing or not proven owned while .rbl/OpenWithProgids still references it; refusing to mutate shared file associations. Remove or repair the association manually after verifying its owner.'
    }
    if ($OpenWithOwned) {
        Remove-ItemProperty -LiteralPath $OpenWithKey -Name $ProgId -ErrorAction Stop
        $openWithItem = Get-Item -LiteralPath $OpenWithKey
        if (-not $Manifest.openwith_key_existed -and $openWithItem.GetValueNames().Count -eq 0 -and $openWithItem.GetSubKeyNames().Count -eq 0) {
            Remove-Item -LiteralPath $OpenWithKey -Force
        }
    }
    if (Test-Path -LiteralPath $ExtensionKey) {
        if ($Current -eq $ProgId) {
            if ($Manifest.extension_key_existed -and $null -ne $Manifest.previous_extension_default) {
                Set-Item -Path $ExtensionKey -Value ([string]$Manifest.previous_extension_default)
            } else {
                $extensionRegistryKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$RegistryRelativeRoot\.rbl", $true)
                if ($extensionRegistryKey) {
                    try {
                        $extensionRegistryKey.DeleteValue('', $false)
                        if (-not $Manifest.extension_key_existed -and $extensionRegistryKey.GetValueNames().Count -eq 0 -and $extensionRegistryKey.GetSubKeyNames().Count -eq 0) {
                            $extensionRegistryKey.Dispose()
                            $extensionRegistryKey = $null
                            [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey("$RegistryRelativeRoot\.rbl", $false)
                        }
                    } finally {
                        if ($extensionRegistryKey) { $extensionRegistryKey.Dispose() }
                    }
                }
            }
        }
    }
    if ($ProgIdOwned) { Remove-Item -LiteralPath $ProgIdKey -Recurse -Force -ErrorAction Stop }
    if ($ShortcutPath -and (Test-Path -LiteralPath $ShortcutPath -PathType Leaf)) {
        Assert-NoReparsePath $ShortcutPath
        $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($ShortcutPath)
        if ($shortcut.TargetPath -eq (Join-Path $InstallDir 'RBLStudio.vbs')) {
            Remove-Item -LiteralPath $ShortcutPath -Force
        }
    }
    } catch {
        $uninstallIntegrationError = $_
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
                if (Test-Path -LiteralPath $ProgIdKey) { Remove-Item -LiteralPath $ProgIdKey -Recurse -Force -ErrorAction Stop }
                Assert-RblRegistrySnapshotRoot $ProgIdRegBackup "$RegistryRegExeRoot\$ProgId"
                & reg.exe import $ProgIdRegBackup | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "reg.exe import exit code $LASTEXITCODE" }
            } catch { $rollbackFailures.Add("ProgID registry restore: $($_.Exception.Message)") }
        } else {
            try { if (Test-Path -LiteralPath $ProgIdKey) { Remove-Item -LiteralPath $ProgIdKey -Recurse -Force -ErrorAction Stop } }
            catch { $rollbackFailures.Add("removal of newly-created ProgID: $($_.Exception.Message)") }
        }
        if ($HadShortcut) {
            try {
                Assert-NoReparsePath $ShortcutPath
                Copy-Item -LiteralPath $ShortcutBackup -Destination $ShortcutPath -Force -ErrorAction Stop
            }
            catch { $rollbackFailures.Add("Start Menu shortcut restore: $($_.Exception.Message)") }
        }
        if ((Get-RblRollbackDisposition $rollbackFailures.ToArray()) -eq 'KeepCurrentInstallAndSnapshots') {
            $KeepRegistryBackup = $true
            $details = $rollbackFailures -join '; '
            throw "UNINSTALL REGISTRY ROLLBACK INCOMPLETE. Application files were not removed. Snapshot artifacts retained at '$RegistryBackupDir'. Restore the listed registry/shortcut state manually before retrying uninstall. Original integration error: $($uninstallIntegrationError.Exception.Message). Rollback failures: $details"
        }
        throw $uninstallIntegrationError
    } finally {
        if (-not $KeepRegistryBackup -and (Test-Path -LiteralPath $RegistryBackupDir)) { Remove-Item -LiteralPath $RegistryBackupDir -Recurse -Force }
    }
}

foreach ($relative in @($Manifest.files)) {
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $InstallDir ([string]$relative -replace '/', '\')))
    Assert-NoReparsePath $candidate
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { Remove-Item -LiteralPath $candidate -Force }
}

Remove-Item -LiteralPath $ManifestPath -Force -ErrorAction SilentlyContinue
# Prune only directories implied by manifest-owned files; unrelated empty user
# directories under the program root are not considered installer-owned.
if (Test-Path -LiteralPath $InstallDir -PathType Container) {
    $OwnedDirectories.Keys | Sort-Object { ($_ -split '[\\/]').Count } -Descending | ForEach-Object {
        $directory = Join-Path $InstallDir $_
        if (Test-Path -LiteralPath $directory -PathType Container) {
            if (-not (Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {
                Remove-Item -LiteralPath $directory -Force
            }
        }
    }
    if (-not (Get-ChildItem -LiteralPath $InstallDir -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $InstallDir -Force
    }
}
Write-Host 'RBL Studio wxWidgets program files removed. User settings, workspaces and backups were not touched.'
if (Test-Path -LiteralPath $InstallDir -PathType Container) {
    Write-Host "Untracked user files remain in: $InstallDir"
}
