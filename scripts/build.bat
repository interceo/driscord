@echo off
setlocal

set ROOT=%~dp0..
set BUILD=%ROOT%\build

:: --- auto-detect vcpkg toolchain ---
if defined VCPKG_ROOT (
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
) else if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
) else (
    set "TOOLCHAIN="
)

:: --- auto-detect FFmpeg from BtbN builds ---
if exist "C:\ffmpeg\lib" (
    set "FFMPEG_PATH=-DCMAKE_PREFIX_PATH=C:\ffmpeg"
) else (
    set "FFMPEG_PATH="
)

:: --- auto-detect OpenSSL (winget ShiningLight install) ---
if exist "C:\Program Files\OpenSSL-Win64" (
    set "OPENSSL_PATH=-DOPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64"
) else (
    set "OPENSSL_PATH="
)

:: --- pick generator: Ninja if available, else NMake ---
where ninja >nul 2>&1
if %errorlevel%==0 (
    set "GENERATOR=-G Ninja"
) else (
    set "GENERATOR=-G \"NMake Makefiles\""
)

if not exist "%BUILD%\CMakeCache.txt" (
    echo ==^> Configuring CMake...
    cmake -S "%ROOT%" -B "%BUILD%" %GENERATOR% -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% %FFMPEG_PATH% %OPENSSL_PATH%
    if errorlevel 1 (
        echo CMake configuration failed.
        exit /b 1
    )
)

echo ==^> Building with %NUMBER_OF_PROCESSORS% jobs...
cmake --build "%BUILD%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo ==^> Done
echo     Server: %BUILD%\server\driscord_server.exe
echo     Client: %BUILD%\client\driscord_client.exe
