@echo off
chcp 65001 >nul
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"
if not exist "D:\Project\音乐显示\cpp\bin" mkdir "D:\Project\音乐显示\cpp\bin"
echo Compiling GUI...
cl /std:c++20 /EHsc /W3 /O2 /MD /I"D:\Project\音乐显示\cpp\third_party" "D:\Project\音乐显示\cpp\gui\main_gui.cpp" "D:\Project\音乐显示\cpp\gui\moekoe_ws.cpp" winhttp.lib runtimeobject.lib windowsapp.lib ws2_32.lib ole32.lib user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib psapi.lib /Fe:"D:\Project\音乐显示\cpp\bin\moekoe_gui.exe"
echo Done. Exit code: %errorlevel%