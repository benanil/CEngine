#include "Bitpack.hlsl"

Texture2D<uint> TangentFrameTexture : register(t0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputNormal : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 fullSize;
    TangentFrameTexture.GetDimensions(fullSize.x, fullSize.y);
    uint2 outputSize;
    OutputNormal.GetDimensions(outputSize.x, outputSize.y);

    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    uint2 center = min(tid.xy * 2u + 1u, fullSize - 1u);
    uint tangentFrame = TangentFrameTexture.Load(int3(center, 0));
    f16_3 worldNormal;
    f16_3 worldTangent;
    UnpackNormalTangent(tangentFrame, worldNormal, worldTangent);

    float3 normal = normalize(float3(worldNormal));
    OutputNormal[tid.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
}
