#include "MLAACommon.hlsl"

Texture2D<uint> EdgeMaskTexture : register(t0, space0);
[[vk::image_format("rg32ui")]] RWTexture2D<uint2> EdgeCountTexture : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    uint pixel = EdgeMaskTexture.Load(int3(p, 0));
    uint4 edgeCount = uint4(0u, 0u, 0u, 0u);

    if ((pixel & (MLAA_UPPER_MASK | MLAA_RIGHT_MASK)) != 0u)
    {
        uint4 edgeDirMask = uint4(MLAA_UPPER_MASK, MLAA_UPPER_MASK, MLAA_RIGHT_MASK, MLAA_RIGHT_MASK);
        uint4 edgeFound = uint4(
            (pixel & edgeDirMask.x) != 0u ? 0xffffffffu : 0u,
            (pixel & edgeDirMask.y) != 0u ? 0xffffffffu : 0u,
            (pixel & edgeDirMask.z) != 0u ? 0xffffffffu : 0u,
            (pixel & edgeDirMask.w) != 0u ? 0xffffffffu : 0u);
        uint4 stopBit = uint4(
            edgeFound.x != 0u ? MLAA_STOP_BIT : 0u,
            edgeFound.y != 0u ? MLAA_STOP_BIT : 0u,
            edgeFound.z != 0u ? MLAA_STOP_BIT : 0u,
            edgeFound.w != 0u ? MLAA_STOP_BIT : 0u);

        [unroll]
        for (int i = 1; i <= int(MLAA_MAX_EDGE_LENGTH); i++)
        {
            uint4 mask;
            mask.x = EdgeMaskTexture.Load(int3(ClampPixel(p + int2(-i,  0)), 0));
            mask.y = EdgeMaskTexture.Load(int3(ClampPixel(p + int2( i,  0)), 0));
            mask.z = EdgeMaskTexture.Load(int3(ClampPixel(p + int2( 0,  i)), 0));
            mask.w = EdgeMaskTexture.Load(int3(ClampPixel(p + int2( 0, -i)), 0));

            edgeFound &= (mask & edgeDirMask);
            edgeCount.x = edgeFound.x != 0u ? edgeCount.x + 1u : (edgeCount.x | stopBit.x);
            edgeCount.y = edgeFound.y != 0u ? edgeCount.y + 1u : (edgeCount.y | stopBit.y);
            edgeCount.z = edgeFound.z != 0u ? edgeCount.z + 1u : (edgeCount.z | stopBit.z);
            edgeCount.w = edgeFound.w != 0u ? edgeCount.w + 1u : (edgeCount.w | stopBit.w);
        }
    }

    EdgeCountTexture[tid.xy] = uint2(EncodeCount(edgeCount.x, edgeCount.y), EncodeCount(edgeCount.z, edgeCount.w));
}
