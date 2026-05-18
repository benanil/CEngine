#ifndef PBR_HLSL
#define PBR_HLSL

#include "Math.hlsl"

#ifndef PBR_DEBUG_OUTPUT
    #define PBR_DEBUG_OUTPUT 0
#endif

f16_4 UnpackColor4Uint(uint color)
{
    return f16_4(
        f16((color >> 0u)  & 0xFFu),
        f16((color >> 8u)  & 0xFFu),
        f16((color >> 16u) & 0xFFu),
        f16((color >> 24u) & 0xFFu)) * f16(1.0 / 255.0);
}

f16 DistributionGGX(f16 NdotH, f16 roughness)
{
    f16 a = roughness * roughness;
    f16 a2 = a * a;
    f16 denom = NdotH * NdotH * (a2 - f16(1.0)) + f16(1.0);
    return a2 / max(f16(MATH_PI) * denom * denom, f16(0.0001));
}

f16 GeometrySchlickGGX(f16 NdotV, f16 roughness)
{
    f16 r = roughness + f16(1.0);
    f16 k = (r * r) * f16(0.125);
    return NdotV / max(NdotV * (f16(1.0) - k) + k, f16(0.0001));
}

f16 GeometrySmith(f16 NdotV, f16 NdotL, f16 roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

f16_3 FresnelSchlick(f16 cosTheta, f16_3 f0)
{
    return f0 + (f16(1.0) - f0) * f16(pow(saturate(f16(1.0) - cosTheta), f16(5.0)));
}

f16_3 DecodeNormalRG(f16_2 normalRG)
{
    f16_2 xy = normalRG * f16(2.0) - f16(1.0);
    f16 z = sqrt(saturate(f16(1.0) - dot(xy, xy)));
    return normalize(f16_3(xy, z));
}

f16_3 ApplyPBR(f16_3 albedo, f16_3 normal, f16_3 viewDir, f16 metallic, f16 roughness)
{
    normal    = normalize(normal);

    #if PBR_DEBUG_OUTPUT == 1
        return normal;
    #elif PBR_DEBUG_OUTPUT == 2
        return albedo;
    #elif PBR_DEBUG_OUTPUT == 3
        return f16_3(metallic, roughness, f16(0.0));
    #endif

    viewDir   = normalize(viewDir);
    roughness = clamp(roughness, f16(0.045), f16(1.0));
    metallic  = saturate(metallic);

    f16_3 lightDir = normalize(f16_3(-0.5, 0.5, 0.0));
    f16_3 halfVec  = normalize(viewDir + lightDir);
    f16_3 radiance = f16_3(3.0, 2.9, 2.7) * f16(2.0);

    f16 NdotL = saturate(dot(normal, lightDir));
    f16 NdotV = saturate(dot(normal, viewDir));
    f16 NdotH = saturate(dot(normal, halfVec));
    f16 HdotV = saturate(dot(halfVec, viewDir));

    f16_3 f0 = lerp(f16_3(0.04, 0.04, 0.04), albedo, metallic);
    f16_3 F  = FresnelSchlick(HdotV, f0);
    f16 D = DistributionGGX(NdotH, roughness);
    f16 G = GeometrySmith(NdotV, NdotL, roughness);

    f16_3 specular = (D * G * F) / max(f16(4.0) * NdotV * NdotL, f16(0.0001));
    f16_3 diffuse  = (f16(1.0) - F) * (f16(1.0) - metallic) * albedo * f16(MATH_OneDivPI);
    f16_3 direct   = (diffuse + specular) * radiance * NdotL;
    f16_3 ambient  = albedo * f16(0.1);
    return ambient + direct + specular;
}

#endif
