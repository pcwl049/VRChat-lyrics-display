@echo off
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvarsall.bat" x64
echo Compiling mica_test.cpp ...
cl.exe /nologo /W3 /O2 /utf-8 /DUNICODE /D_UNICODE /Fe"mica_test.exe" mica_test.cpp user32.lib gdi32.lib dwmapi.lib shell32.lib
if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    start mica_test.exe
) else (
    echo Build failed!
)