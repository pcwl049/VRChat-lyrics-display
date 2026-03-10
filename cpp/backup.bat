@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

:: VRChat Lyrics Display Backup Script
:: Creates timestamped backup of compiled executable and config

set "BACKUP_DIR=%~dp0backup"
set "BIN_DIR=%~dp0bin"

:: Use PowerShell for reliable date format
for /f "tokens=*" %%i in ('powershell -Command "Get-Date -Format 'yyyyMMdd_HHmmss'"') do set "DATE_STR=%%i"

:: Create backup subfolder with timestamp
set "BACKUP_PATH=%BACKUP_DIR%\%DATE_STR%"

echo ========================================
echo   VRChat Lyrics Display Backup
echo ========================================
echo.

if not exist "%BIN_DIR%\VRCLyricsDisplay.exe" (
    echo [ERROR] VRCLyricsDisplay.exe not found in bin folder
    echo Please compile first using compile_gui.bat
    pause
    exit /b 1
)

:: Create backup folder
if not exist "%BACKUP_PATH%" mkdir "%BACKUP_PATH%"

:: Copy files
echo [Backing up files...]
copy "%BIN_DIR%\VRCLyricsDisplay.exe" "%BACKUP_PATH%\" >nul
if exist "%BIN_DIR%\config_gui.json" copy "%BIN_DIR%\config_gui.json" "%BACKUP_PATH%\" >nul
if exist "%~dp0config_gui.json" copy "%~dp0config_gui.json" "%BACKUP_PATH%\" >nul

:: Create info file
echo Backup created: %DATE_STR% > "%BACKUP_PATH%\backup_info.txt"
echo. >> "%BACKUP_PATH%\backup_info.txt"

:: Get file size
for %%A in ("%BIN_DIR%\VRCLyricsDisplay.exe") do set "SIZE=%%~zA"
echo File size: !SIZE! bytes >> "%BACKUP_PATH%\backup_info.txt"
echo Source: %BIN_DIR%\VRCLyricsDisplay.exe >> "%BACKUP_PATH%\backup_info.txt"

echo.
echo [SUCCESS] Backup created:
echo   %BACKUP_PATH%
echo.
echo Files backed up:
echo   - VRCLyricsDisplay.exe
if exist "%BACKUP_PATH%\config_gui.json" echo   - config_gui.json
echo   - backup_info.txt
echo.

:: List all backups
echo ========================================
echo   Existing Backups
echo ========================================
dir /b /ad "%BACKUP_DIR%" 2>nul | sort /r
echo ========================================

pause
