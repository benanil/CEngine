#include "ShadowCascade.hlsl"
#include "../Include/RenderLimits.h"

cbuffer SDSMSetupParams : register(b0, space2)
{
    float4x4 invViewProj;
    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 sunDirection;
    float4 setupParams; // x near, y far, z max shadow distance, w valid bounds
    float4 fovParams;   // x tanHalfFov, y aspect, z overlap, w lambda
};

Texture2D<float2> DepthBounds : register(t0, space0);
RWStructuredBuffer<ShadowCascadeBuffer> ShadowCascades : register(u0, space1);

#define Mul44(a, b) mul(a, b)

float4x4 InverseRotationTranslation(float3x3 r, float3 t)
{
    float4x4 inv = float4x4(float4(r._11_21_31, 0.0f),
                            float4(r._12_22_32, 0.0f),
                            float4(r._13_23_33, 0.0f),
                            float4(0.0f, 0.0f, 0.0f, 1.0f));
    inv[3][0] = -dot(t, r[0]);
    inv[3][1] = -dot(t, r[1]);
    inv[3][2] = -dot(t, r[2]);
    return inv;
}

float4x4 OrthographicProjection(float l, float b, float r, float t, float zn, float zf)
{
    return float4x4(float4(2.0f / (r - l), 0.0f, 0.0f, 0.0f),
                    float4(0.0f, 2.0f / (t - b), 0.0f, 0.0f),
                    float4(0.0f, 0.0f, -2.0f / (zf - zn), 0.0f),
                    float4((l + r) / (l - r), (t + b) / (b - t), -(zf + zn) / (zf - zn), 1.0f));
}

float LinearDistanceFromDepth(float depth)
{
    float4 world = mul(float4(0.0f, 0.0f, depth, 1.0f), invViewProj);
    world.xyz /= max(abs(world.w), 0.00001f);
    return dot(world.xyz - cameraPosition.xyz, cameraForward.xyz);
}

float CascadeSplitDistance(float shadowNear, float shadowFar, uint cascade)
{
    float splitNear = max(shadowNear, min(SHADOW_SPLIT_NEAR_DISTANCE, shadowFar * 0.5f));
    float p = float(cascade + 1u) / float(SHADOW_CASCADE_COUNT);
    float logSplit = splitNear * pow(abs(shadowFar / splitNear), p);
    float uniformSplit = lerp(splitNear, shadowFar, p);
    return clamp(lerp(uniformSplit, logSplit, fovParams.w), shadowNear, shadowFar);
}

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    ShadowCascadeBuffer output;
    float shadowNear = setupParams.x;
    float shadowFar = min(setupParams.y, setupParams.z);

    if (setupParams.w > 0.5f)
    {
        uint width, height, levels;
        DepthBounds.GetDimensions(0, width, height, levels);
        uint finalMip = ((levels - 1u) / 3u) * 3u;
        float2 depthBounds = DepthBounds.Load(int3(0, 0, finalMip));
        if (depthBounds.x <= depthBounds.y && depthBounds.x < 0.9999f && depthBounds.y > 0.0f)
        {
            float visibleNear = LinearDistanceFromDepth(depthBounds.x);
            float visibleFar = LinearDistanceFromDepth(depthBounds.y);
            shadowNear = max(setupParams.x, visibleNear - SDSM_NEAR_PADDING);
            shadowFar = min(setupParams.z, max(shadowNear + 10.0f, visibleFar + SDSM_FAR_PADDING));
        }
    }

    float3 lightDir = normalize(sunDirection.xyz);
    float3 lightZ = lightDir;
    float3 upDir = abs(lightDir.y) > 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 lightX = normalize(cross(upDir, lightZ));
    float3 lightY = cross(lightZ, lightX);
    float3x3 lightRot;
    lightRot[0] = lightX;
    lightRot[1] = lightY;
    lightRot[2] = lightZ;

    // Create a matrix that pulls the world origin into camera-local space
    float4x4 worldToCameraTranslation = float4x4(
        float4(1.0f, 0.0f, 0.0f, 0.0f),
        float4(0.0f, 1.0f, 0.0f, 0.0f),
        float4(0.0f, 0.0f, 1.0f, 0.0f),
        float4(-cameraPosition.xyz, 1.0f)
    );

    float previousSplit = shadowNear;
    [unroll]
    for (uint cascade = 0u; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        float split = CascadeSplitDistance(shadowNear, shadowFar, cascade);
        output.splitDistances[cascade] = split;
        float nearDist = cascade > 0u ? max(shadowNear, previousSplit - fovParams.z) : previousSplit;
        float farDist = cascade + 1u < SHADOW_CASCADE_COUNT ? min(shadowFar, split + fovParams.z) : split;
        previousSplit = split;

        float nearH = fovParams.x * nearDist;
        float nearW = nearH * fovParams.y;
        float farH = fovParams.x * farDist;
        float farW = farH * fovParams.y;
        
        // CAMERA-RELATIVE: Omit cameraPosition.xyz from the calculations to preserve fp32 precision
        float3 nearCenter = cameraForward.xyz * nearDist;
        float3 farCenter = cameraForward.xyz * farDist;
        float3 corners[8] = {
            nearCenter + cameraUp.xyz * nearH + cameraRight.xyz * -nearW,
            nearCenter + cameraUp.xyz * nearH + cameraRight.xyz * nearW,
            nearCenter + cameraUp.xyz * -nearH + cameraRight.xyz * -nearW,
            nearCenter + cameraUp.xyz * -nearH + cameraRight.xyz * nearW,
            farCenter  + cameraUp.xyz * farH  + cameraRight.xyz * -farW,
            farCenter  + cameraUp.xyz * farH  + cameraRight.xyz * farW,
            farCenter  + cameraUp.xyz * -farH  + cameraRight.xyz * -farW,
            farCenter  + cameraUp.xyz * -farH  + cameraRight.xyz * farW
        };

        float3 center = 0.0f;
        uint i;
        [unroll] for (i = 0u; i < 8u; i++) center += corners[i];
        center *= 0.125f;

        float radius = 1.0f;
        [unroll] for (i = 0u; i < 8u; i++) radius = max(radius, length(corners[i] - center));
        float eyeDistance = radius + SHADOW_CAMERA_DISTANCE + SHADOW_CASTER_DEPTH_MARGIN;
        float3 eye = center + lightDir * eyeDistance;
        
        // This lightView is now a 'Camera-Space to Light-Space' transformation matrix
        float4x4 lightView = InverseRotationTranslation(lightRot, eye);

        float3 mins =  3.402823466e+38f;
        float3 maxes = -3.402823466e+38f;
        [unroll]
        for (i = 0u; i < 8u; i++)
        {
            float3 cornerLS = mul(float4(corners[i], 1.0f), lightView).xyz;
            mins = min(mins, cornerLS);
            maxes = max(maxes, cornerLS);
        }

        float extent = ceil(max(maxes.x - mins.x, maxes.y - mins.y) + 2.0f);
        float halfExtent = extent * 0.5f;
        float texelSize = extent / float(max(SHADOW_MAP_SIZE >> cascade, 1u));
        float2 centerLS = floor((((mins.xy + maxes.xy) * 0.5f) / texelSize) + 0.5f) * texelSize;
        float4x4 shadowProj = OrthographicProjection(centerLS.x - halfExtent, centerLS.y - halfExtent,
                                                     centerLS.x + halfExtent, centerLS.y + halfExtent,
                                                     SHADOW_NEAR_PLANE, eyeDistance + radius + SHADOW_CASTER_DEPTH_MARGIN);
        
        // CHAIN: World-Space Mesh Coordinates -> Camera Space -> Light Space -> Projection Space
        float4x4 shadowMatrix = Mul44(worldToCameraTranslation, Mul44(lightView, shadowProj));
        
        uint row = cascade * 4u;
        output.lightViewProj[row + 0u] = shadowMatrix._11_21_31_41;
        output.lightViewProj[row + 1u] = shadowMatrix._12_22_32_42;
        output.lightViewProj[row + 2u] = shadowMatrix._13_23_33_43;
        output.lightViewProj[row + 3u] = shadowMatrix._14_24_34_44;
    }
    output.splitDistances.w = output.splitDistances.z;
    ShadowCascades[0] = output;
}