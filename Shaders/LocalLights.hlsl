#ifndef LOCAL_LIGHTS_HLSL
#define LOCAL_LIGHTS_HLSL

uint PointShadowFace(float3 lightToWorld)
{
    float3 a = abs(lightToWorld);
    if (a.x >= a.y && a.x >= a.z)
        return lightToWorld.x >= 0.0f ? 0u : 1u;
    if (a.y >= a.z)
        return lightToWorld.y >= 0.0f ? 2u : 3u;
    return lightToWorld.z >= 0.0f ? 4u : 5u;
}

float SamplePointShadowLocal(LightGPU light, float3 worldPos, float3 normal, float3 lightDir)
{
    if ((light.flags & LIGHT_FLAG_SHADOWED) == 0u || light.shadowIndex >= POINT_SHADOW_MAX_LIGHTS)
        return 1.0f;

    float3 lightToWorld = worldPos - light.positionRadius.xyz;
    uint face = PointShadowFace(lightToWorld);
    uint matrixIndex = light.shadowIndex * POINT_SHADOW_FACE_COUNT + face;
    float4 shadowPos = MulPointShadowSide(PointShadowMatrices[matrixIndex], float4(worldPos, 1.0f));
    float3 proj = shadowPos.xyz / max(abs(shadowPos.w), 0.00001f);
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
        return 1.0f;

    uint width, height, layers;
    PointShadowTexture.GetDimensions(width, height, layers);
    if (light.shadowIndex >= layers)
        return 1.0f;

    float ndotl = saturate(dot(normal, lightDir));
    float bias = max(0.0025f * (1.0f - ndotl), 0.0008f);
    uint faceWidth = max(width / POINT_SHADOW_FACE_COUNT, 1u);
    float2 texel = 1.0f / float2(faceWidth, max(height, 1u));
    float shadow = 0.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = clamp(uv + ShadowKernel[i] * texel * 1.5f, texel * 0.5f, 1.0f - texel * 0.5f);
        float2 atlasUV = float2((sampleUV.x + float(face)) / float(POINT_SHADOW_FACE_COUNT), sampleUV.y);
        float mapDepth = PointShadowTexture.SampleLevel(PointShadowSampler, float3(atlasUV, float(light.shadowIndex)), 0.0f);
        shadow += float(mapDepth >= depth - bias);
    }
    return max(shadow * 0.125f, 0.15f);
}

float SampleSpotShadowLocal(LightGPU light, float3 worldPos, float3 normal, float3 lightDir)
{
    if ((light.flags & LIGHT_FLAG_SHADOWED) == 0u || light.shadowIndex >= SPOT_SHADOW_MAX_LIGHTS)
        return 1.0f;

    uint width, height, layers;
    SpotShadowTexture.GetDimensions(width, height, layers);
    if (light.shadowIndex >= layers)
        return 1.0f;

    float4 shadowPos = MulPointShadowSide(SpotShadowMatrices[light.shadowIndex], float4(worldPos, 1.0f));
    float3 proj = shadowPos.xyz / max(abs(shadowPos.w), 0.00001f);
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(normal, lightDir));
    float bias = max(0.0015f * (1.0f - ndotl), 0.0005f);
    float2 texel = 1.0f / float2(max(width, 1u), max(height, 1u));
    float shadow = 0.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = uv + ShadowKernel[i] * texel * 1.25f;
        float mapDepth = SpotShadowTexture.SampleLevel(SpotShadowSampler, float3(sampleUV, float(light.shadowIndex)), 0.0f);
        shadow += float(mapDepth >= depth - bias);
    }
    return max(shadow * 0.125f, 0.15f);
}

float3 ApplyLocalLight(float3 albedo, float3 normal, float3 viewDir, float metallic, float perceptualRoughness, LightGPU light, float3 worldPos, float ao)
{
    float3 lightVector = light.positionRadius.xyz - worldPos;
    float distanceSq = dot(lightVector, lightVector);
    float radius = max(light.positionRadius.w, 0.001f);
    if (distanceSq >= radius * radius)
        return 0.0f;

    float distanceToLight = sqrt(max(distanceSq, 0.00001f));
    float3 lightDir = lightVector / distanceToLight;
    float attenuation = saturate(1.0f - distanceToLight / radius);
    attenuation *= attenuation;

    if (light.type == LIGHT_TYPE_SPOT)
    {
        float coneCos = light.directionCone.w;
        float spotCos = dot(normalize(-light.directionCone.xyz), lightDir);
        float spot = saturate((spotCos - coneCos) / max(1.0f - coneCos, 0.0001f));
        attenuation *= spot * spot;
    }

    float shadow = light.type == LIGHT_TYPE_POINT ? SamplePointShadowLocal(light, worldPos, normal, lightDir) :
                   (light.type == LIGHT_TYPE_SPOT ? SampleSpotShadowLocal(light, worldPos, normal, lightDir) : 1.0f);
    float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation * shadow;
    return ApplyPBRLight(albedo, normal, viewDir, metallic, perceptualRoughness, radiance, lightDir) * saturate(ao);
}

#endif
