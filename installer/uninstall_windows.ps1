$ErrorActionPreference = 'Stop'
$ProgId = 'Reiwa.RBLStudio.RBL'
Remove-Item -Path 'HKCU:\Software\Classes\.rbl' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\$ProgId" -Recurse -Force -ErrorAction SilentlyContinue
$Shell = New-Object -ComObject Shell.Application
try { $Shell.Refresh } catch {}
Write-Host 'RBL Studio file association removed.' -ForegroundColor Green
