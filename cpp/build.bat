@echo off
chcp 65001 >nul
echo ========================================
echo   MoeKoeVRChat C++ 构建脚本
echo ========================================
echo.

:: 检查 Visual Studio
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 Visual Studio 编译器
    echo 请在 "Developer Command Prompt for Visual Studio" 中运行此脚本
    pause
    exit /b 1
)

:: 创建构建目录
if not exist build mkdir build
cd build

:: CMake 配置
echo [1/2] CMake 配置...
cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo [错误] CMake 配置失败
    cd ..
    pause
    exit /b 1
)

:: CMake 构建
echo.
echo [2/2] 编译中...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo [错误] 编译失败
    cd ..
    pause
    exit /b 1
)

cd ..

echo.
echo ========================================
echo   构建完成！
echo   输出: bin\Release\MoeKoeVRChat.exe
echo ========================================
pause
