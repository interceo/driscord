@echo off

set ROOT=%~dp0..
set BUILD=%ROOT%\build

:: --- detect vcpkg BEFORE vcvars (vcvars sets its own VCPKG_ROOT with spaces) ---
set "TOOLCHAIN="
if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
)

:: --- activate MSVC environment if cl.exe not found ---
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ==^> Activating MSVC environment...
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

:: --- auto-detect FFmpeg ---
set "FFMPEG_PATH="
if exist "C:\ffmpeg\lib" set "FFMPEG_PATH=-DCMAKE_PREFIX_PATH=C:\ffmpeg"

:: --- auto-detect OpenSSL ---
set "OPENSSL_PATH="
if exist "C:\Program Files\OpenSSL-Win64" set "OPENSSL_PATH=-DOPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64"

if exist "%BUILD%\CMakeCache.txt" goto build

echo ==^> Configuring CMake...
cmake -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% %FFMPEG_PATH% %OPENSSL_PATH%
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)

:build
echo ==^> Building...
cmake --build "%BUILD%"
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo ==^> Done
echo     Server: %BUILD%\server\driscord_server.exe
echo     Client: %BUILD%\client\driscord_client.exe
