@echo off
REM Packaging script for RemotePicoScope
REM Usage: package.bat
REM
REM Builds the project (if needed) and assembles a release package under
REM    dist\RemotePicoScope-<version>\
REM containing two subfolders:
REM    app\     - RemotePicoScope.exe, profiles, README, LICENSE
REM    remote\  - picoscope-cli.exe and PICOSCOPE-CLI.md
REM
REM The version number is read from src\core\Version.h (Version::APP).

setlocal enabledelayedexpansion

REM --- Ensure the project is built ---
if not exist "build\Release\RemotePicoScope.exe" goto :build
if not exist "build\Release\picoscope-cli.exe"  goto :build
goto :have_build

:build
echo Building project first...
call build.bat
if errorlevel 1 goto :error

:have_build

REM --- Extract APP version from Version.h ---
set "APP_VERSION="
for /f "tokens=2 delims==" %%A in ('findstr /c:"APP " src\core\Version.h') do (
    set "_line=%%A"
    REM Trim quotes and semicolons
    set "_line=!_line:"=!"
    set "_line=!_line:;=!"
    for /f "tokens=1" %%B in ("!_line!") do set "APP_VERSION=%%B"
)
if "%APP_VERSION%"=="" (
    echo Error: Could not extract APP version from src\core\Version.h
    exit /b 1
)

set "PKG_NAME=RemotePicoScope-%APP_VERSION%"
set "PKG_DIR=dist\%PKG_NAME%"

echo.
echo Packaging %PKG_NAME% ...

REM --- Clean and create package layout ---
if exist "%PKG_DIR%" rmdir /s /q "%PKG_DIR%"
mkdir "%PKG_DIR%\app"
mkdir "%PKG_DIR%\app\profiles"
mkdir "%PKG_DIR%\remote"

REM --- App folder: GUI exe + profiles + README + LICENSE ---
copy /y "build\Release\RemotePicoScope.exe" "%PKG_DIR%\app\" >nul || goto :error
copy /y "profiles\*.json"                    "%PKG_DIR%\app\profiles\" >nul || goto :error
copy /y "README.md"                          "%PKG_DIR%\app\" >nul || goto :error
copy /y "ARCHITECTURE.md"                    "%PKG_DIR%\app\" >nul || goto :error
copy /y "LICENSE"                            "%PKG_DIR%\app\" >nul || goto :error

REM --- Remote folder: CLI exe + doc + LICENSE ---
copy /y "build\Release\picoscope-cli.exe"    "%PKG_DIR%\remote\" >nul || goto :error
copy /y "src\remote\PICOSCOPE-CLI.md"        "%PKG_DIR%\remote\" >nul || goto :error
copy /y "LICENSE"                            "%PKG_DIR%\remote\" >nul || goto :error

REM --- Zip the package if PowerShell is available ---
set "ZIP_PATH=dist\%PKG_NAME%.zip"
if exist "%ZIP_PATH%" del /q "%ZIP_PATH%"
powershell -NoProfile -Command "Compress-Archive -Path '%PKG_DIR%\*' -DestinationPath '%ZIP_PATH%' -Force" >nul 2>&1
if errorlevel 1 (
    echo Warning: zip creation failed ^(PowerShell not available?^). Package folder is still usable.
) else (
    echo Created: %ZIP_PATH%
)

echo.
echo Package layout:
echo   %PKG_DIR%\app\RemotePicoScope.exe
echo   %PKG_DIR%\app\profiles\*.json
echo   %PKG_DIR%\app\README.md
echo   %PKG_DIR%\app\ARCHITECTURE.md
echo   %PKG_DIR%\app\LICENSE
echo   %PKG_DIR%\remote\picoscope-cli.exe
echo   %PKG_DIR%\remote\PICOSCOPE-CLI.md
echo   %PKG_DIR%\remote\LICENSE
echo.
echo Package succeeded.
goto :eof

:error
echo Packaging failed!
exit /b 1
