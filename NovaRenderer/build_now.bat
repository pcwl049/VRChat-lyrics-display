@echo off
set VSCMD_START_DIR=%CD%
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvarsall.bat" x64
cd D:\Projects\MusicDisplay\NovaRenderer\build
D:\Tools\ninja.exe