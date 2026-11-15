@echo off
setlocal
if defined RBL_PYTHON goto run
set "RBL_PYTHON=%~dp0runtime\python\python.exe"
if exist "%RBL_PYTHON%" goto run
set "RBL_PYTHON=python"
:run
"%RBL_PYTHON%" "%~dp0rbl.py" %*
exit /b %ERRORLEVEL%
