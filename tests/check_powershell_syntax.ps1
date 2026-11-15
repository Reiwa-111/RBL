$ErrorActionPreference = 'Stop'
$files = @(
    'tools/build_wx_ide.ps1',
    'installer/install_windows.ps1',
    'tests/accept_windows_registered_installer.ps1'
)

foreach ($file in $files) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path (Get-Location) $file),
        [ref]$tokens,
        [ref]$errors
    ) | Out-Null
    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Error "${file}: $_" }
        exit 1
    }
}

Write-Output 'PowerShell parser: PASS'
