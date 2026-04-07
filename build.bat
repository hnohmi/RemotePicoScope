@echo off
REM Build script for RemotePicoScope
REM Usage: build.bat         (configure + build)
REM        build.bat clean   (wipe build dir, full rebuild)
REM
REM Requires:
REM   - Visual Studio 2022 with C++ desktop workload
REM   - PICOSCOPE_SDK_PATH environment variable pointing to PicoScope SDK
REM
REM Note: If vcpkg install fails during configure, you may need to add an
REM       exclusion in Windows Defender for the vcpkg buildtrees directory.

call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set CMAKE="C:\Program Files\CMake\bin\cmake.exe"
if not defined PICOSCOPE_SDK_PATH set PICOSCOPE_SDK_PATH=C:\Program Files\Pico Technology\SDK

if "%1"=="clean" (
    echo Cleaning build directory...
    rmdir /s /q build 2>nul
)

echo Configuring...
%CMAKE% -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
if errorlevel 1 goto :error

echo Building...
%CMAKE% --build build --config Release
if errorlevel 1 goto :error

echo.
echo Build succeeded:
echo   build\Release\RemotePicoScope.exe
echo   build\Release\picoscope-cli.exe
goto :eof

:error
echo Build failed!
exit /b 1
