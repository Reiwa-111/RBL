$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Pyw = (Get-Command pyw.exe -ErrorAction SilentlyContinue).Source
if (-not $Pyw) { $Pyw = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source }
if (-not $Pyw) { $Pyw = (Get-Command python.exe -ErrorAction SilentlyContinue).Source }
if (-not $Pyw) {
    Write-Host 'Python 3 was not found on PATH.' -ForegroundColor Red
    exit 1
}
$Launcher = Join-Path $Root 'RBLStudio.py'
$Icon = Join-Path $Root 'assets\rblstudio.ico'
$ProgId = 'Reiwa.RBLStudio.RBL'
New-Item -Path 'HKCU:\Software\Classes\.rbl' -Force | Out-Null
Set-Item -Path 'HKCU:\Software\Classes\.rbl' -Value $ProgId
New-Item -Path "HKCU:\Software\Classes\$ProgId" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId" -Name '(Default)' -Value 'RBL source file'
New-Item -Path "HKCU:\Software\Classes\$ProgId\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\DefaultIcon" -Name '(Default)' -Value "`"$Icon`",0"
New-Item -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Force | Out-Null
$Command = "`"$Pyw`" `"$Launcher`" `"%1`""
Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Name '(Default)' -Value $Command
$Shell = New-Object -ComObject Shell.Application
try { $Shell.Refresh } catch {}
Write-Host "RBL files are now associated with RBL Studio." -ForegroundColor Green
Write-Host "Launcher: $Pyw"
Write-Host "Project:  $Root"
