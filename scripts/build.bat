@echo off
:: Unified Driscord build script (Windows — client only).
::
:: Usage:
::   scripts\build.bat                # build client (release)
::   scripts\build.bat --debug        # build client (debug)
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "COMPOSE_DIR=%ROOT%\client-compose"
set "BUILD_TYPE=Release"

:: --- Parse flags ---
for %%A in (%*) do (
    if "%%A"=="--debug" set "BUILD_TYPE=Debug"
    if "%%A"=="--release" set "BUILD_TYPE=Release"
)

if "%BUILD_TYPE%"=="Debug" (
    set "TYPE_LOWER=debug"
) else (
    set "TYPE_LOWER=release"
)

set "BUILD=%ROOT%\.builds\cmake\windows-%TYPE_LOWER%"
set "OUT=%ROOT%\.builds\client\windows\%TYPE_LOWER%"

:: ---------------------------------------------------------------------------
:: Detect vcpkg BEFORE vcvars
:: ---------------------------------------------------------------------------
set "VCPKG_EXE="
set "TOOLCHAIN="
set "VCPKG_TRIPLET=x64-windows-static"
if exist "C:\vcpkg\vcpkg.exe" (
    set "VCPKG_EXE=C:\vcpkg\vcpkg.exe"
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
)

:: ---------------------------------------------------------------------------
:: Activate MSVC if needed
:: ---------------------------------------------------------------------------
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo =^> Activating MSVC environment...
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    ) else (
        echo ERROR: Visual Studio not found.
        exit /b 1
    )
)
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found after vcvars activation.
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: vcpkg dependencies
:: ---------------------------------------------------------------------------
if defined VCPKG_EXE (
    echo =^> Checking vcpkg dependencies...
    "%VCPKG_EXE%" install openssl:%VCPKG_TRIPLET% ffmpeg[avcodec,avformat,avdevice,swscale]:%VCPKG_TRIPLET% boost-beast:%VCPKG_TRIPLET% boost-asio:%VCPKG_TRIPLET% --no-print-usage 2>nul
    if errorlevel 1 (
        set "VCPKG_TRIPLET=x64-windows"
        "%VCPKG_EXE%" install openssl:%VCPKG_TRIPLET% ffmpeg[avcodec,avformat,avdevice,swscale]:%VCPKG_TRIPLET% boost-beast:%VCPKG_TRIPLET% boost-asio:%VCPKG_TRIPLET% --no-print-usage 2>nul
    )
)

:: Manual FFmpeg / OpenSSL fallback
set "FFMPEG_PATH="
if not defined VCPKG_EXE (
    if exist "C:\ffmpeg\lib" set "FFMPEG_PATH=-DCMAKE_PREFIX_PATH=C:\ffmpeg"
)
set "OPENSSL_PATH="
if not defined VCPKG_EXE (
    if exist "C:\Program Files\OpenSSL-Win64\include\openssl" set "OPENSSL_PATH=-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
)

:: ---------------------------------------------------------------------------
:: 1. CMake — core + JNI
:: ---------------------------------------------------------------------------
if not exist "%BUILD%\CMakeCache.txt" (
    echo =^> Configuring CMake (%BUILD_TYPE%)...
    cmake -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBUILD_SERVER=OFF %TOOLCHAIN% -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% %FFMPEG_PATH% %OPENSSL_PATH%
    if errorlevel 1 ( echo CMake configuration failed. & exit /b 1 )
)

echo =^> Building C++ (%BUILD_TYPE%)...
cmake --build "%BUILD%"
if errorlevel 1 ( echo C++ build failed. & exit /b 1 )

:: Copy DLLs next to core lib
set "CORE_DIR=%BUILD%\core"
if exist "C:\vcpkg\installed\x64-windows\bin\avutil*.dll" (
    copy /Y "C:\vcpkg\installed\x64-windows\bin\av*.dll" "%CORE_DIR%\" >nul 2>&1
    copy /Y "C:\vcpkg\installed\x64-windows\bin\sw*.dll" "%CORE_DIR%\" >nul 2>&1
)
if exist "C:\ffmpeg\bin\avutil*.dll" (
    copy /Y "C:\ffmpeg\bin\av*.dll" "%CORE_DIR%\" >nul 2>&1
    copy /Y "C:\ffmpeg\bin\sw*.dll" "%CORE_DIR%\" >nul 2>&1
)

if exist "%CORE_DIR%\core.dll" (
    echo     JNI library: %CORE_DIR%\core.dll
)

:: ---------------------------------------------------------------------------
:: Auto-detect JAVA_HOME
:: ---------------------------------------------------------------------------
if not defined JAVA_HOME (
    for /f "tokens=2*" %%a in (
        'reg query "HKLM\SOFTWARE\JavaSoft\JDK" /v CurrentVersion 2^>nul'
    ) do set "_JDK_VER=%%b"
    if defined _JDK_VER (
        for /f "tokens=2*" %%a in (
            'reg query "HKLM\SOFTWARE\JavaSoft\JDK\!_JDK_VER!" /v JavaHome 2^>nul'
        ) do set "JAVA_HOME=%%b"
    )
)
if not defined JAVA_HOME (
    for %%D in (
        "C:\Program Files\Eclipse Adoptium"
        "C:\Program Files\Java"
        "C:\Program Files\Microsoft"
        "C:\Program Files\Amazon Corretto"
    ) do (
        if exist "%%~D" (
            for /d %%J in ("%%~D\jdk-2*" "%%~D\jdk21*") do (
                if exist "%%~J\bin\java.exe" ( set "JAVA_HOME=%%~J" )
            )
        )
    )
)
if defined JAVA_HOME (
    set "PATH=%JAVA_HOME%\bin;%PATH%"
    echo     JAVA_HOME: %JAVA_HOME%
)

:: ---------------------------------------------------------------------------
:: 2. Kotlin/Compose fatJar
:: ---------------------------------------------------------------------------
echo.
echo =^> Building Kotlin/Compose client (%BUILD_TYPE%)...

if not exist "%COMPOSE_DIR%\gradlew.bat" (
    echo     gradlew.bat not found ^— bootstrapping...
    where gradle >nul 2>&1
    if errorlevel 1 (
        echo     WARNING: Gradle not found. Skipping Kotlin build.
        goto stage
    )
    pushd "%COMPOSE_DIR%"
    gradle wrapper --quiet
    popd
)

set "DRISCORD_NATIVE_LIB_DIR=%CORE_DIR%"
set "DRISCORD_BUILDS_DIR=%ROOT%\.builds"
set "GRADLE_USER_HOME=%ROOT%\.builds\gradle-home"

pushd "%COMPOSE_DIR%"
call gradlew.bat fatJar --quiet -PbuildsDir="%DRISCORD_BUILDS_DIR%" -PclientBuildDir="%OUT%"
if errorlevel 1 ( echo Kotlin build failed. & popd & exit /b 1 )
popd

:: ---------------------------------------------------------------------------
:: 3. Stage artifacts
:: ---------------------------------------------------------------------------
:stage
if not exist "%OUT%" mkdir "%OUT%"

if exist "%CORE_DIR%\core.dll" copy /Y "%CORE_DIR%\core.dll" "%OUT%\" >nul 2>&1
if exist "%CORE_DIR%\*.dll"    copy /Y "%CORE_DIR%\*.dll"    "%OUT%\" >nul 2>&1
copy /Y "%ROOT%\driscord.json" "%OUT%\" >nul

:: Launcher
(
    echo @echo off
    echo cd /d "%%~dp0"
    echo java -Djava.library.path=. -jar driscord.jar %%*
) > "%OUT%\driscord.bat"

echo.
echo =^> Build complete: %OUT%
