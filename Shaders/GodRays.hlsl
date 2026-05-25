#define GODRAY_NUM_SAMPLES 64

float ComputeGodRays(float2 uv)
{
    const float rayExposure = 0.2f;
    const float decay = 0.96815f;
    const float density = 0.926f;
    const float weight = 0.587f;

    if (godRayIntensity <= 0.0f) return 0.0f;
    if (any(sunPos < -0.5f) || any(sunPos > 1.5f)) return 0.0f;

    float2 deltaTexCoord = (uv - sunPos) * (density / float(GODRAY_NUM_SAMPLES));
    float illuminationDecay = 1.0f;
    float result = 0.0f;
    float2 sampleUV = uv;

    [loop]
    for (int i = 0; i < GODRAY_NUM_SAMPLES; i++)
    {
        sampleUV -= deltaTexCoord;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f))
        {
            illuminationDecay *= decay;
            continue;
        }

        float2 diff = sampleUV - sunPos;
        diff.x *= float(outputSize.x) / max(float(outputSize.y), 1.0f);
        float hasSun = dot(diff, diff) < 0.004f ? 1.0f : 0.0f;
        float hasSky = DepthTexture.SampleLevel(DepthSampler, sampleUV, 0.0f) >= 0.9999f ? 1.0f : 0.0f;
        float raySample = (0.35f * hasSun * hasSky + 0.012f * hasSky) * illuminationDecay * weight;
        result += raySample;
        illuminationDecay *= decay;
    }

    return clamp(result * rayExposure * godRayIntensity * EaseOut(uv.y), 0.0f, 1.0f);
}
