@echo off
setlocal EnableExtensions
set "APP_ROOT=%~dp0"
if "%APP_ROOT:~-1%"=="\" set "APP_ROOT=%APP_ROOT:~0,-1%"
set "GUI="
set "WORKER="

rem Portable package: keep GUI, worker and backend payload together.
if exist "%APP_ROOT%\rbl-studio-wx.exe" if exist "%APP_ROOT%\rbl-studio-worker.exe" goto :portable

rem Developer checkout: use the last successful wx build pointer.
set "BUILD_DIR="
set "POINTER=%APP_ROOT%\build\wx-ui-current-win.path"
if exist "%POINTER%" set /p BUILD_DIR=<"%POINTER%"
if defined BUILD_DIR if not "%BUILD_DIR:~0,2%"=="\\" if not "%BUILD_DIR:~1,1%"==":" set "BUILD_DIR=%APP_ROOT%\%BUILD_DIR%"
if defined BUILD_DIR if exist "%BUILD_DIR%\rbl-studio-wx.exe" if exist "%BUILD_DIR%\rbl-studio-worker.exe" (
  set "GUI=%BUILD_DIR%\rbl-studio-wx.exe"
  set "WORKER=%BUILD_DIR%\rbl-studio-worker.exe"
  goto :launch
)

if not "%RBL_STUDIO_ALLOW_LEGACY%"=="1" goto :no_gui
if exist "%APP_ROOT%\target\release\rbl-studio.exe" set "LEGACY=%APP_ROOT%\target\release\rbl-studio.exe"
if not defined LEGACY if exist "%APP_ROOT%\target\x86_64-pc-windows-gnu\release\rbl-studio.exe" set "LEGACY=%APP_ROOT%\target\x86_64-pc-windows-gnu\release\rbl-studio.exe"
if not defined LEGACY (
  echo Legacy egui executable was not found.
  exit /b 1
)
"%LEGACY%" %*
exit /b %errorlevel%

:portable
set "GUI=%APP_ROOT%\rbl-studio-wx.exe"
set "WORKER=%APP_ROOT%\rbl-studio-worker.exe"
:launch
set "RBL_STUDIO_ROOT=%APP_ROOT%"
set "RBL_STUDIO_WORKER=%WORKER%"
cd /d "%APP_ROOT%"
"%GUI%" %*
exit /b %errorlevel%

:no_gui

echo RBL Studio wxWidgets build was not found.
echo Build it with: powershell -NoProfile -ExecutionPolicy Bypass -File "%APP_ROOT%\tools\build_wx_ide.ps1"
echo To explicitly run the retired egui frontend, set RBL_STUDIO_ALLOW_LEGACY=1.
exit /b 1
