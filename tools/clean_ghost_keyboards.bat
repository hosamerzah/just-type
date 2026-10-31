@echo off
cd /d "%~dp0"
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Cleaning extra keyboards from Windows system profile...
python "%~dp0wipe_stray_keyboards.py"

echo.
echo Restarting taskbar and input services...
powershell -NoProfile -Command "Stop-Process -Name 'TextInputHost' -Force -ErrorAction SilentlyContinue; Stop-Process -Name 'explorer' -Force -ErrorAction SilentlyContinue;"

echo.
echo ===================================================
echo Done! All extra keyboards have been deleted.
echo Only English (US) and Arabic (101) remain.
echo ===================================================
echo.
pause
