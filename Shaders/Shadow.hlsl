
static const float2 ShadowKernel[32] =
{
    float2(0.382143, -0.750672),
    float2(-0.699118, 0.417980),
    float2(0.017911, 0.530034),
    float2(0.461916, -0.497940),
    float2(0.138959, -0.753612),
    float2(-0.619547, 0.523433),
    float2(-0.403566, 0.251649),
    float2(0.501719, -0.631005),
    float2(0.409324, 0.174980),
    float2(-0.649627, -0.377275),
    float2(0.134689, -0.450591),
    float2(-0.686986, 0.133178),
    float2(-0.089575, -0.083734),
    float2(0.007943, 0.826362),
    float2(-0.057125, -0.329970),
    float2(0.078787, -0.175278),
    float2(-0.772931, 0.384923),
    float2(0.551949, -0.669406),
    float2(-0.087713, 0.962403),
    float2(0.094594, -0.576342),
    float2(-0.362402, -0.462390),
    float2(0.714690, -0.311474),
    float2(0.801766, -0.471645),
    float2(0.069921, 0.802410),
    float2(0.422638, -0.300293),
    float2(-0.774194, -0.435089),
    float2(0.359599, -0.633199),
    float2(-0.721211, -0.323427),
    float2(0.242559, 0.850162),
    float2(-0.737375, -0.288707),
    float2(-0.865741, 0.433705),
    float2(-0.191099, -0.707353)
};

static const float2 ShadowRotations[32] =
{
    float2(-0.995507, 0.094687),
    float2(0.735224, -0.677824),
    float2(-0.950197, 0.311651),
    float2(0.080015, 0.996794),
    float2(0.491843, 0.870684),
    float2(0.969434, 0.245352),
    float2(0.501749, -0.865013),
    float2(0.992536, -0.121956),
    float2(0.683535, -0.729918),
    float2(0.948335, 0.317270),
    float2(-0.238814, -0.971065),
    float2(-0.975825, -0.218552),
    float2(-0.248723, 0.968575),
    float2(-0.743790, -0.668413),
    float2(0.992742, -0.120261),
    float2(0.995206, 0.097796),
    float2(-0.907968, -0.419039),
    float2(0.961052, 0.276368),
    float2(-0.797258, 0.603638),
    float2(-0.643269, 0.765641),
    float2(0.915711, -0.401837),
    float2(-0.988326, -0.152355),
    float2(0.763451, 0.645866),
    float2(-0.998725, 0.050491),
    float2(0.829770, -0.558106),
    float2(0.991375, -0.131057),
    float2(-0.996397, -0.084806),
    float2(0.000972, 1.000000),
    float2(0.916640, 0.399713),
    float2(0.785641, -0.618683),
    float2(0.894287, 0.447494),
    float2(0.900530, -0.434794)
};

float SampleShadow(Texture2DArray<float> shadowMap, SamplerState samp,
                   float4 shadowPos, uint cascadeIndex,
                   float3 normal, float3 lightDir, uint2 screenPos)
{
    float3 proj = shadowPos.xyz / shadowPos.w;
    float2 uv   = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float  depth = proj.z;

    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth <= 0.0f || depth >= 1.0f)
        return 1.0f;

    uint width, height, layers;
    shadowMap.GetDimensions(width, height, layers);
    float2 texel = 1.0f / float2(width, height);

    lightDir = normalize(lightDir);
    float cascadeBiasScale = cascadeIndex == 0u ? 1.2f : (cascadeIndex == 1u ? 2.8f : 2.6f);
    float ndotl = saturate(dot(normalize(normal), lightDir));
    float bias  = max(0.0009f * (1.0f - ndotl), 0.00025f) * cascadeBiasScale;

    float2 rot          = ShadowRotations[(screenPos.x ^ screenPos.y * 7u) & 31u];
    float2 searchRadius = texel * 2.0f;

    float shadow = 0.0f;
    static const int KernelSize = 24;
    [unroll]
    for (int i = 0; i < KernelSize; i++)
    {
        float2 off = ShadowKernel[i];
        float2 sampleUV = uv + float2(off.x * rot.x - off.y * rot.y,
                                      off.x * rot.y + off.y * rot.x) * searchRadius;

        [branch]
        if (all(sampleUV >= 0.0f) && all(sampleUV <= 1.0f))
        {
            float mapDepth = shadowMap.Sample(samp, float3(sampleUV, float(cascadeIndex))).r;
            if (mapDepth < 0.999f)
                shadow += (depth - bias <= mapDepth) ? 1.0f : 0.0f;
            else
                shadow += 1.0f;
        }
        else
        {
            shadow += 1.0f;
        }
    }

    return max(shadow * (1.0f / float(KernelSize)), 0.2f);
}