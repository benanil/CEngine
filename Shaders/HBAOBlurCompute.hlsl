cbuffer HBAOParams : register(b0, space2)
{
    float4x4 invProjection;
    float4x4 view;
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

Texture2D<float> AOSource : register(t0, space0);
Texture2D<float> DepthTexture : register(t1, space0);
[[vk::image_format("r8")]] RWTexture2D<float> OutputAO : register(u0, space1);

float SampleAO(int2 p)
{
    p = clamp(p, int2(0, 0), int2(aoSize) - 1);
    return AOSource.Load(int3(p, 0));
}

float SampleDepth(int2 p)
{
    p = clamp(p, int2(0, 0), int2(aoSize) - 1);
    uint2 fullP = min(uint2(p) * 2u + 1u, fullSize - 1u);
    return DepthTexture.Load(int3(fullP, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;

    int2 p = int2(tid.xy);
    float centerDepth = SampleDepth(p);
    float total = SampleAO(p);
    float weightTotal = 1.0f;

    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            if (x == 0 && y == 0) continue;
            float dist2 = float(x * x + y * y);
            float dd = (SampleDepth(p + int2(x, y)) - centerDepth) * 80.0f;
            float w = exp2(-dist2 * 0.35f - dd * dd);
            total += SampleAO(p + int2(x, y)) * w;
            weightTotal += w;
        }
    }

    OutputAO[tid.xy] = total / weightTotal;
}
