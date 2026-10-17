@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Visual C++ Build Tools was not found. Run this file from an "x86 Native Tools Command Prompt".
  exit /b 1
)
rc /nologo app.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /O2 /MT /W4 /DUNICODE /D_UNICODE main.cpp app.res /link /SUBSYSTEM:WINDOWS /OUT:JedkxClicker.exe user32.lib comctl32.lib gdi32.lib uxtheme.lib dwmapi.lib
if exist main.obj del /q main.obj
if exist app.res del /q app.res
