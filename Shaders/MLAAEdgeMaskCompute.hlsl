#include "MLAACommon.hlsl"

Texture2D<float4> SourceTexture : register(t0, space0);
[[vk::image_format("r32ui")]] RWTexture2D<uint> EdgeMaskTexture : register(u0, space1);

#if 0
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
    bool isValid = (tid.x < outputSize.x && tid.y < outputSize.y);
    
    int2 p = int2(tid.xy);
    float center = 0.0f;
    float up = 0.0f;

    if (isValid)
    {
        center = SceneLuma(SourceTexture.Load(int3(p, 0)).rgb);
        up     = SceneLuma(SourceTexture.Load(int3(ClampPixel(p + int2(0, -1)), 0)).rgb);
    }

    uint laneIndex = WaveGetLaneIndex();
    float right = WaveReadLaneAt(center, laneIndex + 1);
    if (gtid.x == 7 || (tid.x + 1 >= outputSize.x))
    {
        right = SceneLuma(SourceTexture.Load(int3(ClampPixel(p + int2(1, 0)), 0)).rgb);
    }

    if (!isValid) return;
    uint mask = 0u;
    if (CompareLuma(center, up))    mask |= MLAA_UPPER_MASK;
    if (CompareLuma(center, right)) mask |= MLAA_RIGHT_MASK;
    EdgeMaskTexture[tid.xy] = mask;
}
#else
// An 8x8 thread block needs to read 1 pixel up and 1 pixel right.
// Total bounds needed: X standard (0 to 8), Y standard (-1 to 7) -> 9x9 size requirement.
groupshared float s_Luma[9][9];

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    int2 groupStartPos = int2(tid.xy) - int2(gtid.xy);

    // COLLABORATIVE LOAD: 81 elements needed, handled cleanly by 64 threads
    [unroll]
    for (uint i = groupIndex; i < 81; i += 64)
    {
        uint ldsY = i / 9;
        uint ldsX = i % 9;
        // Offset Y by -1 to capture the 'up' apron rows
        int2 loadPos = groupStartPos + int2(ldsX, int(ldsY) - 1);
        loadPos = ClampPixel(loadPos);
        s_Luma[ldsY][ldsX] = SceneLuma(SourceTexture.Load(int3(loadPos, 0)).rgb);
    }

    // Wait for all threads to write to shared memory
    GroupMemoryBarrierWithGroupSync();

    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    // Map local LDS positions (shifting Y because index 0 in LDS is row -1)
    uint localX = gtid.x;
    uint localY = gtid.y + 1; 
    float center = s_Luma[localY][localX];
    float up     = s_Luma[localY - 1][localX];
    float right  = s_Luma[localY][localX + 1];

    uint mask = 0u;
    if (CompareLuma(center, up))    mask |= MLAA_UPPER_MASK;
    if (CompareLuma(center, right)) mask |= MLAA_RIGHT_MASK;
    EdgeMaskTexture[tid.xy] = mask;
}
#endif