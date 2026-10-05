@echo off
setlocal
set ROOT=%~dp0
set PY=%ROOT%.venv\Scripts\pythonw.exe
if not exist "%PY%" set PY=pythonw.exe
if not exist "%PY%" set PY=python.exe
"%PY%" -c "import tkinter" >nul 2>"%ROOT%ide_startup.log"
if errorlevel 1 (
  echo RBL Studio cannot start: Python/Tkinter is unavailable.
  echo See "%ROOT%ide_startup.log"
  exit /b 1
)
cd /d "%ROOT%"
"%PY%" "%ROOT%RBLStudio.py" %*
exit /b %errorlevel%
