@echo off
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
if exist build rmdir /s /q build
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo CMake configuration failed
    exit /b 1
)
cmake --build build --config Release
