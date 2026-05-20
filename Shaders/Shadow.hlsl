float SampleShadow(Texture2DArray<float> shadowMap, SamplerState sampler, float4 shadowPos, uint cascadeIndex, float3 normal, float3 lightDir)
{
    float3 proj = shadowPos.xyz / shadowPos.w;
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth <= 0.0f || depth >= 1.0f)
        return 1.0f;

    uint width, height, layers;
    shadowMap.GetDimensions(width, height, layers);

    float2 texel = 1.0f / float2(width, height);
    lightDir = normalize(lightDir);
    float cascadeBiasScale = cascadeIndex == 0u ? 1.2f : (cascadeIndex == 1u ? 2.8f : 2.6f);
    float ndotl = saturate(dot(normalize(normal), lightDir));
    float bias = max(0.0009f * (1.0f - ndotl), 0.00025f) * cascadeBiasScale;

    const float minShadow = 0.2f;
    float shadow = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            float weight = (3.0f - abs(float(x))) * (3.0f - abs(float(y)));
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
            shadow += tapShadow * weight;
            weightSum += weight;
        }
    }
    return max(shadow / weightSum, minShadow);
}
