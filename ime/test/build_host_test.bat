@echo off
setlocal
set ROOT=%~dp0
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
pushd "%ROOT%"
cl /nologo /EHsc /utf-8 /DUNICODE /D_UNICODE host_test.cpp ole32.lib oleaut32.lib uuid.lib user32.lib
popd
