@echo off
setlocal

set ROOT=%~dp0..
set BUILD=%ROOT%\build

:: --- activate MSVC environment if not already active ---
if not defined VSINSTALLDIR (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            echo ==^> Activating MSVC environment...
            call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        )
    )
)

:: --- auto-detect vcpkg toolchain ---
set "TOOLCHAIN="
if defined VCPKG_ROOT set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

:: --- auto-detect FFmpeg from BtbN builds ---
set "FFMPEG_PATH="
if exist "C:\ffmpeg\lib" set "FFMPEG_PATH=-DCMAKE_PREFIX_PATH=C:\ffmpeg"

:: --- auto-detect OpenSSL ---
set "OPENSSL_PATH="
if exist "C:\Program Files\OpenSSL-Win64" set "OPENSSL_PATH=-DOPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64"

if exist "%BUILD%\CMakeCache.txt" goto build

echo ==^> Configuring CMake...

where ninja >nul 2>&1
if %errorlevel%==0 (
    cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% %FFMPEG_PATH% %OPENSSL_PATH%
) else (
    cmake -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% %FFMPEG_PATH% %OPENSSL_PATH%
)
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)

:build
echo ==^> Building with %NUMBER_OF_PROCESSORS% jobs...
cmake --build "%BUILD%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo ==^> Done
echo     Server: %BUILD%\server\driscord_server.exe
echo     Client: %BUILD%\client\driscord_client.exe
