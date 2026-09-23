@echo off
rem Build BatPathIntelliSense.dll (x64).
rem
rem Notepad++ 8.6.4 here is x64-only, so the plugin must be x64 too.
rem
rem Three environment traps this script works around. All three were hit for
rem real while setting this project up on this machine, so please keep them.
rem
rem   1. PATH puts F:\mingw64\bin *ahead* of the MSVC bin directory, and MinGW
rem      ships a GNU coreutils "link.exe" (a hard-link tool). cl looks the
rem      linker up on PATH, so it would hand MSVC link arguments to that
rem      program and die with an endless "command line error D8000" storm.
rem      Fix: force the MSVC bin directory to the front and call the linker by
rem      absolute path.
rem
rem   2. Passing more than one source file to a single cl invocation makes
rem      cl.exe crash with an access violation (0xC0000005) on this machine.
rem      Compiling each file in its own invocation works fine. So: one file
rem      per cl call, then a separate link step.
rem
rem   3. cmd.exe reads .bat in the OEM codepage (936), and MSVC reads sources
rem      in that same default codepage. So this file must stay ASCII-only, and
rem      the sources must be compiled with /utf-8 or the Chinese comments get
rem      misread and compilation fails outright.
rem
rem Keep this file ASCII-only.

rem enabledelayedexpansion: "%errorlevel%" inside the for block below would be
rem expanded once at parse time, so the check has to use "!errorlevel!".
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "VCVARS=F:\vs2015\VC\vcvarsall.bat"
set "VCBIN=F:\vs2015\VC\bin\amd64"
set "OUTDIR=%ROOT%build"
set "SRCDIR=%ROOT%src"
set "SDKDIR=%ROOT%sdk"

if not exist "%VCVARS%" goto no_toolchain
if not exist "%VCBIN%\link.exe" goto no_toolchain
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

call "%VCVARS%" amd64 >nul 2>&1
if not "%errorlevel%"=="0" goto no_toolchain

rem See trap 1.
set "PATH=%VCBIN%;%PATH%"

rem rc.exe (Windows SDK) is needed for the version resource.
where rc >nul 2>&1
if not "!errorlevel!"=="0" goto no_rc

echo === compiling ===
rem See trap 2: one file per invocation.
for %%F in (Utf8 PathCompletion Options Plugin) do (
    echo   %%F.cpp
    cl /nologo /c /W4 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /I"%SDKDIR%" /I"%SRCDIR%" /Fo"%OUTDIR%\%%F.obj" /Fd"%OUTDIR%\BatPathIntelliSense.pdb" "%SRCDIR%\%%F.cpp"
    if not "!errorlevel!"=="0" goto build_failed
)

echo === version resource ===
rem nppPluginList requires the DLL to carry a version resource, and the version
rem to match the list entry. rc.exe lives in the Windows SDK bin dir, which
rem vcvarsall puts on PATH.
rc /nologo /fo"%OUTDIR%\BatPathIntelliSense.res" "%SRCDIR%\BatPathIntelliSense.rc"
if not "%errorlevel%"=="0" goto build_failed

echo === linking ===
rem See trap 1: absolute path, not a PATH lookup.
"%VCBIN%\link.exe" /nologo /DLL /OUT:"%OUTDIR%\BatPathIntelliSense.dll" /MACHINE:X64 "%OUTDIR%\Utf8.obj" "%OUTDIR%\PathCompletion.obj" "%OUTDIR%\Options.obj" "%OUTDIR%\Plugin.obj" "%OUTDIR%\BatPathIntelliSense.res" user32.lib kernel32.lib
if not "%errorlevel%"=="0" goto build_failed

rem A crashing tool reports a negative exit code, and cmd's "if errorlevel N"
rem does a signed compare -- it would read that as success. Check the artifact.
if not exist "%OUTDIR%\BatPathIntelliSense.dll" goto build_failed

echo.
echo [ok] %OUTDIR%\BatPathIntelliSense.dll
exit /b 0

:no_toolchain
echo [x] toolchain not found. Check VCVARS / VCBIN in this file.
exit /b 1

:no_rc
echo [x] rc.exe not found on PATH -- it ships with the Windows SDK and is
echo     normally set up by vcvarsall.bat.
exit /b 1

:build_failed
echo.
echo [x] build failed
exit /b 1
