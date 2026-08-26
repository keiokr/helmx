@echo off
REM helm-x build script — MinGW static link
setlocal

cd /d %~dp0

REM 1. generate embedded resources
python tools\embed.py || goto :error

REM 2. configure + build
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || goto :error
cmake --build build -j || goto :error

REM 3. optional UPX compress (smaller single exe)
if exist "C:\Tools\upx\upx.exe" (
    "C:\Tools\upx\upx.exe" --best --lzma build\helmx.exe >nul 2>&1
    echo [OK] upx compressed build\helmx.exe
)

echo.
echo [OK] build\helmx.exe
echo [OK] deps check: objdump -p build\helmx.exe ^| findstr "DLL Name"
goto :eof

:error
echo [FAIL] build error
exit /b 1
