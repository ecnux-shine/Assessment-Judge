@echo off
REM Try MSVC first
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with MSVC...
    rc /fo resource.res assets\resource.rc
    cl /EHsc aj.cpp resource.res psapi.lib
    if errorlevel 1 (
        echo Build failed with MSVC.
        exit /b 1
    ) else (
        echo Build succeeded: aj.exe
        exit /b 0
    )
)

REM Fall back to MinGW g++
echo Compiling with MinGW g++...
windres assets\resource.rc -o resource.res
g++ aj.cpp resource.res -std=c++17 -O2 -o aj.exe -lpsapi
if errorlevel 1 (
    echo Build failed with MinGW.
    exit /b 1
) else (
    echo Build succeeded: aj.exe
    exit /b 0
)
