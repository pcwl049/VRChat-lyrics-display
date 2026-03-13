@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   VRChat Lyrics Display Build
echo   Modular Version
echo ========================================
echo.

call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Cannot initialize Visual Studio environment
    exit /b 1
)

cd /d "%~dp0"

echo [Compiling...]

cl /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /await /I. ^
    common/logger.cpp common/string_utils.cpp common/utils.cpp common/theme.cpp common/config_manager.cpp ui/draw_helpers.cpp ui/custom_dialog.cpp ui/window_utils.cpp core/osc_manager.cpp core/perf_monitor.cpp core/hardware_detect.cpp core/update_checker.cpp core/lyrics_search.cpp main_gui.cpp glass_window.cpp moekoe_ws.cpp netease_ws.cpp smtc_client.cpp ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib gdiplus.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib dxgi.lib wbemuuid.lib winmm.lib advapi32.lib ^
    /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OUT:main_gui.exe

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo   Build Success!
    echo ========================================
) else (
    echo.
    echo [ERROR] Build failed
)

del /q *.obj 2>nul

endlocal
