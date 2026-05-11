cbuffer CopyParams : register(b0, space2)
{
    uint2 dstOffset;
    uint2 copySize;
    uint sourceMip;
};

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler      : register(s0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> DestTexture : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= copySize.x || tid.y >= copySize.y) return;
    float2 uv = (float2(tid.xy) + 0.5f) / float2(copySize);
    DestTexture[dstOffset + tid.xy] = SourceTexture.SampleLevel(SourceSampler, uv, sourceMip);
}
