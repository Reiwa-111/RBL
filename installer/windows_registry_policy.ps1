function Resolve-RblRegistryClassesRoot([string]$Path) {
    $defaultPath = 'HKCU:\Software\Classes'
    if ([string]::Equals($Path, $defaultPath, [System.StringComparison]::Ordinal)) {
        return [pscustomobject]@{ ProviderPath = $defaultPath; RelativePath = 'Software\Classes'; RegExePath = 'HKCU\Software\Classes' }
    }
    $pattern = '^HKCU:\\Software\\RBLStudioAcceptance\\([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\\Classes$'
    if ($Path -cnotmatch $pattern) { throw 'RegistryClassesRoot must be exactly HKCU:\Software\Classes or HKCU:\Software\RBLStudioAcceptance\<canonical-guid>\Classes.' }
    $guid = [guid]::Empty
    if (-not [guid]::TryParseExact($Matches[1], 'D', [ref]$guid) -or $guid.ToString('D') -cne $Matches[1]) {
        throw 'Acceptance RegistryClassesRoot must contain a canonical lowercase GUID in D format.'
    }
    $relative = 'Software\RBLStudioAcceptance\' + $Matches[1] + '\Classes'
    return [pscustomobject]@{ ProviderPath = ('HKCU:\' + $relative); RelativePath = $relative; RegExePath = ('HKCU\' + $relative) }
}

function Assert-RblRegistrySnapshotRoot([string]$SnapshotPath, [string]$ExpectedRegExePath) {
    if (-not (Test-Path -LiteralPath $SnapshotPath -PathType Leaf)) { throw "Registry snapshot is missing: $SnapshotPath" }
    if (-not $ExpectedRegExePath.StartsWith('HKCU\', [System.StringComparison]::Ordinal)) { throw 'Registry snapshot target is not a validated HKCU path.' }
    $expectedKey = 'HKEY_CURRENT_USER\' + $ExpectedRegExePath.Substring(5)
    $keyLines = @(Get-Content -LiteralPath $SnapshotPath | Where-Object { $_ -match '^\[.+\]$' })
    if ($keyLines.Count -eq 0) { throw "Registry snapshot contains no key sections: $SnapshotPath" }
    foreach ($line in $keyLines) {
        $key = $line.Substring(1, $line.Length - 2)
        if (-not ([string]::Equals($key, $expectedKey, [System.StringComparison]::OrdinalIgnoreCase) -or
            $key.StartsWith($expectedKey + '\', [System.StringComparison]::OrdinalIgnoreCase))) {
            throw "Registry snapshot contains a key outside the validated root '$expectedKey': $key"
        }
    }
}

function Get-RblExpectedOpenCommand([string]$InstallDir, [string]$WindowsDir) {
    $launcher = [System.IO.Path]::GetFullPath((Join-Path $InstallDir 'RBLStudio.vbs'))
    $wscript = [System.IO.Path]::GetFullPath((Join-Path $WindowsDir 'System32\wscript.exe'))
    return ('"{0}" "{1}" "%1"' -f $wscript, $launcher)
}

function Test-RblProgIdCommandOwnership([string]$ActualCommand, [string]$ExpectedCommand) {
    return [string]::Equals($ActualCommand, $ExpectedCommand, [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-RblProgIdOwnership([string]$Description, [string]$Icon, [string]$Command, [string]$ExpectedIcon, [string]$ExpectedCommand) {
    return ((Test-RblProgIdCommandOwnership $Command $ExpectedCommand) -and
        [string]::Equals($Description, 'RBL source file', [System.StringComparison]::Ordinal) -and
        [string]::Equals($Icon, $ExpectedIcon, [System.StringComparison]::OrdinalIgnoreCase))
}

function Test-RblExactRegistryNames([object[]]$Actual, [string[]]$Expected) {
    $actualNames = @($Actual | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object)
    $expectedNames = @($Expected | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object)
    return ($actualNames.Count -eq $expectedNames.Count -and (($actualNames -join "`0") -ceq ($expectedNames -join "`0")))
}

function Get-RblProgIdTreeShape([Microsoft.Win32.RegistryKey]$RootKey) {
    $icon = $RootKey.OpenSubKey('DefaultIcon', $false)
    $shell = $RootKey.OpenSubKey('shell', $false)
    $open = if ($shell) { $shell.OpenSubKey('open', $false) } else { $null }
    $command = if ($open) { $open.OpenSubKey('command', $false) } else { $null }
    try {
        if (-not $icon -or -not $shell -or -not $open -or -not $command) { return $null }
        return @{
            root_values = @($RootKey.GetValueNames()); root_subkeys = @($RootKey.GetSubKeyNames())
            icon_values = @($icon.GetValueNames()); icon_subkeys = @($icon.GetSubKeyNames())
            shell_values = @($shell.GetValueNames()); shell_subkeys = @($shell.GetSubKeyNames())
            open_values = @($open.GetValueNames()); open_subkeys = @($open.GetSubKeyNames())
            command_values = @($command.GetValueNames()); command_subkeys = @($command.GetSubKeyNames())
        }
    } finally {
        if ($command) { $command.Dispose() }; if ($open) { $open.Dispose() }
        if ($shell) { $shell.Dispose() }; if ($icon) { $icon.Dispose() }
    }
}

function Test-RblProgIdTreeOwnership($Shape) {
    if ($null -eq $Shape) { return $false }
    return ((Test-RblExactRegistryNames $Shape.root_values @('')) -and
        (Test-RblExactRegistryNames $Shape.root_subkeys @('DefaultIcon', 'shell')) -and
        (Test-RblExactRegistryNames $Shape.icon_values @('')) -and
        (Test-RblExactRegistryNames $Shape.icon_subkeys @()) -and
        (Test-RblExactRegistryNames $Shape.shell_values @()) -and
        (Test-RblExactRegistryNames $Shape.shell_subkeys @('open')) -and
        (Test-RblExactRegistryNames $Shape.open_values @()) -and
        (Test-RblExactRegistryNames $Shape.open_subkeys @('command')) -and
        (Test-RblExactRegistryNames $Shape.command_values @('')) -and
        (Test-RblExactRegistryNames $Shape.command_subkeys @()))
}

function Test-RblOpenWithValueOwnership([string]$Kind, $Data) {
    if ($Kind -ne 'None' -or $null -eq $Data) { return $false }
    return ($Data -is [byte[]] -and $Data.Length -eq 0)
}

function Should-RblSetExtensionDefault([bool]$IsUpgrade, $CurrentDefault, [string]$ProgId) {
    return (-not $IsUpgrade -or $CurrentDefault -eq $ProgId)
}

function Test-RblAssociationMutationAllowed([bool]$ProgIdOwned, [bool]$OpenWithValueExists, $CurrentDefault, [string]$ProgId) {
    $extensionReferencesProgId = ($CurrentDefault -eq $ProgId)
    return ($ProgIdOwned -or (-not $OpenWithValueExists -and -not $extensionReferencesProgId))
}

function Test-RblOwnedShortcutPath([string]$ActualPath, [string]$ExpectedPath) {
    if ([string]::IsNullOrWhiteSpace($ActualPath) -or [string]::IsNullOrWhiteSpace($ExpectedPath)) { return $false }
    try {
        $actualFull = [System.IO.Path]::GetFullPath($ActualPath)
        $expectedFull = [System.IO.Path]::GetFullPath($ExpectedPath)
        return [string]::Equals($actualFull, $expectedFull, [System.StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

function Get-RblRollbackDisposition([string[]]$Failures) {
    if ($Failures -and $Failures.Count -gt 0) { return 'KeepCurrentInstallAndSnapshots' }
    return 'RestorePreviousInstall'
}
