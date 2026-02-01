@echo off
setlocal enabledelayedexpansion

:: Configuration
set CROSS=Shaders\shadercross.exe
set BIN2C=Shaders\bin2c.exe
set SHADER_DIR=Shaders

echo [1/2] Compiling HLSL to SPIR-V...

:: Compile Fragment Shader
%CROSS% %SHADER_DIR%\SkinnedFrag.hlsl -s HLSL -d SPIRV -t fragment -o %SHADER_DIR%\SkinnedFrag.spv
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile SkinnedFrag.hlsl
    pause
    exit /b %ERRORLEVEL%
)

:: Compile Vertex Shader
%CROSS% %SHADER_DIR%\SkinnedVert.hlsl -s HLSL -d SPIRV -t vertex -o %SHADER_DIR%\SkinnedVert.spv
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile SkinnedVert.hlsl
    pause
    exit /b %ERRORLEVEL%
)

echo [2/2] Generating C headers...

:: Convert to headers
%BIN2C% -o %SHADER_DIR%\SkinnedFrag.spv.h %SHADER_DIR%\SkinnedFrag.spv
if %ERRORLEVEL% neq 0 goto :bin_error

%BIN2C% -o %SHADER_DIR%\SkinnedVert.spv.h %SHADER_DIR%\SkinnedVert.spv
if %ERRORLEVEL% neq 0 goto :bin_error

echo.
echo SUCCESS: All shaders compiled and headers generated.
:: Optional: Clean up intermediate SPIR-V files
:: del %SHADER_DIR%\*.spv
exit /b 0

:bin_error
echo [ERROR] bin2c failed to process files.
pause
exit /b 1