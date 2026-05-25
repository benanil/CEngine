cbuffer HiZBuildParams : register(b0, space2)
{
    uint2 sourceSize;
    uint2 outputSize;
    uint sourceMip;
    uint isBaseLevel; // Unused here, can be left in the cbuffer structure
    uint2 padding;
};

Texture2D<float> SourceDepth : register(t0, space0);
[[vk::image_format("r32f")]] RWTexture2D<float> OutputTexture : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;
    uint2 clampedCoord = min(tid.xy, sourceSize - 1u);
    OutputTexture[tid.xy] = SourceDepth.Load(int3(clampedCoord, 0));
}