@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   VRChat Lyrics Display Build
echo ========================================
echo.

call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Cannot initialize Visual Studio environment
    exit /b 1
)

cd /d "%~dp0"
if not exist ..\bin mkdir ..\bin

echo [Compiling...]
cl /std:c++20 /EHsc /W3 /O2 /MD /utf-8 ^
    main_gui.cpp glass_window.cpp moekoe_ws.cpp netease_ws.cpp ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib ^
    /Fe:..\bin\VRCLyricsDisplay.exe

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo   Build Success!
    echo   Output: ..\bin\VRCLyricsDisplay.exe
    echo ========================================
    echo.
    echo Make sure MoeKoeMusic or Netease Music is running
) else (
    echo.
    echo [ERROR] Build failed
)

del /q *.obj 2>nul

endlocal
