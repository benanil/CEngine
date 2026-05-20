float SampleShadow(Texture2D<float> shadowMap, SamplerState sampler, float4 shadowPos, float3 normal)
{
    float3 proj = shadowPos.xyz / shadowPos.w;
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth >= 1.0f)
        return 1.0f;

    uint width, height;
    shadowMap.GetDimensions(width, height);

    float2 texel = 1.35f / float2(width, height);
    float3 lightDir = normalize(float3(-0.33f, 0.66f, 0.0f));
    float bias = max(0.0007f * (1.0f - dot(normalize(normal), lightDir)), 0.0002f);

    float shadow = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float mapDepth = shadowMap.Sample(sampler, uv + float2(x, y) * texel).r;
            shadow += (depth - bias <= mapDepth) ? 1.0f : 0.0f;
        }
    }
    return max(shadow * (1.0f / 9.0f), 0.2f);
}
