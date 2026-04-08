@echo off
:: Unified Driscord launcher (Windows — client only).
::
:: Usage:
::   scripts\run.bat                # run client (release)
::   scripts\run.bat --debug        # run client (debug)
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "COMPOSE_DIR=%ROOT%\client-compose"
set "BUILD_TYPE=release"

:: --- Parse flags ---
for %%A in (%*) do (
    if "%%A"=="--debug" set "BUILD_TYPE=debug"
    if "%%A"=="--release" set "BUILD_TYPE=release"
)

set "BUILD=%ROOT%\.builds\cmake\windows-%BUILD_TYPE%"
set "NATIVE_DIR=%BUILD%\core"

:: ---------------------------------------------------------------------------
:: Auto-detect JAVA_HOME if not set
:: ---------------------------------------------------------------------------
if defined JAVA_HOME goto java_ok
if defined JDK_HOME ( set "JAVA_HOME=%JDK_HOME%" & goto java_ok )

echo =^> JAVA_HOME not set, searching for JDK...

for /f "tokens=2*" %%a in (
    'reg query "HKLM\SOFTWARE\JavaSoft\JDK" /v CurrentVersion 2^>nul'
) do set "_JDK_VER=%%b"
if defined _JDK_VER (
    for /f "tokens=2*" %%a in (
        'reg query "HKLM\SOFTWARE\JavaSoft\JDK\%_JDK_VER%" /v JavaHome 2^>nul'
    ) do set "JAVA_HOME=%%b"
)
if defined JAVA_HOME goto java_ok

for %%D in (
    "C:\Program Files\Eclipse Adoptium"
    "C:\Program Files\Java"
    "C:\Program Files\Microsoft"
    "C:\Program Files\Amazon Corretto"
    "C:\Program Files\BellSoft"
    "C:\Program Files\Azul Systems\Zulu"
    "C:\Program Files\SapMachine\JDK"
) do (
    if exist "%%~D" (
        for /d %%J in ("%%~D\jdk-2*") do (
            if exist "%%~J\bin\java.exe" (
                set "JAVA_HOME=%%~J"
                goto java_ok
            )
        )
        for /d %%J in ("%%~D\jdk21*" "%%~D\jdk-21*") do (
            if exist "%%~J\bin\java.exe" (
                set "JAVA_HOME=%%~J"
                goto java_ok
            )
        )
    )
)

where java.exe >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%p in ('where java.exe') do (
        set "_JAVA_BIN=%%~dpp"
        set "JAVA_HOME=!_JAVA_BIN:~0,-1!"
        for %%x in ("!JAVA_HOME!") do set "JAVA_HOME=%%~dpx"
        set "JAVA_HOME=!JAVA_HOME:~0,-1!"
        goto java_ok
    )
)

echo ERROR: JDK not found.
echo   Install JDK 21 from https://adoptium.net and set JAVA_HOME, or add java to PATH.
exit /b 1

:java_ok
set "PATH=%JAVA_HOME%\bin;%PATH%"
echo     JAVA_HOME: %JAVA_HOME%

:: ---------------------------------------------------------------------------
:: Check for the JNI DLL; build if missing
:: ---------------------------------------------------------------------------
if not exist "%NATIVE_DIR%\core.dll" (
    echo =^> core.dll not found ^— building C++ first...
    if "%BUILD_TYPE%"=="debug" (
        call "%~dp0build.bat" --debug
    ) else (
        call "%~dp0build.bat"
    )
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
)

if not exist "%NATIVE_DIR%\core.dll" (
    echo ERROR: core.dll not found even after build.
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Bootstrap Gradle wrapper if needed
:: ---------------------------------------------------------------------------
if not exist "%COMPOSE_DIR%\gradlew.bat" (
    echo =^> Bootstrapping Gradle wrapper...
    where gradle >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Gradle not installed. Run:  cd %COMPOSE_DIR% ^&^& gradle wrapper
        exit /b 1
    )
    pushd "%COMPOSE_DIR%"
    gradle wrapper --quiet
    popd
)

:: ---------------------------------------------------------------------------
:: Run
:: ---------------------------------------------------------------------------
echo =^> Launching Driscord (%BUILD_TYPE%) ...
echo     Native lib dir: %NATIVE_DIR%

set "DRISCORD_NATIVE_LIB_DIR=%NATIVE_DIR%"
pushd "%COMPOSE_DIR%"
call gradlew.bat run
popd
