@echo off
:: Launch the Driscord native binary (Kotlin/Native mingwX64).
:: Builds C++ + Kotlin/Native automatically if the binary is not found.
::
:: Usage:
::   scripts\run.bat [server-url]
::   scripts\run.bat ws://localhost:9001
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "BUILDS=%ROOT%\builds\kotlin"
set "BINARY=%BUILDS%\bin\mingwX64\driscordReleaseExecutable\driscord.exe"
set "NATIVE_LIB_DIR=%ROOT%\build\client"

:: ---------------------------------------------------------------------------
:: Auto-build if binary is missing
:: ---------------------------------------------------------------------------
if not exist "%BINARY%" (
    echo =^> Native binary not found ^— building...
    call "%~dp0build.bat"
    if errorlevel 1 (
        echo Build failed.
        exit /b 1
    )
)

if not exist "%BINARY%" (
    echo ERROR: Build succeeded but binary not found at:
    echo   %BINARY%
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Put the native DLL directory on PATH so the binary finds driscord_capi.dll
:: ---------------------------------------------------------------------------
set "PATH=%NATIVE_LIB_DIR%;%PATH%"

echo =^> Launching Driscord (Kotlin/Native mingwX64)
echo     Binary  : %BINARY%
echo     DLL dir : %NATIVE_LIB_DIR%
echo.

"%BINARY%" %*
