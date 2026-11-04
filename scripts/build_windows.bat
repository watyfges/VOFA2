@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "QT_DIR=D:\Qt\6.8.3\mingw_64"
set "MINGW_DIR=D:\Qt\Tools\mingw1310_64"
set "CMAKE_DIR=D:\Qt\Tools\CMake\bin"
set "NINJA_DIR=D:\Qt\Tools\Ninja"

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo Qt MinGW 6.8.3 not found at %QT_DIR%
    exit /b 1
)
if not exist "%MINGW_DIR%\bin\g++.exe" (
    echo MinGW toolchain not found at %MINGW_DIR%
    exit /b 1
)
if not exist "%CMAKE_DIR%\cmake.exe" (
    echo CMake not found at %CMAKE_DIR%
    exit /b 1
)

set "PATH=%MINGW_DIR%\bin;%CMAKE_DIR%;%NINJA_DIR%;%QT_DIR%\bin;%PATH%"

echo ==== Configure ====
"%CMAKE_DIR%\cmake.exe" -S "%ROOT%" -B "%ROOT%\build-win" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_C_COMPILER="%MINGW_DIR%\bin\gcc.exe" ^
    -DCMAKE_CXX_COMPILER="%MINGW_DIR%\bin\g++.exe" ^
    -DVOFA_BUILD_APP=ON ^
    -DVOFA_BUILD_TESTS=ON
if errorlevel 1 exit /b 1

echo ==== Build ====
"%CMAKE_DIR%\cmake.exe" --build "%ROOT%\build-win"
if errorlevel 1 exit /b 1

echo ==== Tests ====
"%CMAKE_DIR%\ctest.exe" --test-dir "%ROOT%\build-win" --output-on-failure
if errorlevel 1 exit /b 1

echo ==== Deploy ====
if exist "%ROOT%\dist" rmdir /s /q "%ROOT%\dist"
mkdir "%ROOT%\dist"
copy /y "%ROOT%\build-win\vofa2_app.exe" "%ROOT%\dist\" >nul
"%QT_DIR%\bin\windeployqt.exe" --release --compiler-runtime ^
    --qmldir "%ROOT%\src\app" "%ROOT%\dist\vofa2_app.exe"
if errorlevel 1 exit /b 1

rem Keep only the Basic style; the app forces it at startup. Also drop
rem debug-only qmltooling, unused Qt translations and the software OpenGL
rem fallback (Qt Quick uses D3D11 on Windows 10/11).
echo ==== Trim ====
if exist "%ROOT%\dist\qmltooling" rmdir /s /q "%ROOT%\dist\qmltooling"
if exist "%ROOT%\dist\translations" rmdir /s /q "%ROOT%\dist\translations"
if exist "%ROOT%\dist\opengl32sw.dll" del /q "%ROOT%\dist\opengl32sw.dll"
for %%S in (Fusion Material Universal Imagine Windows FluentWinUI3) do (
    if exist "%ROOT%\dist\qml\QtQuick\Controls\%%S" rmdir /s /q "%ROOT%\dist\qml\QtQuick\Controls\%%S"
)
for %%M in (Dialogs Effects LocalStorage Particles VectorImage tooling) do (
    if exist "%ROOT%\dist\qml\QtQuick\%%M" rmdir /s /q "%ROOT%\dist\qml\QtQuick\%%M"
)
del /q "%ROOT%\dist\Qt6QuickControls2Fusion*.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickControls2Material*.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickControls2Universal*.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickControls2Imagine*.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickControls2WindowsStyleImpl.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickControls2FluentWinUI3StyleImpl.dll" 2>nul
del /q "%ROOT%\dist\Qt6QuickEffects.dll" 2>nul

echo.
echo Build complete: %ROOT%\dist\vofa2_app.exe
endlocal
