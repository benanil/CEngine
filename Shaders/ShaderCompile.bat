@echo off
setlocal enabledelayedexpansion
set CROSS=Shaders\Build\shadercross.exe
set BIN2C=Shaders\Build\bin2c.exe
set SHADER_DIR=Shaders
set SPV_DIR=Shaders\spv
set MSL_DIR=Shaders\msl
set SHADERS=SkinnedFrag:ps SkinnedVert:vs
set COMPUTE_SHADERS=AnimationCompute:cs

if not exist %SPV_DIR% mkdir %SPV_DIR%
if not exist %MSL_DIR% mkdir %MSL_DIR%

echo [1/2] Compiling shaders...
for %%S in (%SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        Shaders\Build\dxc.exe -spirv -T %%B_6_6 -E main -enable-16bit-types %SHADER_DIR%\%%A.hlsl -Fo %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 (
            echo [ERROR] Failed to compile %%A.hlsl to SPIR-V
            pause
            exit /b !ERRORLEVEL!
        )
    )
)

for %%S in (%COMPUTE_SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        Shaders\Build\dxc.exe -spirv -T %%B_6_6 -E main -enable-16bit-types %SHADER_DIR%\%%A.hlsl -Fo %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 (
            echo [ERROR] Failed to compile %%A.hlsl to SPIR-V
            pause
            exit /b !ERRORLEVEL!
        )
    )
)

echo [2/2] Generating C headers...
for %%S in (%SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        %BIN2C% -o %SHADER_DIR%\%%A.spv.h %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 goto :bin_error
        move /Y %SHADER_DIR%\%%A.spv.h %SPV_DIR%\%%A.spv.h >NUL
    )
)

for %%S in (%COMPUTE_SHADERS%) do (
    for /f "tokens=1,2 delims=:" %%A in ("%%S") do (
        %BIN2C% -o %SHADER_DIR%\%%A.spv.h %SHADER_DIR%\%%A.spv
        if !ERRORLEVEL! neq 0 goto :bin_error
        move /Y %SHADER_DIR%\%%A.spv.h %SPV_DIR%\%%A.spv.h >NUL
    )
)

echo.
echo SUCCESS: All shaders compiled and headers generated.
del %SHADER_DIR%\*.spv
exit /b 0

:bin_error
echo [ERROR] bin2c failed to process files.
pause
exit /b 1