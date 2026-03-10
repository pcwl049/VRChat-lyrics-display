@echo off
set VULKAN_SDK=E:\Vulkan_SDK
set VSLANG=1033
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"
cd /d D:\Projects\MusicDisplay\NovaRenderer
if not exist build (
    cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)
ninja -C build

