@echo off
setlocal EnableExtensions

rem Packages an already-built dist/ directory:
rem   - VOFA2-<version>-win64.zip  (portable archive; create an empty file
rem     named "portable" next to vofa2_app.exe to keep settings in data\)
rem   - installer\output\VOFA2-<version>-setup.exe when Inno Setup 6 is installed
set "ROOT=%~dp0.."
set "DIST=%ROOT%\dist"
set "VERSION=0.1.0"
set "ZIP=%ROOT%\VOFA2-%VERSION%-win64.zip"

if not exist "%DIST%\vofa2_app.exe" (
    echo dist\vofa2_app.exe not found. Run scripts\build_windows.bat first.
    exit /b 1
)

echo ==== Zip ====
powershell -NoProfile -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 exit /b 1

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" (
    echo ==== Inno Setup ====
    "%ISCC%" "%ROOT%\installer\vofa2.iss"
    if errorlevel 1 exit /b 1
    echo Installer: %ROOT%\installer\output\VOFA2-%VERSION%-setup.exe
) else (
    echo Inno Setup 6 not found; skipping installer. Zip is ready.
)

echo Zip: %ZIP%
endlocal
