
@echo off
setlocal

call Shaders\ShaderCompile.bat || exit /b %ERRORLEVEL%

REM --- MSVC env ---
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 || exit /b

echo Compiling project...

if "%1"=="Debug" (
    rem cmake -S . -B Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build Debug
    start Debug/Debug/CPlayground.exe
) else (
    rem cmake -S . -B Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build Release
    start Release/Release/CPlayground.exe
)   

exit

echo Build complete.
