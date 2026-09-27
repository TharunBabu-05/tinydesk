@echo off
rem tinydesk_putty.cmd [COMx] [baud] - PuTTY on an ESP32 dev board without
rem resetting it: serial_bridge.py holds the COM port with DTR/RTS released
rem and PuTTY connects to it over Telnet on 127.0.0.1:2310.
rem Close this window (or press Ctrl+C in it) to stop the bridge before flashing.
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM10
set BAUD=%2
if "%BAUD%"=="" set BAUD=921600
title tinydesk serial bridge %PORT%
python "%~dp0serial_bridge.py" %PORT% --baud %BAUD% --putty
