#define GODRAY_MAX_SAMPLES 128

float ComputeGodRays(float2 uv)
{
    const float rayExposure = 0.2f;
    const float decay = 0.96815f;
    const float density = 0.926f;
    const float weight = 0.587f;

    int numSamples = clamp((int)godRaySamples, 0, GODRAY_MAX_SAMPLES);
    if (numSamples <= 0 || godRayIntensity <= 0.0f) return 0.0f;
    if (any(sunPos < -0.5f) || any(sunPos > 1.5f)) return 0.0f;

    float2 deltaTexCoord = (uv - sunPos) * (density / float(numSamples));
    float illuminationDecay = 1.0f;
    float result = 0.0f;
    float2 sampleUV = uv;

    [loop]
    for (int i = 0; i < numSamples; i++)
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
        float hasSky = DepthTexture.SampleLevel(DepthSampler, sampleUV, 0.0f) <= 0.0001f ? 1.0f : 0.0f; // reversed-Z: sky at far = 0
        float raySample = (0.35f * hasSun * hasSky + 0.012f * hasSky) * illuminationDecay * weight;
        result += raySample;
        illuminationDecay *= decay;
    }

    result *= 64.0f / float(numSamples); // keep the brightness of the reference 64 sample look
    return clamp(result * rayExposure * godRayIntensity * EaseOut(uv.y), 0.0f, 1.0f);
}
