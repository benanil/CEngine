@echo off
echo Compiling shaders with sokol-shdc...
.\Build\sokol-shdc.exe --input Shaders\Cube.glsl --output Shaders\Cube.glsl.h --slang hlsl5
rem sokol-shdc --input Triangle.glsl --output Triangle.h --slang hlsl5
exit /b 0