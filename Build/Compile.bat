@echo off
setlocal

rem call Shaders\ShaderCompile.bat || exit /b %ERRORLEVEL%

echo Compiling project...
devenv vsbuild\CPlayground.sln /Build "%1"
start vsbuild\Debug\CPlayground.exe

exit

REM --- Shaders ---
echo Compiling shaders...
call Build\ShaderCompile.bat || exit /b %ERRORLEVEL%

REM --- Paths ---
set OBJ=Build\obj
set DOBJ=Build\dbgobj
set INC=/I.

if not exist %OBJ% mkdir %OBJ%
if not exist %DOBJ% mkdir %DOBJ%

REM --- Sources ---
set SRC=^
 Main.c OS.c Extern/dynarray.c BasisSokol.cpp ^
 TLSF.c Memory.c ECS.c FileSystem.c Platform.c ^
 Graphics.c GLTFParser.c Animation.c Algorithm.c AssetManager.c

set LIBS=advapi32.lib d3d11.lib dxgi.lib dxguid.lib user32.lib shell32.lib gdi32.lib winmm.lib

REM --- MSVC env ---
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 || exit /b

REM --- Extern (release) ---
if not exist %OBJ%\ExternAll.obj (
    cl /std:c++17 /c /O2 /arch:AVX2 /GR- /GS- %INC% Extern\ExternAll.cpp /Fo%OBJ%\ExternAll.obj || exit /b
)

REM --- Extern (debug) ---
if not exist %DOBJ%\ExternAll.obj (
    cl /std:c++17 /c /Od /Zi /GR- /GS- %INC% Extern\ExternAll.cpp /Fo%DOBJ%\ExternAll.obj /Fd%DOBJ%\ExternAll.pdb || exit /b
)

REM --- Build ---
if "%1"=="Debug" (
    cl /std:c++17 /Od /Zi /arch:AVX2 /openmp /GR- /EHsc %INC% ^
       %SRC% %DOBJ%\ExternAll.obj ^
       /Fo%DOBJ%\ /Fe:Build\MainDebug.exe /FdBuild\MainDebug.pdb /link %LIBS%
    start Build\MainDebug.exe
) else (
    cl /std:c++17 /O2 /arch:AVX2 /openmp /GR- /GS- /EHsc %INC% ^
       %SRC% %OBJ%\ExternAll.obj ^
       /Fo%OBJ%\ /Fe:Build\Main.exe /link %LIBS%
    start Build\Main.exe
)

echo Build complete.
