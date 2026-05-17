Texture2DMS<float> SourceDepth : register(t0, space0);
[[vk::image_format("r32f")]] RWTexture2D<float> OutputTexture : register(u0, space1);

cbuffer DepthResolveParams : register(b0, space2)
{
    uint2 outputSize;
    uint sampleCount;
    uint padding;
};

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    float depth = 1.0f;
    for (uint i = 0; i < sampleCount; i++)
        depth = min(depth, SourceDepth.Load(p, int(i)));

    OutputTexture[tid.xy] = depth;
}
