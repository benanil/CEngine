cbuffer HBAOParams : register(b0, space2)
{
    float4x4 invProjection;
    uint2 fullSize;
    uint2 aoSize;
    float radius;
    float projectionScale;
    float bias;
    float intensity;
    float power;
    uint enabled;
    uint frameIndex;
    uint padding;
};

Texture2D<float> DepthTexture : register(t0, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutputNormal : register(u0, space1);

float3 ReconstructView(uint2 p)
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
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;

    uint2 center = min(tid.xy * 2u + 1u, fullSize - 1u);
    uint2 px = uint2(min(center.x + 1u, fullSize.x - 1u), center.y);
    uint2 nx = uint2(center.x > 0u ? center.x - 1u : 0u, center.y);
    uint2 py = uint2(center.x, min(center.y + 1u, fullSize.y - 1u));
    uint2 ny = uint2(center.x, center.y > 0u ? center.y - 1u : 0u);

    float3 dx = ReconstructView(px) - ReconstructView(nx);
    float3 dy = ReconstructView(py) - ReconstructView(ny);
    float3 n = normalize(cross(dy, dx));
    n = n.z < 0.0f ? -n : n;

    OutputNormal[tid.xy] = float4(n, 1.0f);
}
