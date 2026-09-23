@echo off
rem Build, then package for a nppPluginList pull request.
rem
rem Keep this file ASCII-only: cmd.exe reads .bat in the OEM codepage (936).

setlocal

set "ROOT=%~dp0"

call "%ROOT%build.bat"
if not "%errorlevel%"=="0" (
    echo [x] build failed, not packaging
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\package.ps1"
if not "%errorlevel%"=="0" (
    echo.
    echo [x] packaging failed
    exit /b 1
)

echo.
echo === preflight (mirrors nppPluginList validator.py) ===
where python3 >nul 2>&1
if "%errorlevel%"=="0" (
    python3 "%ROOT%tools\preflight.py"
    if not "%errorlevel%"=="0" exit /b 1
) else (
    echo [skip] python3 not on PATH; run tools\preflight.py manually before the PR
)
