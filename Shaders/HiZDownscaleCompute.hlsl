cbuffer HiZDownscaleParams : register(b0, space2)
{
    uint2 sourceSize;
    uint2 outputSize;
    uint sourceMip;
    uint isBaseLevel;
    uint2 padding;
};

Texture2D<float> SourceHiZ : register(t0, space0);
[[vk::image_format("r32f")]] RWTexture2D<float> OutputTexture : register(u0, space1);

float LoadSource(uint2 coord)
{
    uint2 clampedCoord = min(coord, sourceSize - 1u);
    return SourceHiZ.Load(int3(clampedCoord, sourceMip));
}

// hizdownscale.hlsl
[numthreads(8, 8, 1)]
    void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;
    uint2 src = tid.xy * 2u;

    bool hasRight  = (src.x + 1u) < sourceSize.x;
    bool hasBottom = (src.y + 1u) < sourceSize.y;

    float depth = SourceHiZ.Load(int3(src, sourceMip));
    if (hasRight)              depth = max(depth, SourceHiZ.Load(int3(src + uint2(1u, 0u), sourceMip)));
    if (hasBottom)             depth = max(depth, SourceHiZ.Load(int3(src + uint2(0u, 1u), sourceMip)));
    if (hasRight && hasBottom) depth = max(depth, SourceHiZ.Load(int3(src + uint2(1u, 1u), sourceMip)));
    OutputTexture[tid.xy] = depth;
}
