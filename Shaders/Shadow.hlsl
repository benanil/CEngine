float SampleShadow(Texture2DArray<float> shadowMap, SamplerState sampler, float4 shadowPos, uint cascadeIndex, float3 normal)
{
    float3 proj = shadowPos.xyz / shadowPos.w;
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth <= 0.0f || depth >= 1.0f)
        return 1.0f;

    uint width, height, layers;
    shadowMap.GetDimensions(width, height, layers);

    float2 texel = 1.25f / float2(width, height);
    float3 lightDir = normalize(float3(-0.33f, 0.66f, 0.0f));
    float cascadeBiasScale = cascadeIndex == 0u ? 1.5f : (cascadeIndex == 1u ? 3.0f : 4.0f);
    float bias = max(0.0012f * (1.0f - dot(normalize(normal), lightDir)), 0.00035f) * cascadeBiasScale;

    const float minShadow = 0.2f;
    float shadow = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = uv + float2(x, y) * texel;
            float tapShadow = 1.0f;
            if (all(sampleUV >= 0.0f) && all(sampleUV <= 1.0f))
            {
                float mapDepth = shadowMap.Sample(sampler, float3(sampleUV, float(cascadeIndex))).r;
                if (mapDepth < 0.999f)
                {
                    tapShadow = (depth - bias <= mapDepth) ? 1.0f : 0.0f;
                }
            }
            shadow += tapShadow;
        }
    }
    return max(shadow * (1.0f / 9.0f), minShadow);
}
