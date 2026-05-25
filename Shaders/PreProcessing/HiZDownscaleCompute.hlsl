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
    return SourceHiZ.Load(int3(min(coord, sourceSize - 1u), sourceMip));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;
    uint2 src = tid.xy * 2u;
    float maxDepth = LoadSource(src);
    maxDepth = max(maxDepth, LoadSource(src + uint2(1u, 0u)));
    maxDepth = max(maxDepth, LoadSource(src + uint2(0u, 1u)));
    maxDepth = max(maxDepth, LoadSource(src + uint2(1u, 1u)));
    OutputTexture[tid.xy] = maxDepth;
}
