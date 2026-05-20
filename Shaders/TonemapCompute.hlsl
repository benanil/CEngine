cbuffer TonemapParams : register(b0, space2)
{
    uint2 outputSize;
    float exposure;
    float gamma;
    float2 sunPos;
    float godRayIntensity;
    float time;
    float cloudTime;
    float3 padding;
    float4x4 invViewProj;
    float4 cameraPosition;
    float4 sunDirection;
};

Texture2D<float4> SourceTexture : register(t0, space0);
Texture2D<float> DepthTexture   : register(t1, space0);
Texture2DArray<float> Noise3DTexture : register(t2, space0);
SamplerState SourceSampler      : register(s0, space0);
SamplerState DepthSampler       : register(s1, space0);
SamplerState NoiseSampler       : register(s2, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

#define GODRAY_NUM_SAMPLES 64

float EaseOut(float x)
{
    float r = 1.0f - x;
    return 1.0f - r * r;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 345.45f));
    p += dot(p, p + 34.345f);
    return frac(p.x * p.y);
}

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float Rand(float p)
{
    p = frac(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return frac(p);
}

float Noise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0f - 2.0f * f);

    float lb = Hash21(i + float2(0.0f, 0.0f));
    float rb = Hash21(i + float2(1.0f, 0.0f));
    float lt = Hash21(i + float2(0.0f, 1.0f));
    float rt = Hash21(i + float2(1.0f, 1.0f));

    return lerp(lerp(lb, rb, f.x), lerp(lt, rt, f.x), f.y);
}

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

float Sqr(float x)
{
    return x * x;
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

    float t = cloudTime * 0.1f;
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
        float hasSky = DepthTexture.SampleLevel(DepthSampler, sampleUV, 0.0f) > 0.9992f ? 1.0f : 0.0f;
        float raySample = (0.35f * hasSun * hasSky + 0.012f * hasSky) * illuminationDecay * weight;
        result += raySample;
        illuminationDecay *= decay;
    }

    return clamp(result * rayExposure * godRayIntensity * EaseOut(uv.y), 0.0f, 1.0f);
}

float3 MulMat3(float3 row0, float3 row1, float3 row2, float3 v)
{
    return float3(dot(row0, v), dot(row1, v), dot(row2, v));
}

float3 AgXDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2
         - 40.14f * x4 * x
         + 31.96f * x4
         - 6.868f * x2 * x
         + 0.4298f * x2
         + 0.1191f * x
         - 0.00232f;
}

float3 TonemapAgX(float3 color)
{
    color = MulMat3(
        float3(0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f),
        float3(0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f),
        float3(0.0423756549057051f, 0.0784336f,          0.879142973793104f),
        color);

    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    color = clamp(log2(max(color, 1e-10f)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = AgXDefaultContrastApprox(color);

    color = MulMat3(
        float3(1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f),
        float3(-0.0528968517574562f, 1.15190312990417f,   -0.0989611768448433f),
        float3(-0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f),
        color);

    return saturate(color);
}

float3 TonemapACES(float3 color)
{
    float3 v = MulMat3(
        float3(0.59719f, 0.35458f, 0.04823f),
        float3(0.07600f, 0.90834f, 0.01566f),
        float3(0.02840f, 0.13383f, 0.83777f),
        color);

    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    v = a / b;

    v = MulMat3(
        float3(1.60475f, -0.53108f, -0.07367f),
        float3(-0.10208f, 1.10813f, -0.00605f),
        float3(-0.00327f, -0.07276f, 1.07602f),
        v);

    return saturate(v);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 color = SourceTexture.SampleLevel(SourceSampler, uv, 0.0f).rgb;
    float depth = DepthTexture.SampleLevel(DepthSampler, uv, 0.0f);
    if (depth > 0.9992f)
    {
        color = ComputeSky(SkyRayDirection(uv));
    }
    float godRays = ComputeGodRays(uv);
    color += godRays * float3(1.35f, 1.08f, 0.72f);
    color = TonemapACES(color * exposure);
    color = pow(color, 1.0f / max(gamma, 0.001f));
    OutputTexture[tid.xy] = float4(color, 1.0f);
}
