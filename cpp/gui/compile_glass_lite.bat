@echo off
chcp 65001 >nul
set VSLANG=1033

call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"

echo Compiling Glass Window Lite...

cl.exe /nologo /O2 /WX- /MD /D "NOMINMAX" /D "WIN32_LEAN_AND_MEAN" ^
    /D "UNICODE" /D "_UNICODE" ^
    glass_window_lite.cpp ^
    /Fe:glass_window_lite.exe ^
    /link user32.lib gdi32.lib dwmapi.lib

if %ERRORLEVEL% == 0 (
    echo.
    echo Build succeeded!
    echo Running...
    start glass_window_lite.exe
) else (
    echo Build failed!
)
