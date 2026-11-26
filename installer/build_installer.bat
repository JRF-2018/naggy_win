@echo off
rem Compiles the installer with Inno Setup 6 (run tools/stage.sh first).
setlocal
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 ISCC.exe not found.
  exit /b 1
)
"%ISCC%" "%~dp0naggy_win.iss"
