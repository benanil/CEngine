// Forward+ AO helper: the deferred path feeds HBAO with normals unpacked from the
// G-buffer tangent target, which the forward path never writes. This reconstructs a
// world-space normal from the prepass depth (cross product of neighbouring reconstructed
// positions) into the same rgba8 target HBAO already samples.
Texture2D<float> DepthTexture : register(t0, space0);
SamplerState     DepthSampler : register(s0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputNormal : register(u0, space1);

cbuffer Params : register(b0, space2)
{
    float4x4 invViewProj;
    float4   cameraPosition;
    uint2    fullSize;
    uint2    aoSize;
};

float3 ReconstructWorld(uint2 fp)
{
    fp = min(fp, fullSize - 1u);
    float depth = DepthTexture.Load(int3(fp, 0));
    float2 uv = (float2(fp) + 0.5f) / float2(fullSize);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 w = mul(invViewProj, clip);
    return w.xyz / max(abs(w.w), 0.00001f);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;

    uint2 center = tid.xy * 2u + 1u;
    float3 p  = ReconstructWorld(center);
    float3 px = ReconstructWorld(center + uint2(2u, 0u));
    float3 py = ReconstructWorld(center + uint2(0u, 2u));

    float3 normal = normalize(cross(px - p, py - p));
    if (dot(normal, cameraPosition.xyz - p) < 0.0f) normal = -normal;

    OutputNormal[tid.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
}
