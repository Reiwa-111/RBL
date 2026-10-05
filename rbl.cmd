@echo off
setlocal
set ROOT=%~dp0
if exist "%ROOT%.venv\Scripts\python.exe" (
  "%ROOT%.venv\Scripts\python.exe" "%ROOT%rbl.py" %*
) else (
  python.exe "%ROOT%rbl.py" %*
)
exit /b %errorlevel%
