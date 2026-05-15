#ifndef PBR_HLSL
#define PBR_HLSL

#include "Math.hlsl"

#ifndef PBR_DEBUG_OUTPUT
    #define PBR_DEBUG_OUTPUT 0
#endif

float4 UnpackColor4Uint(uint color)
{
    return float4(
        float((color >> 0u)  & 0xFFu),
        float((color >> 8u)  & 0xFFu),
        float((color >> 16u) & 0xFFu),
        float((color >> 24u) & 0xFFu)) * (1.0f / 255.0f);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(MATH_PI * denom * denom, 0.0001f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotV / max(NdotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 DecodeNormalRG(float2 normalRG)
{
    float2 xy = normalRG * 2.0f - 1.0f;
    float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return normalize(float3(xy, z));
}

float3 ApplyPBR(float3 albedo, float3 normal, float3 viewDir, float metallic, float roughness)
{
    normal    = normalize(normal);

    #if PBR_DEBUG_OUTPUT == 1
        return normal;
    #elif PBR_DEBUG_OUTPUT == 2
        return albedo;
    #elif PBR_DEBUG_OUTPUT == 3
        return float3(metallic, roughness, 0.0f);
    #endif

    viewDir   = normalize(viewDir);
    roughness = clamp(roughness, 0.045f, 1.0f);
    metallic  = saturate(metallic);

    float3 lightDir = normalize(float3(-0.45f, 0.8f, 0.25f));
    float3 halfVec  = normalize(viewDir + lightDir);
    float3 radiance = float3(3.0f, 2.9f, 2.7f) * 2.0;

    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float NdotH = saturate(dot(normal, halfVec));
    float HdotV = saturate(dot(halfVec, viewDir));

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F  = FresnelSchlick(HdotV, f0);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);

    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.0001f);
    float3 diffuse  = (1.0f - F) * (1.0f - metallic) * albedo * MATH_OneDivPI;
    float3 direct   = (diffuse + specular) * radiance * NdotL;
    float3 ambient  = albedo * 0.1f;
    return ambient + direct + specular;
}

#endif
