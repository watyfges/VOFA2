@echo off
setlocal
set "QT_QPA_PLATFORM=offscreen"
set "VOFA_SMOKE_TEST=1"
if not exist "%~dp0..\dist\platforms\qoffscreen.dll" (
    copy /y "D:\Qt\6.8.3\mingw_64\plugins\platforms\qoffscreen.dll" "%~dp0..\dist\platforms\" >nul
)
echo QT_QPA_PLATFORM=%QT_QPA_PLATFORM%
echo VOFA_SMOKE_TEST=%VOFA_SMOKE_TEST%
"%~dp0..\dist\vofa2_app.exe"
echo exit=%errorlevel%
endlocal
