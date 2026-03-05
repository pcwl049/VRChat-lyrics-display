@echo off
title Kugou Music - VRChat

echo ========================================
echo   Kugou Music - VRChat Chatbox Display
echo ========================================
echo.

python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python not found, please install Python first
    pause
    exit /b 1
)

echo [INFO] Checking dependencies...
pip install -r requirements.txt -q

echo.
echo [INFO] Starting program...
echo.

python main.py

pause