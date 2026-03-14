@echo off
setlocal

set ROOT=%~dp0..
set BUILD=%ROOT%\build

if not exist "%BUILD%\CMakeCache.txt" (
    echo ==^> Configuring CMake...
    cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo CMake configuration failed.
        exit /b 1
    )
)

echo ==^> Building with %NUMBER_OF_PROCESSORS% jobs...
cmake --build "%BUILD%" --config Release -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo ==^> Done
echo     Server: %BUILD%\server\Release\driscord_server.exe
echo     Client: %BUILD%\client\Release\driscord_client.exe
