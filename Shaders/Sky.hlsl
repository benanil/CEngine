#include "NoiseHash.hlsl"

float EaseOut(float x)
{
    float r = 1.0f - x;
    return 1.0f - r * r;
}

float Sqr(float x) { return x * x; }

float NoiseTexture2D(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0f - 2.0f * f);
    float lb = Noise3DTexture.SampleLevel(NoiseSampler, float3(frac((i + float2(0.0f, 0.0f)) / 64.0f), 0.0f), 0.0f);
    float rb = Noise3DTexture.SampleLevel(NoiseSampler, float3(frac((i + float2(1.0f, 0.0f)) / 64.0f), 0.0f), 0.0f);
    float lt = Noise3DTexture.SampleLevel(NoiseSampler, float3(frac((i + float2(0.0f, 1.0f)) / 64.0f), 0.0f), 0.0f);
    float rt = Noise3DTexture.SampleLevel(NoiseSampler, float3(frac((i + float2(1.0f, 1.0f)) / 64.0f), 0.0f), 0.0f);
    return lerp(lerp(lb, rb, f.x), lerp(lt, rt, f.x), f.y);
}

float SampleNoiseVolume(float3 uv)
{
    uv = frac(uv);
    float z = uv.z * 64.0f;
    float z0 = floor(z);
    float z1 = z0 + 1.0f;
    float f = frac(z);
    float a = Noise3DTexture.SampleLevel(NoiseSampler, float3(uv.xy, z0), 0.0f);
    float b = Noise3DTexture.SampleLevel(NoiseSampler, float3(uv.xy, z1 >= 64.0f ? 0.0f : z1), 0.0f);
    return lerp(a, b, f);
}

float FBM(float2 uv)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int i = 0; i < 5; i++)
    {
        value += Noise(uv) * amplitude;
        amplitude *= 0.5f;
        uv *= 2.0f;
    }
    return value;
}

float VolumeFBM(float3 uv)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int i = 0; i < 5; i++)
    {
        value += SampleNoiseVolume(uv) * amplitude;
        amplitude *= 0.5f;
        uv *= 2.0f;
    }
    return value;
}

float CloudFBM(float2 uv)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int i = 0; i < 8; i++)
    {
        value += NoiseTexture2D(uv) * amplitude;
        amplitude *= 0.5f;
        uv *= 2.0f;
    }
    return value;
}

float3 SkyRayDirection(float2 uv)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    float4 world = mul(invViewProj, clip);
    world.xyz /= max(abs(world.w), 0.00001f);
    return normalize(world.xyz - cameraPosition.xyz);
}

float GetStars(float3 rd)
{
    float theta = atan2(rd.x, rd.z) * (0.5f / 3.14159265f);
    float phi = acos(clamp(rd.y, -1.0f, 1.0f)) * (1.0f / 3.14159265f);
    return pow(Noise(float2(theta, phi) * 1000.0f), 240.0f) * 3.0f;
}

float3 NightSky(float3 rd)
{
    float sc = time * 0.128f;
    float3 galaxyNormal = float3(sin(sc), cos(sc), 0.0f);
    float3 galaxyCenterDir = normalize(float3(-0.607f, 0.0f, 0.607f));

    float galaxyPlane = 16.0f * dot(galaxyNormal, rd);
    galaxyPlane = 1.0f / (galaxyPlane * galaxyPlane + 1.0f);
    float galaxyCenter = dot(galaxyCenterDir, rd) * 0.5f + 0.5f;
    galaxyCenter *= galaxyCenter;

    float dustNoise = SampleNoiseVolume(rd * 0.5f + 0.5f + float3(0.0f, time * 0.002f, 0.0f));
    float dust = 1.0f - (dustNoise * 0.6f + 1.2f) * galaxyPlane * galaxyCenter;
    float3 dustColor = float3(0.7f, 0.5f, 0.6f);
    float3 galaxyColor = float3(0.5f, 0.4f, 0.2f);
    float3 galaxy = 0.6f * galaxyPlane * galaxyCenter * galaxyColor;

    return galaxy * (dustColor + dust) + GetStars(rd);
}

float GetCloudIntensity(float x)
{
    return lerp(Hash21(float2(floor(x), 0.0f)), Hash21(float2(floor(x) + 1.0f, 0.0f)), frac(x));
}

float Cloud(float3 ro, float3 rd)
{
    if (rd.y <= 0.0f) return 0.0f;

    const float skyPlaneScale = 100000.0f;
    float dist = (skyPlaneScale - ro.y) / max(rd.y, 0.001f);
    float2 p = (ro + dist * rd).xz;
    p *= 1.2f / skyPlaneScale;

    float t = cloudTime * 0.05f;
    float den = CloudFBM(float2(p.x - t, p.y - t));
    float cloudMix = smoothstep(0.4f, 0.8f, den);
    cloudMix *= saturate(rd.y * 6.0f);
    return cloudMix;
}

float3 SunDisk(float3 rd, float3 sunDir, float3 sunCol, float inverse)
{
    float sundot = saturate(dot(rd, sunDir * inverse));
    float sunDisk = smoothstep(0.9985f, 1.0f, sundot);
    return max(sunCol * sunDisk * 2.0f, 0.0f);
}

float3 DaySky(float3 rd, float effective)
{
    float3 skyCol = float3(0.23f, 0.4f, 0.75f);
    float3 horizonColor = lerp(float3(0.9f, 0.885f, 0.92f), 0.98f * float3(0.9418f, 0.9418f, 0.9418f), effective * 2.1f);
    return lerp(skyCol, horizonColor, pow(1.0f - max(rd.y, 0.0f), 28.0f));
}

float3 ComputeSky(float3 rd)
{
    float3 sunDir = normalize(sunDirection.xyz);
    float sunPhase = time * 0.035f + sunDirection.w;
    float active = sin(sunPhase);
    float effective = abs(cos(sunPhase) * 0.4f);

    float3 day = DaySky(rd, effective) * max(smoothstep(0.0f, 0.1f, max(active, 0.0f)), 0.015f);
    float3 night = NightSky(rd) * smoothstep(-0.4f, 0.75f, max(-active + effective, 0.0f));

    float3 sunColor = float3(1.0f, 0.82f, 0.55f) * 2.0f;
    float3 moonColor = float3(0.7f, 0.8f, 0.9f) * 0.75f;
    float3 skyCol = day + night + SunDisk(rd, sunDir, sunColor, 1.0f) + SunDisk(rd, sunDir, moonColor, -1.0f);

    float cloudMix = Cloud(cameraPosition.xyz, rd);
    float cloudDay = max(smoothstep(0.0f, 0.228f, max(active, 0.0f)), 0.1f);
    float3 cloudColor = float3(1.0f, 1.0f, 1.0f) * cloudDay;
    skyCol = lerp(skyCol, cloudColor, cloudMix);
    return max(skyCol, 0.0f);
}