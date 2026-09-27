@echo off
REM Сборка проекта MinGW + CMake (Windows)
setlocal
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Built: build\udp_server.exe  build\udp_client.exe
echo Run:   build\udp_server.exe 54000
echo        build\udp_client.exe 127.0.0.1 54000 player1 qwerty 10 1000
