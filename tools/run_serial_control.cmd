@echo off
setlocal
set "PYTHONUTF8=1"
rem Use the repository-local Python environment; do not modify system packages or PATH.
set "serial_python=%~dp0..\.cache\serial_tools\venv\Scripts\python.exe"
if not exist "%serial_python%" (
    python -m venv "%~dp0..\.cache\serial_tools\venv"
    if errorlevel 1 goto failed
)
rem Retry dependency installation after a previous interrupted first-time setup.
"%serial_python%" -c "import serial, paho.mqtt.client" >nul 2>&1
if errorlevel 1 (
    "%serial_python%" -m pip install -r "%~dp0requirements_serial.txt"
    if errorlevel 1 goto failed
)
"%serial_python%" "%~dp0serial_control.py" %*
if errorlevel 1 goto failed
exit /b 0
:failed
echo Serial tool failed. Check the message above.
pause
exit /b 1
