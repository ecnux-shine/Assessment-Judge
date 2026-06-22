@echo off
cl /EHsc aj.cpp psapi.lib
if errorlevel 1 (
    echo Build failed.
) else (
    echo Build succeeded: aj.exe
)
