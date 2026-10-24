@echo off
title SmartMed - Khoi Dong He Thong
color 0B

echo ===============================================================
echo        HE THONG HOP THUOC THONG MINH SMARTMED - KHOI DONG
echo ===============================================================
echo.

:: 1. Kiem tra Mosquitto MQTT Broker (Port 1883)
echo [1/3] Kiem tra Mosquitto MQTT Broker (Cong 1883)...
netstat -ano | findstr :1883 >nul
if %errorlevel% equ 0 (
    echo       ^> Mosquitto Broker dang hoat dong tot!
) else (
    echo       ^> Dang thu khoi dong dich vu Mosquitto...
    powershell -Command "Start-Service mosquitto" >nul 2>&1
    ping 127.0.0.1 -n 2 >nul
    netstat -ano | findstr :1883 >nul
    if %errorlevel% equ 0 (
        echo       ^> Da khoi dong Mosquitto thanh cong!
    ) else (
        echo       ^> [CANH BAO] Khong tim thay Mosquitto tren cong 1883.
        echo         Vui long bat Mosquitto de nhan du lieu tu ESP32.
    )
)

:: 2. Giai phong Port 8000 neu bi treo tu phien cu
echo.
echo [2/3] Khoi dong Backend Server (FastAPI + WebSocket)...
for /f "tokens=5" %%p in ('netstat -aon ^| findstr :8000 ^| findstr LISTENING') do (
    taskkill /F /PID %%p >nul 2>&1
)

:: Chay Backend trong cua so rieng
cd /d "%~dp0backend"
start "SmartMed Backend Server" cmd /k "title SmartMed Backend Server && color 0A && python main.py"

echo       ^> Dang khoi dong Backend va ket noi MQTT...
ping 127.0.0.1 -n 3 >nul

:: 3. Mo Web Dashboard tren trinh duyet
echo.
echo [3/3] Dang mo Web Dashboard tren trinh duyet...
start http://localhost:8000

echo.
echo ===============================================================
echo   KHOI DONG HOAN TAT! HE THONG DA SAN SANG HOAT DONG.
echo   - Web Dashboard : http://localhost:8000
echo   - Backend Server: Cua so mau xanh la dang chay
echo   - De TAT he thong: Chay file 'DUNG_HE_THONG.bat'
echo ===============================================================
echo.
ping 127.0.0.1 -n 4 >nul
