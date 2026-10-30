@echo off
cd /d "%~dp0"
py BuildUI.py
if errorlevel 1 pause
