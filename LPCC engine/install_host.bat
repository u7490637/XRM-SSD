@echo off
REM Install the native messaging host for XRM-SSD LPCC-2
REM Run as Administrator.

set HOST_NAME=com.xrmssd.lpcc2
set HOST_PATH=%~dp0native_host.py
set MANIFEST_PATH=%~dp0%HOST_NAME%.json

REM Create registry key for Chrome (NativeMessagingHosts)
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\%HOST_NAME%" /ve /t REG_SZ /d "%MANIFEST_PATH%" /f

echo Native host installed successfully.
pause
