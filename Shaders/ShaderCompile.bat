@echo off
setlocal enabledelayedexpansion

:: Configuration
set CROSS=Shaders\Build\shadercross.exe
set BIN2C=Shaders\Build\bin2c.exe
set SHADER_DIR=Shaders
set SPV_DIR=Shaders\spv
set MSL_DIR=Shaders\msl
rem set SHADERS=SkinnedFrag:fragment SkinnedVert:vertex
set SHADERS=SkinnedFrag:ps SkinnedVert:vs

if not exist %SPV_DIR% mkdir %SPV_DIR%
if not exist %MSL_DIR% mkdir %MSL_DIR%

echo [1/2] Compiling shaders...
for %%S in (%SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        rem %CROSS% %SHADER_DIR%\%%A.hlsl -s HLSL -d SPIRV -t %%B -o %SHADER_DIR%\%%A.spv
        rem Shaders\dxc.exe -spirv -T %%B_6_6 -E main -enable-16bit-types -D VULKAN=1 %SHADER_DIR%\%%A.hlsl -Fo %SHADER_DIR%\%%A.spv
        Shaders\Build\dxc.exe -spirv -T %%B_6_6 -E main -enable-16bit-types %SHADER_DIR%\%%A.hlsl -Fo %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 (
            echo [ERROR] Failed to compile %%A.hlsl to SPIR-V
            pause
            exit /b !ERRORLEVEL!
        )
        rem %CROSS% %SHADER_DIR%\%%A.hlsl -s HLSL -d MSL -t %%B -o %MSL_DIR%\%%A.msl
        rem if !ERRORLEVEL! neq 0 (
        rem     echo [ERROR] Failed to compile %%A.hlsl to MSL
        rem     pause
        rem     exit /b !ERRORLEVEL!
        rem )
    )
)

echo [2/2] Generating C headers...

for %%S in (%SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        %BIN2C% -o %SHADER_DIR%\%%A.spv.h %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 goto :bin_error
        copy /Y %SHADER_DIR%\%%A.spv %SPV_DIR%\%%A.spv >NUL
        copy /Y %SHADER_DIR%\%%A.spv.h %SPV_DIR%\%%A.spv.h >NUL
        %BIN2C% -o %MSL_DIR%\%%A.msl.h %MSL_DIR%\%%A.msl
        if !ERRORLEVEL! neq 0 goto :bin_error
    )
)

echo.
echo SUCCESS: All shaders compiled and headers generated.
:: Optional: Clean up intermediate SPIR-V files
:: del %SHADER_DIR%\*.spv
exit /b 0

:bin_error
echo [ERROR] bin2c failed to process files.
pause
exit /b 1
