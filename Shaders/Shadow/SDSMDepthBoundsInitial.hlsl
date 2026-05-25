cbuffer SDSMDepthBoundsParams : register(b0, space2)
{
    uint2 outputSize;
    uint2 sourceSize;
};

Texture2D<float> SourceDepth : register(t0, space0);
[[vk::image_format("rg32f")]] RWTexture2D<float2> OutputBounds : register(u0, space1);

groupshared float2 depthSamples[16][16];

[numthreads(8, 8, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint threadIndex : SV_GroupIndex)
{
    uint2 tileBase = gid.xy * 16u;

    [unroll]
    for (uint i = threadIndex; i < 256u; i += 64u)
    {
        uint2 ldsPos = uint2(i & 15u, i >> 4u);
        uint2 p = tileBase + ldsPos;
        float2 bounds = float2(1.0f, 0.0f);
        if (p.x < sourceSize.x && p.y < sourceSize.y)
        {
            float depth = SourceDepth.Load(int3(p, 0));
            if (depth < 0.999999f) bounds = float2(depth, depth);
        }
        depthSamples[ldsPos.y][ldsPos.x] = bounds;
    }

    GroupMemoryBarrierWithGroupSync();

    uint2 outPos = gid.xy * 8u + gtid.xy;
    if (outPos.x >= outputSize.x || outPos.y >= outputSize.y) return;

    uint2 ldsBase = gtid.xy * 2u;
    float2 a = depthSamples[ldsBase.y + 0u][ldsBase.x + 0u];
    float2 b = depthSamples[ldsBase.y + 0u][ldsBase.x + 1u];
    float2 c = depthSamples[ldsBase.y + 1u][ldsBase.x + 0u];
    float2 d = depthSamples[ldsBase.y + 1u][ldsBase.x + 1u];

    OutputBounds[outPos] = float2(min(min(a.x, b.x), min(c.x, d.x)),
                                  max(max(a.y, b.y), max(c.y, d.y)));
}
