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

echo [Compiling modular components...]

:: 编译通用模块
echo   - common/logger.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. common/logger.cpp /Fo:build\obj\logger.obj

echo   - ui/draw_helpers.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. ui/draw_helpers.cpp /Fo:build\obj\draw_helpers.obj

:: 编译主程序（暂时使用单文件版本，待后续拆分）
echo   - main_gui.cpp (monolithic)
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /await /I. main_gui.cpp /Fo:build\obj\main_gui.obj

echo   - glass_window.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. glass_window.cpp /Fo:build\obj\glass_window.obj

echo   - moekoe_ws.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. moekoe_ws.cpp /Fo:build\obj\moekoe_ws.obj

echo   - netease_ws.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. netease_ws.cpp /Fo:build\obj\netease_ws.obj

echo   - smtc_client.cpp
cl /c /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /I. smtc_client.cpp /Fo:build\obj\smtc_client.obj

:: 链接
echo [Linking...]
link /OUT:main_gui.exe /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
    build\obj\main_gui.obj build\obj\glass_window.obj build\obj\moekoe_ws.obj ^
    build\obj\netease_ws.obj build\obj\smtc_client.obj ^
    build\obj\logger.obj build\obj\draw_helpers.obj ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib ^
    gdiplus.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib dxgi.lib wbemuuid.lib winmm.lib

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo   Build Success!
    echo   Output: main_gui.exe
    echo ========================================
) else (
    echo.
    echo [ERROR] Build failed
)

:: 清理临时文件
del /q *.obj 2>nul

endlocal
