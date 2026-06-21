@echo off
setlocal
set ROOT=%~dp0..
set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" (
    echo vcvarsall.bat not found: %VCVARS%
    exit /b 1
)
call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b 1
cl /nologo /Zi /Od /I "%ROOT%" /I "%ROOT%\Extern\SDL3\include" "%ROOT%\Tests\SceneSerializerLoadLoopTest.c" /Fe:"%ROOT%\Tests\SceneSerializerLoadLoopTest.exe"
if errorlevel 1 exit /b 1
"%ROOT%\Tests\SceneSerializerLoadLoopTest.exe"
cmd /k