@echo off

set ROOT=%~dp0..
set BUILD=%ROOT%\build

:: --- detect vcpkg BEFORE vcvars (vcvars sets its own VCPKG_ROOT with spaces) ---
set "VCPKG_EXE="
set "TOOLCHAIN="
set "VCPKG_TRIPLET=x64-windows-static"
if exist "C:\vcpkg\vcpkg.exe" (
    set "VCPKG_EXE=C:\vcpkg\vcpkg.exe"
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

:: --- install vcpkg dependencies if available ---
if defined VCPKG_EXE (
    echo ==^> Checking vcpkg dependencies...
    "%VCPKG_EXE%" install openssl:%VCPKG_TRIPLET% ffmpeg[avcodec,avformat,avdevice,swscale]:%VCPKG_TRIPLET% boost-beast:%VCPKG_TRIPLET% boost-asio:%VCPKG_TRIPLET% --no-print-usage 2>nul
    if errorlevel 1 (
        echo WARNING: Some vcpkg packages may have failed to install.
        echo Trying with x64-windows triplet as fallback...
        set "VCPKG_TRIPLET=x64-windows"
        "%VCPKG_EXE%" install openssl:%VCPKG_TRIPLET% ffmpeg[avcodec,avformat,avdevice,swscale]:%VCPKG_TRIPLET% boost-beast:%VCPKG_TRIPLET% boost-asio:%VCPKG_TRIPLET% --no-print-usage 2>nul
    )
)

:: --- auto-detect FFmpeg (fallback for manual installs) ---
set "FFMPEG_PATH="
if not defined VCPKG_EXE (
    if exist "C:\ffmpeg\lib" set "FFMPEG_PATH=-DCMAKE_PREFIX_PATH=C:\ffmpeg"
)

:: --- auto-detect OpenSSL (fallback for manual installs) ---
set "OPENSSL_PATH="
if not defined VCPKG_EXE (
    if exist "C:\Program Files\OpenSSL-Win64\include\openssl" set "OPENSSL_PATH=-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
)

if exist "%BUILD%\CMakeCache.txt" goto build

echo ==^> Configuring CMake...
cmake -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% %FFMPEG_PATH% %OPENSSL_PATH%
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

:: --- collect DLLs next to client exe ---
set "CLIENT_DIR=%BUILD%\client"
if exist "C:\vcpkg\installed\x64-windows\bin\avutil*.dll" (
    copy /Y "C:\vcpkg\installed\x64-windows\bin\av*.dll" "%CLIENT_DIR%\" >nul 2>&1
    copy /Y "C:\vcpkg\installed\x64-windows\bin\sw*.dll" "%CLIENT_DIR%\" >nul 2>&1
)
if exist "C:\ffmpeg\bin\avutil*.dll" (
    copy /Y "C:\ffmpeg\bin\av*.dll" "%CLIENT_DIR%\" >nul 2>&1
    copy /Y "C:\ffmpeg\bin\sw*.dll" "%CLIENT_DIR%\" >nul 2>&1
)

:: --- package into zip archives on Z:\ ---
echo ==^> Packaging...

set "STAGING=%BUILD%\staging"

:: Client archive
if exist "%STAGING%\client" rd /s /q "%STAGING%\client"
mkdir "%STAGING%\client"
copy /Y "%BUILD%\client\driscord_client.exe" "%STAGING%\client\" >nul
copy /Y "%ROOT%\driscord.json" "%STAGING%\client\" >nul
if exist "%CLIENT_DIR%\*.dll" (
    copy /Y "%CLIENT_DIR%\*.dll" "%STAGING%\client\" >nul
)
echo ==^> Creating Z:\driscord_client.zip ...
powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\client\*' -DestinationPath 'Z:\driscord_client.zip' -Force"
if errorlevel 1 (
    echo WARNING: Failed to create Z:\driscord_client.zip
) else (
    echo     Z:\driscord_client.zip created
)

:: Server archive
if exist "%STAGING%\server" rd /s /q "%STAGING%\server"
mkdir "%STAGING%\server"
copy /Y "%BUILD%\server\driscord_server.exe" "%STAGING%\server\" >nul
copy /Y "%ROOT%\driscord.json" "%STAGING%\server\" >nul
echo ==^> Creating Z:\driscord_server.zip ...
powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\server\*' -DestinationPath 'Z:\driscord_server.zip' -Force"
if errorlevel 1 (
    echo WARNING: Failed to create Z:\driscord_server.zip
) else (
    echo     Z:\driscord_server.zip created
)

:: Cleanup staging
rd /s /q "%STAGING%" >nul 2>&1

echo ==^> Done
echo     Server: %BUILD%\server\driscord_server.exe
echo     Client: %BUILD%\client\driscord_client.exe
