@echo off

echo Shaders compiling...
call "Build/ShaderCompile.bat"
if %ERRORLEVEL% neq 0 (
    echo Failed to compile shaders!
    exit /b %ERRORLEVEL%
)

set SOURCE_FILES=^
	Main.c ^
	OS.c ^
	Extern/dynarray.c ^
	TLSF.c ^
	Memory.c ^
	ECS.c ^
	FileSystem.c ^
    Platform.c ^
	Graphics.c ^
	GLTFParser.c ^
	Animation.c ^
	Algorithm.c ^
	AssetManager.c 

if not exist "Build\obj" mkdir "Build\obj"

REM <<<  CLANG COMPILER  >>>
REM clang -O3 -mavx2 -mfma -msse4.2 -mf16c -mrdseed -s -fno-rtti -fno-stack-protector -Wimplicit-function-declaration -fno-exceptions -fno-unwind-tables -static-libgcc ^
REM %SOURCE_FILES% Extern/ufbx.c BasisSokol.cpp -o Build/MainCLANG.exe -I. -ladvapi32 -ld3d11 -ldxgi -ldxguid -luser32 -lshell32 -lgdi32 -lwinmm
REM start "" "Build/MainCLANG.exe"
REM exit /b %ERRORLEVEL%

REM <<<  MSVC COMPILER  >>>
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo Failed to set up MSVC environment!
    exit /b %ERRORLEVEL%
)

echo Checking extern libs...
REM Compile C files
if not exist "Build\obj\ufbx.obj" (
    echo Compiling ufbx.c...
    cl.exe /c /O2 /arch:AVX2 /GR- /GS- /I. /FoBuild\obj\ufbx.obj Extern\ufbx.c >nul
)

REM Compile C++ files  
if not exist "Build\obj\BasisSokol.obj" (
    echo Compiling BasisSokol.cpp...
    cl.exe /std:c++17 /c /O2 /arch:AVX2 /GR- /GS- /EHsc /I. /FoBuild\obj\BasisSokol.obj BasisSokol.cpp >nul
)

if "%1"=="Debug" (
	cl.exe /std:c++17 /Od /Zi /arch:AVX2 /GR- /EHsc /I. /FoBuild\obj\ /FdBuild\MainDebug.pdb %SOURCE_FILES% Build\obj\ufbx.obj Build\obj\BasisSokol.obj  /Fe:Build\MainDebug.exe /link advapi32.lib d3d11.lib dxgi.lib dxguid.lib user32.lib shell32.lib gdi32.lib winmm.lib
	if %ERRORLEVEL% neq 0 (
		echo Compilation failed!
		exit /b %ERRORLEVEL%
	)
    start "" "Build/MainDebug.exe"
) else (
	cl.exe /std:c++17 /O2 /arch:AVX2 /GR- /GS- /EHsc /I. /FoBuild\obj\ %SOURCE_FILES%  Build\obj\ufbx.obj Build\obj\BasisSokol.obj /Fe:Build\MainMSVC.exe /link advapi32.lib d3d11.lib dxgi.lib dxguid.lib user32.lib shell32.lib gdi32.lib winmm.lib
	if %ERRORLEVEL% neq 0 (
		echo Compilation failed!
		exit /b %ERRORLEVEL%
	)
    start "" "Build/MainMSVC.exe"
)

echo Build complete.
