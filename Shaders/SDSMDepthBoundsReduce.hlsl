cbuffer SDSMDepthBoundsParams : register(b0, space2)
{
    uint2 outputSize;
    uint2 sourceSize;
    uint sourceMip;
    uint padding;
};

Texture2D<float2> SourceBounds : register(t0, space0);
[[vk::image_format("rg32f")]] RWTexture2D<float2> OutputBounds : register(u0, space1);

groupshared float2 depthSamples[64];

[numthreads(8, 8, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint threadIndex : SV_GroupIndex)
{
    uint2 p = min(gid.xy * 8u + gtid.xy, sourceSize - 1u);
    depthSamples[threadIndex] = SourceBounds.Load(int3(p, sourceMip));

    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint s = 32u; s > 0u; s >>= 1u)
    {
        if (threadIndex < s)
        {
            depthSamples[threadIndex].x = min(depthSamples[threadIndex].x, depthSamples[threadIndex + s].x);
            depthSamples[threadIndex].y = max(depthSamples[threadIndex].y, depthSamples[threadIndex + s].y);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex == 0u && gid.x < outputSize.x && gid.y < outputSize.y)
        OutputBounds[gid.xy] = depthSamples[0];
}
