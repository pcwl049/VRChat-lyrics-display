@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   VRChat Lyrics Display - Unit Tests
echo ========================================
echo.

call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Cannot initialize Visual Studio environment
    exit /b 1
)

cd /d "%~dp0"

echo [Compiling tests...]
cl /EHsc /O2 /DUNICODE /D_UNICODE test_unit.cpp /Fe:test_unit.exe /link advapi32.lib winhttp.lib ws2_32.lib

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo   Running Tests...
    echo ========================================
    echo.
    test_unit.exe
) else (
    echo.
    echo [ERROR] Compilation failed
)

del /q *.obj 2>nul

endlocal
