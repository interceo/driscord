@echo off
:: Launch the Kotlin/Compose Desktop client.
:: Builds the native JNI library first if driscord_jni.dll is not found.
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "COMPOSE_DIR=%ROOT%\client-compose"
set "NATIVE_DIR=%BUILD%\client"

:: ---------------------------------------------------------------------------
:: Check for the JNI DLL; build if missing
:: ---------------------------------------------------------------------------
if not exist "%NATIVE_DIR%\driscord_jni.dll" (
    echo =^> driscord_jni.dll not found ^— building C++ first...
    call "%~dp0build.bat"
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
)

if not exist "%NATIVE_DIR%\driscord_jni.dll" (
    echo ERROR: driscord_jni.dll not found even after build.
    echo   Make sure JNI is available ^(install a JDK and set JAVA_HOME^).
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Bootstrap Gradle wrapper if needed
:: ---------------------------------------------------------------------------
if not exist "%COMPOSE_DIR%\gradlew.bat" (
    echo =^> gradlew.bat not found ^— bootstrapping Gradle wrapper...
    where gradle >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Gradle is not installed.
        echo   Run:  cd %COMPOSE_DIR% ^&^& gradle wrapper
        exit /b 1
    )
    pushd "%COMPOSE_DIR%"
    gradle wrapper --quiet
    popd
)

:: ---------------------------------------------------------------------------
:: Run
:: ---------------------------------------------------------------------------
echo =^> Launching Driscord (Compose) ...
echo     Native lib dir: %NATIVE_DIR%

set "DRISCORD_NATIVE_LIB_DIR=%NATIVE_DIR%"
pushd "%COMPOSE_DIR%"
call gradlew.bat run
popd
