@echo off
title SmartMed - Dung He Thong
color 0C

echo ===============================================================
echo          DANG DUNG HE THONG HOP THUOC SMARTMED...
echo ===============================================================
echo.

echo [1/2] Dang tat Backend Server va giai phong cong 8000...
for /f "tokens=5" %%p in ('netstat -aon ^| findstr :8000 ^| findstr LISTENING') do (
    taskkill /F /PID %%p >nul 2>&1
)

echo [2/2] Dang dong cua so console Backend...
taskkill /FI "WINDOWTITLE eq SmartMed Backend Server*" /F >nul 2>&1

echo.
echo ===============================================================
echo   DA DUNG HE THONG AN TOAN! CONG 8000 DA DUOC GIAI PHONG.
echo ===============================================================
echo.
ping 127.0.0.1 -n 3 >nul
