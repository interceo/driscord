@echo off
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "COMPOSE_DIR=%ROOT%\client-compose"

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
:: 1. CMake — server + legacy client + driscord_jni
:: ---------------------------------------------------------------------------
if not exist "%BUILD%\CMakeCache.txt" (
    echo =^> Configuring CMake...
    cmake -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% %FFMPEG_PATH% %OPENSSL_PATH%
    if errorlevel 1 ( echo CMake configuration failed. & exit /b 1 )
)

echo =^> Building C++...
cmake --build "%BUILD%"
if errorlevel 1 ( echo C++ build failed. & exit /b 1 )

:: Copy DLLs next to legacy client
set "CLIENT_DIR=%BUILD%\client"
if exist "C:\vcpkg\installed\x64-windows\bin\avutil*.dll" (
    copy /Y "C:\vcpkg\installed\x64-windows\bin\av*.dll" "%CLIENT_DIR%\" >nul 2>&1
    copy /Y "C:\vcpkg\installed\x64-windows\bin\sw*.dll" "%CLIENT_DIR%\" >nul 2>&1
)
if exist "C:\ffmpeg\bin\avutil*.dll" (
    copy /Y "C:\ffmpeg\bin\av*.dll" "%CLIENT_DIR%\" >nul 2>&1
    copy /Y "C:\ffmpeg\bin\sw*.dll" "%CLIENT_DIR%\" >nul 2>&1
)

echo     Server:         %BUILD%\server\driscord_server.exe
echo     Legacy client:  %BUILD%\client\driscord_client.exe
if exist "%BUILD%\client\driscord_jni.dll" (
    echo     JNI library:    %BUILD%\client\driscord_jni.dll
)

:: ---------------------------------------------------------------------------
:: Auto-detect JAVA_HOME for Gradle if not set
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
:: 2. Kotlin/Compose Desktop client
:: ---------------------------------------------------------------------------
echo.
echo =^> Building Kotlin/Compose client...

if not exist "%COMPOSE_DIR%\gradlew.bat" (
    echo     gradlew.bat not found ^— bootstrapping...
    where gradle >nul 2>&1
    if errorlevel 1 (
        echo     WARNING: Gradle not found. Install Gradle or run manually:
        echo       cd %COMPOSE_DIR% ^&^& gradle wrapper
        echo     Skipping Kotlin build.
        goto done
    )
    pushd "%COMPOSE_DIR%"
    gradle wrapper --quiet
    popd
)

set "DRISCORD_NATIVE_LIB_DIR=%BUILD%\client"

:: All Kotlin build outputs and Gradle dependency/plugin caches go to Z:\
set "DRISCORD_BUILDS_DIR=Z:\builds"
set "GRADLE_USER_HOME=Z:\gradle-home"

pushd "%COMPOSE_DIR%"
:: packageExe  — self-contained Windows installer (.exe) with bundled JRE
:: packageMsi  — MSI package
:: Both outputs land in %DRISCORD_BUILDS_DIR%\dist\
call gradlew.bat packageExe packageMsi --quiet -PbuildsDir="%DRISCORD_BUILDS_DIR%"
if errorlevel 1 ( echo Kotlin build failed. & popd & exit /b 1 )
popd
echo     Compose installer: %DRISCORD_BUILDS_DIR%\dist\windows\

:: ---------------------------------------------------------------------------
:: Package into zip archives on Z:\
:: ---------------------------------------------------------------------------
echo.
echo =^> Packaging...
set "STAGING=%BUILD%\staging"

:: Legacy client
if exist "%STAGING%\client" rd /s /q "%STAGING%\client"
mkdir "%STAGING%\client"
copy /Y "%BUILD%\client\driscord_client.exe" "%STAGING%\client\" >nul
copy /Y "%ROOT%\driscord.json" "%STAGING%\client\" >nul
if exist "%CLIENT_DIR%\*.dll" copy /Y "%CLIENT_DIR%\*.dll" "%STAGING%\client\" >nul
powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\client\*' -DestinationPath 'Z:\driscord_client.zip' -Force" 2>nul

:: Compose client — copy the ready-made installer directly to Z:\
for /f "delims=" %%F in ('dir /b /s "%DRISCORD_BUILDS_DIR%\dist\windows\*.exe" 2^>nul') do (
    copy /Y "%%F" "Z:\driscord_compose_setup.exe" >nul
    echo     Installer copied: Z:\driscord_compose_setup.exe
)
for /f "delims=" %%F in ('dir /b /s "%DRISCORD_BUILDS_DIR%\dist\windows\*.msi" 2^>nul') do (
    copy /Y "%%F" "Z:\driscord_compose.msi" >nul
    echo     MSI copied:       Z:\driscord_compose.msi
)

:: Server
if exist "%STAGING%\server" rd /s /q "%STAGING%\server"
mkdir "%STAGING%\server"
copy /Y "%BUILD%\server\driscord_server.exe" "%STAGING%\server\" >nul
copy /Y "%ROOT%\driscord.json" "%STAGING%\server\" >nul
powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\server\*' -DestinationPath 'Z:\driscord_server.zip' -Force" 2>nul

rd /s /q "%STAGING%" >nul 2>&1

:done
echo.
echo =^> Done
