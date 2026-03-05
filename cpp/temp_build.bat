@echo off
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\Project\ÒôÀÖÏÔÊ¾\cpp
if not exist bin mkdir bin
echo Compiling...
cl /std:c++20 /EHsc /W3 /O2 /MD /I"third_party" gui\main_gui.cpp gui\moekoe_ws.cpp winhttp.lib runtimeobject.lib windowsapp.lib ws2_32.lib ole32.lib user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib psapi.lib /Fe:bin\moekoe_gui.exe
echo Exit code: %errorlevel%
del /q *.obj 2>nul