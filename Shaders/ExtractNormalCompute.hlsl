cbuffer ExtractNormalParams : register(b0, space2)
{
    float4x4 invProjection;
};

Texture2D<float> DepthTexture : register(t0, space0);
[[vk::image_format("r11g11b10f")]] RWTexture2D<float3> OutputNormal : register(u0, space1);

float3 ReconstructViewPosition(uint2 p, uint2 fullSize)
{
    p = min(p, fullSize - 1u);
    float depth = DepthTexture.Load(int3(p, 0));
    float2 uv = (float2(p) + 0.5f) / float2(fullSize);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 view = mul(invProjection, clip);
    return view.xyz / max(abs(view.w), 0.00001f);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 fullSize;
    DepthTexture.GetDimensions(fullSize.x, fullSize.y);
    uint2 outputSize;
    OutputNormal.GetDimensions(outputSize.x, outputSize.y);

    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    uint2 center = min(tid.xy * 2u + 1u, fullSize - 1u);
    float depth = DepthTexture.Load(int3(center, 0));
    if (depth >= 0.99999f)
    {
        OutputNormal[tid.xy] = float3(0.5f, 0.5f, 1.0f);
        return;
    }

    uint2 px = uint2(center.x < fullSize.x - 1u ? center.x + 1u : center.x - 1u, center.y);
    uint2 py = uint2(center.x, center.y < fullSize.y - 1u ? center.y + 1u : center.y - 1u);
    float3 p  = ReconstructViewPosition(center, fullSize);
    float3 dx = ReconstructViewPosition(px, fullSize) - p;
    float3 dy = ReconstructViewPosition(py, fullSize) - p;
    if (center.x == fullSize.x - 1u) dx = -dx;
    if (center.y == fullSize.y - 1u) dy = -dy;

    float3 normal = normalize(cross(dx, dy));
    if (normal.z > 0.0f) normal = -normal;
    OutputNormal[tid.xy] = normal * 0.5f + 0.5f;
}
