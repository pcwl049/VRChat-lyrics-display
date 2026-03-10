@echo off
setlocal enabledelayedexpansion

echo ========================================
echo   Nova Renderer Build Script
echo ========================================
echo.

:: 设置环境
set VSLANG=1033
set BUILD_DIR=build
set BUILD_TYPE=Release

:: 检查参数
:parse_args
if "%~1"=="" goto end_parse
if /i "%~1"=="debug" set BUILD_TYPE=Debug
if /i "%~1"=="release" set BUILD_TYPE=Release
if /i "%~1"=="clean" (
    echo Cleaning build directory...
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
    goto end_parse
)
shift
goto parse_args
:end_parse

echo Build Type: %BUILD_TYPE%
echo.

:: 创建构建目录
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

:: 查找 Visual Studio
set "VS_PATH="
set "VS_EDITION="

for %%e in (Enterprise Professional Community) do (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\%%e"
        set "VS_EDITION=%%e"
        goto :found_vs
    )
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%e"
        set "VS_EDITION=%%e"
        goto :found_vs
    )
)

echo ERROR: Visual Studio 2022 not found!
echo Please install Visual Studio 2022 with C++ workload.
exit /b 1

:found_vs
echo Found Visual Studio 2022 !VS_EDITION!

:: 设置 MSVC 环境
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" > nul

:: 查找 Vulkan SDK
set "VULKAN_SDK="
if exist "%VULKAN_SDK%" goto :vulkan_found

for /d %%d in ("C:\VulkanSDK\*") do (
    set "VULKAN_SDK=%%d"
)

if not exist "%VULKAN_SDK%" (
    echo ERROR: Vulkan SDK not found!
    echo Please install Vulkan SDK from https://vulkan.lunarg.com/
    exit /b 1
)

:vulkan_found
echo Vulkan SDK: %VULKAN_SDK%

:: 查找 CMake
where cmake > nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found!
    echo Please install CMake or add it to PATH.
    exit /b 1
)

echo.
echo Running CMake...
echo.

cd %BUILD_DIR%

cmake .. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DVULKAN_SDK="%VULKAN_SDK%" ^
    -DNOVA_BUILD_EXAMPLES=ON ^
    -DNOVA_ENABLE_VALIDATION=ON

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed!
    cd ..
    exit /b 1
)

echo.
echo Building...
echo.

cmake --build . --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    cd ..
    exit /b 1
)

cd ..

echo.
echo ========================================
echo   Build Successful!
echo ========================================
echo.
echo Output: %BUILD_DIR%\%BUILD_TYPE%\NovaExample.exe
echo.

if exist "%BUILD_DIR%\%BUILD_TYPE%\NovaExample.exe" (
    echo Run the example? [Y/N]
    set /p run_choice=
    if /i "!run_choice!"=="Y" (
        echo.
        echo Starting example...
        start "" "%BUILD_DIR%\%BUILD_TYPE%\NovaExample.exe"
    )
)

endlocal
