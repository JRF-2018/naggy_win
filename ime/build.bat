@echo off
rem Builds naggy_ime.dll for x64 and x86 with Visual Studio 2022 (MSVC).
rem Usage: build.bat [x64|x86|all]   (default: all)
setlocal
set ROOT=%~dp0
set TARGETS=%1
if "%TARGETS%"=="" set TARGETS=all
if "%TARGETS%"=="all" (
  call :build x64 || exit /b 1
  call :build x86 || exit /b 1
  exit /b 0
)
call :build %TARGETS%
exit /b %ERRORLEVEL%

:build
setlocal
set ARCH=%1
set VSDIR=
for %%e in (Community Professional Enterprise BuildTools) do if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvarsall.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
if not "%VSINSTALLDIR_OVERRIDE%"=="" set "VSDIR=%VSINSTALLDIR_OVERRIDE%"
if "%VSDIR%"=="" (
  echo Visual Studio with C++ tools not found.
  exit /b 1
)
if "%ARCH%"=="x64" (call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul) else (call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul)
set OUT=%ROOT%out\%ARCH%
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
rc /nologo /fo naggy_ime.res "%ROOT%res\naggy_ime.rc" || (popd & exit /b 1)
cl /nologo /c /O2 /MT /EHsc /std:c++17 /W3 /utf-8 /DUNICODE /D_UNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNDEBUG ^
  "%ROOT%src\DllMain.cpp" "%ROOT%src\Register.cpp" "%ROOT%src\TextService.cpp" ^
  "%ROOT%src\CandidateWindow.cpp" "%ROOT%src\DisplayAttribute.cpp" "%ROOT%src\LangBarButton.cpp" ^
  "%ROOT%src\PipeBackend.cpp" "%ROOT%src\Engine.cpp" "%ROOT%src\Protocol.cpp" "%ROOT%src\Log.cpp" || (popd & exit /b 1)
link /nologo /DLL /OUT:naggy_ime.dll /DEF:"%ROOT%src\naggy_ime.def" *.obj naggy_ime.res ^
  user32.lib gdi32.lib advapi32.lib ole32.lib oleaut32.lib uuid.lib shell32.lib d2d1.lib dwrite.lib || (popd & exit /b 1)
rem settings window
if not exist cfg mkdir cfg
rc /nologo /fo cfg\naggy_config.res "%ROOT%res\naggy_config.rc" || (popd & exit /b 1)
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /utf-8 /DUNICODE /D_UNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 ^
  /Fo"cfg\\" /Fe:naggy-config.exe "%ROOT%config\NaggyConfig.cpp" cfg\naggy_config.res ^
  user32.lib gdi32.lib advapi32.lib ole32.lib shell32.lib comctl32.lib ^
  /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED || (popd & exit /b 1)
popd
echo Built %OUT%\naggy_ime.dll
exit /b 0
