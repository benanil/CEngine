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

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float D_GGX(float roughness, float NoH, float3 n, float3 h)
{
    float3 NxH = cross(n, h);
    float oneMinusNoHSquared = dot(NxH, NxH);
    float a = NoH * roughness;
    float k = min(roughness / max(oneMinusNoHSquared + a * a, 0.0000077f), 453.5f);
    return k * k * MATH_OneDivPI;
}

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
    float a2 = roughness * roughness;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5f / max(lambdaV + lambdaL, 0.0000077f);
}

float V_SmithGGXCorrelatedFast(float roughness, float NoV, float NoL)
{
    return 0.5f / max(lerp(2.0f * NoL * NoV, NoL + NoV, roughness), 0.0000077f);
}

float3 F_Schlick(float3 f0, float f90, float VoH)
{
    return f0 + (f90 - f0) * Pow5(1.0f - VoH);
}

float Distribution(float roughness, float NoH, float3 n, float3 h)
{
    return D_GGX(roughness, NoH, n, h);
}

float Visibility(float roughness, float NoV, float NoL)
{
    return V_SmithGGXCorrelatedFast(roughness, NoV, NoL);
}

float3 Fresnel(float3 f0, float LoH)
{
    float f90 = saturate(dot(f0, float3(16.5f, 16.5f, 16.5f)));
    return F_Schlick(f0, f90, LoH);
}

float Diffuse(float roughness, float NoV, float NoL, float LoH)
{
    return MATH_OneDivPI;
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

    float3 lightDir = normalize(float3(-0.5f, 0.5f, 0.0f));
    float3 halfVec  = normalize(viewDir + lightDir);
    float3 radiance = float3(3.0f, 2.9f, 2.7f) * 2.0f;

    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float NdotH = saturate(dot(normal, halfVec));
    float HdotV = saturate(dot(halfVec, viewDir));

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = Fresnel(f0, HdotV);
    float D = Distribution(roughness, NdotH, normal, halfVec);
    float V = Visibility(roughness, NdotV, NdotL);
    float diffuse = Diffuse(roughness, NdotV, NdotL, HdotV);

    float3 specular = D * V * F;
    float3 diffuseColor = (1.0f - F) * (1.0f - metallic) * albedo * diffuse;
    float3 direct = (diffuseColor + specular) * radiance * NdotL;
    float3 ambient = albedo * 0.1f;
    return ambient + direct;
}

#endif
