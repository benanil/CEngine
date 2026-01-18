@echo off
echo Compiling shaders with shadercross

start Shaders/shadercross.exe Shaders/SkinnedFrag.hlsl -s HLSL -d SPIRV -t fragment -o Shaders/SkinnedFrag.spv
start Shaders/shadercross.exe Shaders/SkinnedVert.hlsl -s HLSL -d SPIRV -t vertex -o Shaders/SkinnedVert.spv

echo saving in header files

start Shaders/bin2c.exe -o Shaders/SkinnedFrag.spv.h Shaders/SkinnedFrag.spv
echo SkinnedFrag.spv

start Shaders/bin2c.exe -o Shaders/SkinnedVert.spv.h Shaders/SkinnedVert.spv
echo SkinnedVert.spv
