$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\installer\windows_registry_policy.ps1')

function Assert-Equal($Expected, $Actual, [string]$Name) {
    if ($Expected -ne $Actual) { throw "${Name}: expected '$Expected', got '$Actual'" }
}

$defaultRegistryRoot = Resolve-RblRegistryClassesRoot 'HKCU:\Software\Classes'
Assert-Equal 'HKCU:\Software\Classes' $defaultRegistryRoot.ProviderPath 'default registry classes root'
Assert-Equal 'Software\Classes' $defaultRegistryRoot.RelativePath 'default registry relative root'
$acceptanceGuid = '01234567-89ab-cdef-0123-456789abcdef'
$acceptanceRegistryRoot = Resolve-RblRegistryClassesRoot "HKCU:\Software\RBLStudioAcceptance\$acceptanceGuid\Classes"
Assert-Equal "HKCU:\Software\RBLStudioAcceptance\$acceptanceGuid\Classes" $acceptanceRegistryRoot.ProviderPath 'acceptance registry classes root'
foreach ($invalidRoot in @(
    'HKCU:\Software\Classes\..\Other',
    'HKLM:\Software\RBLStudioAcceptance\01234567-89ab-cdef-0123-456789abcdef\Classes',
    'HKCU:\Software\RBLStudioAcceptance\0123456789abcdef0123456789abcdef\Classes',
    'HKCU:\Software\RBLStudioAcceptance\01234567-89AB-CDEF-0123-456789ABCDEF\Classes',
    'HKCU:\Software\RBLStudioAcceptance\01234567-89ab-cdef-0123-456789abcdef\Classes\Extra'
)) {
    $rejected = $false
    try { $null = Resolve-RblRegistryClassesRoot $invalidRoot } catch { $rejected = $true }
    Assert-Equal $true $rejected "reject invalid registry root '$invalidRoot'"
}
$snapshotTestPath = Join-Path ([System.IO.Path]::GetTempPath()) ('rbl-registry-policy-' + [guid]::NewGuid().ToString('N') + '.reg')
try {
    Set-Content -LiteralPath $snapshotTestPath -Value @('Windows Registry Editor Version 5.00', '', '[HKEY_CURRENT_USER\Software\RBLStudioAcceptance\01234567-89ab-cdef-0123-456789abcdef\Classes\.rbl]') -Encoding Unicode
    Assert-RblRegistrySnapshotRoot $snapshotTestPath 'HKCU\Software\RBLStudioAcceptance\01234567-89ab-cdef-0123-456789abcdef\Classes\.rbl'
    Set-Content -LiteralPath $snapshotTestPath -Value @('Windows Registry Editor Version 5.00', '', '[HKEY_CURRENT_USER\Software\Classes\.rbl]') -Encoding Unicode
    $snapshotRejected = $false
    try { Assert-RblRegistrySnapshotRoot $snapshotTestPath 'HKCU\Software\RBLStudioAcceptance\01234567-89ab-cdef-0123-456789abcdef\Classes\.rbl' } catch { $snapshotRejected = $true }
    Assert-Equal $true $snapshotRejected 'reject registry snapshot outside validated acceptance root'
} finally {
    if (Test-Path -LiteralPath $snapshotTestPath) { Remove-Item -LiteralPath $snapshotTestPath -Force }
}

$install = 'C:\Program Files\RBL Studio'
$expected = Get-RblExpectedOpenCommand $install 'C:\Windows'
Assert-Equal '"C:\Windows\System32\wscript.exe" "C:\Program Files\RBL Studio\RBLStudio.vbs" "%1"' $expected 'expected open command construction'
Assert-Equal $true (Test-RblProgIdCommandOwnership $expected $expected) 'exact ProgID command'
Assert-Equal $false (Test-RblProgIdCommandOwnership ($expected + ' --foreign') $expected) 'modified ProgID command'
Assert-Equal $false (Test-RblProgIdCommandOwnership '"C:\Other\wscript.exe" "C:\Program Files\RBL Studio\RBLStudio.vbs" "%1"' $expected) 'foreign wscript path'
Assert-Equal $true (Test-RblProgIdOwnership 'RBL source file' ('"' + (Join-Path $install 'assets\rblstudio.ico') + '",0') $expected ('"' + (Join-Path $install 'assets\rblstudio.ico') + '",0') $expected) 'complete owned ProgID identity'
Assert-Equal $false (Test-RblProgIdOwnership 'User file type' ('"' + (Join-Path $install 'assets\rblstudio.ico') + '",0') $expected ('"' + (Join-Path $install 'assets\rblstudio.ico') + '",0') $expected) 'modified ProgID description'
Assert-Equal $false (Test-RblProgIdOwnership 'RBL source file' 'C:\foreign.ico,0' $expected ('"' + (Join-Path $install 'assets\rblstudio.ico') + '",0') $expected) 'modified ProgID icon'
$ownedTree = @{ root_values = @(''); root_subkeys = @('DefaultIcon', 'shell'); icon_values = @(''); icon_subkeys = @(); shell_values = @(); shell_subkeys = @('open'); open_values = @(); open_subkeys = @('command'); command_values = @(''); command_subkeys = @() }
Assert-Equal $true (Test-RblProgIdTreeOwnership $ownedTree) 'exact ProgID tree'
$modifiedTree = $ownedTree.Clone(); $modifiedTree.shell_subkeys = @('open', 'edit')
Assert-Equal $false (Test-RblProgIdTreeOwnership $modifiedTree) 'foreign ProgID subkey'
Assert-Equal $true (Test-RblOpenWithValueOwnership 'None' ([byte[]]@())) 'owned REG_NONE value'
Assert-Equal $false (Test-RblOpenWithValueOwnership 'Binary' ([byte[]]@())) 'REG_BINARY is not owned'
Assert-Equal $false (Test-RblOpenWithValueOwnership 'None' ([byte[]](1))) 'non-empty REG_NONE is not owned'
Assert-Equal $false (Test-RblOpenWithValueOwnership 'None' '') 'string value is not owned'
Assert-Equal $true (Should-RblSetExtensionDefault $false 'Other.ProgId' 'Reiwa.RBLStudio.RBL') 'fresh install sets extension default'
Assert-Equal $true (Should-RblSetExtensionDefault $true 'Reiwa.RBLStudio.RBL' 'Reiwa.RBLStudio.RBL') 'upgrade retains owned default'
Assert-Equal $false (Should-RblSetExtensionDefault $true 'User.Changed.ProgId' 'Reiwa.RBLStudio.RBL') 'upgrade preserves user-changed default'
Assert-Equal $true (Test-RblAssociationMutationAllowed $true $true 'Reiwa.RBLStudio.RBL' 'Reiwa.RBLStudio.RBL') 'owned ProgID may clean its associations'
Assert-Equal $false (Test-RblAssociationMutationAllowed $false $true $null 'Reiwa.RBLStudio.RBL') 'missing ProgID with OpenWith reference is protected'
Assert-Equal $false (Test-RblAssociationMutationAllowed $false $false 'Reiwa.RBLStudio.RBL' 'Reiwa.RBLStudio.RBL') 'missing ProgID with extension default is protected'
Assert-Equal $true (Test-RblAssociationMutationAllowed $false $false 'Other.ProgId' 'Reiwa.RBLStudio.RBL') 'unreferenced missing ProgID permits payload cleanup'
Assert-Equal $true (Test-RblOwnedShortcutPath 'C:\Users\Test\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk' 'C:\Users\Test\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk') 'owned shortcut path'
Assert-Equal $false (Test-RblOwnedShortcutPath 'C:\Users\Test\Documents\important.lnk' 'C:\Users\Test\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk') 'foreign shortcut path'
Assert-Equal $false (Test-RblOwnedShortcutPath '' 'C:\Users\Test\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\RBL Studio wx.lnk') 'missing shortcut path'
$installerSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\installer\install_windows.ps1') -Raw
if (-not $installerSource.Contains('Assert-RustWorkerSourceBundle')) { throw 'Installer must verify the Rust worker corresponding-source archive.' }
if (-not $installerSource.Contains('Copy-Tree (Join-Path $BuildDir ''source'') (Join-Path $Stage ''source'')')) { throw 'Installer must retain the Rust worker source archive in the installed tree.' }
$installerGuard = $installerSource.IndexOf('Assert-NoReparsePath $ShortcutPath')
$installerInspect = $installerSource.IndexOf('Test-Path -LiteralPath $ShortcutPath')
if ($installerGuard -lt 0 -or $installerInspect -lt 0 -or $installerGuard -gt $installerInspect) { throw 'Installer must assert shortcut path reparse safety before inspecting it.' }
$installerCreateGuard = $installerSource.IndexOf('Assert-NoReparsePath $ShortcutPath', $installerGuard + 1)
$installerCreate = $installerSource.IndexOf('New-Item -ItemType Directory -Path (Split-Path -Parent $ShortcutPath)')
if ($installerCreateGuard -lt 0 -or $installerCreate -lt 0 -or $installerCreateGuard -gt $installerCreate) { throw 'Installer must recheck shortcut path reparse safety immediately before creating the shortcut.' }
$installerFinalShortcutGuard = $installerSource.IndexOf('Assert-NoReparsePath $ShortcutPath', $installerCreateGuard + 1)
$installerShortcutWrite = $installerSource.LastIndexOf('CreateShortcut($ShortcutPath)')
if ($installerFinalShortcutGuard -lt 0 -or $installerShortcutWrite -lt 0 -or $installerFinalShortcutGuard -gt $installerShortcutWrite) { throw 'Installer must recheck shortcut path reparse safety after creating its parent and before COM writes the shortcut.' }
if ($installerSource -notmatch 'if \(-not \(Test-Path -LiteralPath \$ExtensionKey\)\)\s*\{\s*New-Item -Path \$ExtensionKey -Force') {
    throw 'Installer must create the .rbl registry key only when absent, preserving an existing default value.'
}
if ($installerSource -notmatch 'if \(-not \(Test-Path -LiteralPath \$ClassKey\)\)\s*\{\s*New-Item -Path \$ClassKey -Force') {
    throw 'Installer must create the ProgID registry key only when absent, preserving its owned default value on upgrade.'
}
$uninstallerSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\installer\uninstall_windows.ps1') -Raw
$installerParamRootCount = ([regex]::Matches($installerSource, [regex]::Escape("'HKCU:\Software\Classes'"))).Count
$uninstallerParamRootCount = ([regex]::Matches($uninstallerSource, [regex]::Escape("'HKCU:\Software\Classes'"))).Count
Assert-Equal 2 $installerParamRootCount 'installer default and legacy manifest fallback root literals'
Assert-Equal 2 $uninstallerParamRootCount 'uninstaller default and legacy manifest fallback root literals'
if ($installerSource -match "OpenSubKey\('Software\\Classes|CreateSubKey\('Software\\Classes|reg\.exe (export|import) 'HKCU\\Software\\Classes") { throw 'Installer contains a hard-coded registry operation outside the validated root.' }
if ($uninstallerSource -match "OpenSubKey\('Software\\Classes|DeleteSubKey\('Software\\Classes|reg\.exe (export|import) 'HKCU\\Software\\Classes") { throw 'Uninstaller contains a hard-coded registry operation outside the validated root.' }
if (([regex]::Matches($installerSource, 'Assert-RblRegistrySnapshotRoot .*RegistryRegExeRoot')).Count -ne 2) { throw 'Installer must validate both registry snapshots before import.' }
if (([regex]::Matches($uninstallerSource, 'Assert-RblRegistrySnapshotRoot .*RegistryRegExeRoot')).Count -ne 2) { throw 'Uninstaller must validate both registry snapshots before import.' }
$acceptanceSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'accept_windows_registered_installer.ps1') -Raw
if ($acceptanceSource -match 'HKCU:\\Software\\Classes') { throw 'Acceptance script must never target the real Classes root.' }
foreach ($requiredAcceptanceText in @('reg.exe export', 'reg.exe import', '-RegistryClassesRoot $RegistryClassesRoot', 'finally', 'Remove-Item -LiteralPath $GuidSubkey', 'AppData With Spaces', 'user-changed default', 'licenses/Scintilla/License.txt', 'licenses/Rust/THIRD_PARTY_NOTICES.md', 'source/Rust-worker-source.tar.zst', 'source/Rust-worker-source.sha256', 'New-Item -ItemType Junction', 'Remove-AcceptanceJunction', 'RollbackSentinel', 'late shortcut failure registry rollback')) {
    if (-not $acceptanceSource.Contains($requiredAcceptanceText)) { throw "Acceptance script is missing required coverage text: $requiredAcceptanceText" }
}
$uninstallerOwnership = $uninstallerSource.IndexOf('Test-RblOwnedShortcutPath $ShortcutPath $ExpectedShortcutPath')
$uninstallerGuard = $uninstallerSource.IndexOf('Assert-NoReparsePath $ShortcutPath')
$uninstallerInspect = $uninstallerSource.IndexOf('Test-Path -LiteralPath $ShortcutPath')
if ($uninstallerOwnership -lt 0 -or $uninstallerGuard -lt 0 -or $uninstallerInspect -lt 0 -or $uninstallerOwnership -gt $uninstallerGuard -or $uninstallerGuard -gt $uninstallerInspect) { throw 'Uninstaller must validate exact shortcut ownership and reparse safety before inspecting it.' }
$uninstallerShortcutWrite = $uninstallerSource.IndexOf('CreateShortcut($ShortcutPath)')
$uninstallerFinalShortcutGuard = $uninstallerSource.LastIndexOf('Assert-NoReparsePath $ShortcutPath', $uninstallerShortcutWrite)
if ($uninstallerFinalShortcutGuard -lt $uninstallerGuard -or $uninstallerShortcutWrite -lt 0 -or $uninstallerFinalShortcutGuard -gt $uninstallerShortcutWrite) { throw 'Uninstaller must recheck shortcut path reparse safety immediately before COM inspection.' }
Assert-Equal 'RestorePreviousInstall' (Get-RblRollbackDisposition @()) 'successful rollback disposition'
Assert-Equal 'KeepCurrentInstallAndSnapshots' (Get-RblRollbackDisposition @('ProgID import failed')) 'failed rollback disposition'

Write-Host 'Windows registry policy pure tests passed (no registry access).'
