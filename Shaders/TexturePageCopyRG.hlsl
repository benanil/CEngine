cbuffer CopyParams : register(b0, space2)
{
    uint2 dstOffset;
    uint2 copySize;
    uint2 sourceSize;
    uint sourceMip;
    uint gutter;
    uint channelMode;
};

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler      : register(s0, space0);
[[vk::image_format("rg8")]] RWTexture2D<float2> DestTexture : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= copySize.x || tid.y >= copySize.y) return;
    uint2 innerSize = max(copySize - gutter * 2u, uint2(1, 1));
    uint2 innerPixel = min(uint2(max(int2(tid.xy) - int(gutter), int2(0, 0))), innerSize - uint2(1, 1));
    int2 srcPixel = int2(min(((innerPixel * sourceSize) + innerSize / 2u) / innerSize, sourceSize - uint2(1, 1)));
    float2 uv = (float2(srcPixel) + 0.5f) / float2(sourceSize);
    float4 source = SourceTexture.SampleLevel(SourceSampler, uv, sourceMip);
    DestTexture[dstOffset + tid.xy] = channelMode == 1u ? source.bg : source.xy;
}
