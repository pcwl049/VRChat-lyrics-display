@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   MoeKoeVRChat C++ Build (Test)
echo ========================================
echo.

:: Setup Visual Studio environment
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Cannot initialize Visual Studio environment
    exit /b 1
)

:: Change to project directory
cd /d "%~dp0"

:: Create output directory
if not exist bin mkdir bin

echo [Compiling test version...]
cl /std:c++20 /EHsc /W3 /O2 /MD /I"third_party" src\main.cpp winhttp.lib runtimeobject.lib windowsapp.lib ws2_32.lib ole32.lib /Fe:bin\test.exe

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo   Build Success!
    echo   Output: bin\test.exe
    echo ========================================
    echo.
    echo Running test...
    bin\test.exe
) else (
    echo.
    echo [ERROR] Build failed
)

:: Cleanup
del /q *.obj 2>nul

endlocal
