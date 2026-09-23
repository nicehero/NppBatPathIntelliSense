@echo off
rem Build and run the core-logic tests.
rem
rem Same environment traps as build.bat -- see the notes there. Keep this file
rem ASCII-only.

setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "VCVARS=F:\vs2015\VC\vcvarsall.bat"
set "VCBIN=F:\vs2015\VC\bin\amd64"
set "OUTDIR=%ROOT%build\test"
set "SRCDIR=%ROOT%src"
set "TESTDIR=%ROOT%test"

if not exist "%VCVARS%" goto no_toolchain
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

call "%VCVARS%" amd64 >nul 2>&1
if not "!errorlevel!"=="0" goto no_toolchain

rem One file per cl invocation: multi-file cl crashes here. See build.bat trap 2.
for %%F in (Utf8 PathCompletion) do (
    cl /nologo /c /W4 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /I"%SRCDIR%" /Fo"%OUTDIR%\%%F.obj" "%SRCDIR%\%%F.cpp"
    if not "!errorlevel!"=="0" goto failed
)

cl /nologo /c /W4 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /I"%SRCDIR%" /Fo"%OUTDIR%\TestPathCompletion.obj" "%TESTDIR%\TestPathCompletion.cpp"
if not "!errorlevel!"=="0" goto failed

"%VCBIN%\link.exe" /nologo /SUBSYSTEM:CONSOLE /OUT:"%OUTDIR%\TestPathCompletion.exe" /MACHINE:X64 "%OUTDIR%\Utf8.obj" "%OUTDIR%\PathCompletion.obj" "%OUTDIR%\TestPathCompletion.obj" user32.lib kernel32.lib
if not "!errorlevel!"=="0" goto failed

echo === running tests ===
"%OUTDIR%\TestPathCompletion.exe"
exit /b %errorlevel%

:no_toolchain
echo [x] toolchain not found. Check VCVARS / VCBIN in this file.
exit /b 1

:failed
echo [x] test build failed
exit /b 1
