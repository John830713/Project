@echo off
del /s /q *.d
del /s /q *.o
del /q GeneratedBuild.mk 2>nul
del /q GeneratedIcon.ico 2>nul
del /q GeneratedIcon.rc 2>nul
del /q GeneratedModuleRegistry.cpp 2>nul
del /q GeneratedModuleRegistry.h 2>nul
del /q Translation\zh-TW.ini 2>nul
del /q Build.log 2>nul
echo.