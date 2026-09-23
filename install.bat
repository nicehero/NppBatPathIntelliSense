@echo off
rem Install the plugin into Notepad++.
rem
rem Notepad++ 7.6+ loads plugins from <npp>\plugins\<Folder>\<Folder>.dll,
rem so the folder name and the DLL name must match. Keep this file ASCII-only.

setlocal

set "ROOT=%~dp0"
set "NPPDIR=C:\Program Files\Notepad++"
set "PLUGINDIR=%NPPDIR%\plugins\BatPathIntelliSense"
set "SRCDLL=%ROOT%build\BatPathIntelliSense.dll"

if not exist "%SRCDLL%" (
    echo [x] %SRCDLL% not found -- run build.bat first.
    exit /b 1
)

if not exist "%NPPDIR%\notepad++.exe" (
    echo [x] Notepad++ not found at %NPPDIR%
    echo     Edit NPPDIR in this file.
    exit /b 1
)

rem Writing below Program Files needs elevation.
net session >nul 2>&1
if not "%errorlevel%"=="0" (
    echo [x] This needs an elevated prompt ^(right-click -^> Run as administrator^).
    exit /b 1
)

if not exist "%PLUGINDIR%" mkdir "%PLUGINDIR%"

rem Notepad++ keeps the DLL locked while it is running, so a plain copy can fail.
tasklist /fi "imagename eq notepad++.exe" 2>nul | find /i "notepad++.exe" >nul
if "%errorlevel%"=="0" (
    echo [x] Notepad++ is running -- close it first.
    exit /b 1
)

copy /y "%SRCDLL%" "%PLUGINDIR%\BatPathIntelliSense.dll" >nul
if not "%errorlevel%"=="0" (
    echo [x] copy failed
    exit /b 1
)

echo [ok] installed to %PLUGINDIR%
echo      Start Notepad++, then check the Plugins menu.
