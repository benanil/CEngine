// Tiled Forward+ light culling. One thread group per 16x16 screen tile. Each group
// reduces the tile's view-space depth range from the prepass depth, builds the tile's
// view-space sub-frustum (4 side planes through the camera + near/far from the depth
// range), then cooperatively tests every light sphere and writes the surviving light
// indices into a flat per-tile list consumed by the forward opaque shaders.
#include "../../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"

// Read-only (space0): sampled depth first, then the light storage buffer.
Texture2D<float>           DepthTexture : register(t0, space0);
SamplerState               DepthSampler : register(s0, space0);
StructuredBuffer<LightGPU> sLights      : register(t1, space0);

// Read-write (space1).
RWStructuredBuffer<uint2> sLightGrid        : register(u0, space1); // per tile {offset, count}
RWStructuredBuffer<uint>  sLightIndex       : register(u1, space1); // flat light index list
RWStructuredBuffer<uint>  sLightVisibility  : register(u3, space1); // per-light, 1 if in any tile
globallycoherent RWStructuredBuffer<uint>  sLightIndexCounter: register(u2, space1); // single global allocator

cbuffer Params : register(b0, space2)
{
    float4x4 invProjection;
    float4x4 view;
    uint2 screenSize;
    uint   tileSize;
    uint   tilesX;
    uint   tilesY;
    uint   numLights;
    uint   resetMode; // 1 = clear the global counter / per-light visibility, then return
    uint   padding0;
};

groupshared uint gMinDepth;
groupshared uint gMaxDepth;
groupshared uint gLightCount;
groupshared uint gIndexOffset;
groupshared uint gLightList[MAX_LIGHTS_PER_TILE];

float3 ViewRay(float2 uv)
{
    // direction from the camera (origin in view space) through a screen uv at the far plane
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    float4 v = mul(invProjection, clip);
    return v.xyz / v.w;
}

float ViewLinearDepth(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 v = mul(invProjection, clip);
    return -(v.z / v.w); // view looks down -z, return positive linear depth
}

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    // resetMode pass: a single small dispatch clears the global allocator and the
    // per-light visibility flags before the main per-tile dispatch runs.
    if (resetMode != 0u)
    {
        uint flat = groupId.x * 256u + groupIndex;
        if (flat == 0u) sLightIndexCounter[0] = 0u;
        if (flat < numLights) sLightVisibility[flat] = 0u;
        return;
    }

    if (groupIndex == 0u)
    {
        gMinDepth = 0x7F7FFFFFu; // ~FLT_MAX
        gMaxDepth = 0u;
        gLightCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixel = groupId.xy * tileSize + gtid.xy;
    if (pixel.x < screenSize.x && pixel.y < screenSize.y)
    {
        float depth = DepthTexture.Load(int3(pixel, 0));
        if (depth < 1.0f) // ignore sky / cleared depth
        {
            float2 uv = (float2(pixel) + 0.5f) / float2(screenSize);
            float ld = ViewLinearDepth(uv, depth);
            InterlockedMin(gMinDepth, asuint(ld));
            InterlockedMax(gMaxDepth, asuint(ld));
        }
    }
    GroupMemoryBarrierWithGroupSync();

    float minDepth = asfloat(gMinDepth);
    float maxDepth = asfloat(gMaxDepth);
    bool tileHasGeometry = gMaxDepth != 0u;

    // Tile sub-frustum side planes (through the camera origin) in view space.
    float2 uv0 = float2(groupId.xy * tileSize) / float2(screenSize);
    float2 uv1 = float2(min((groupId.xy + 1u) * tileSize, screenSize)) / float2(screenSize);
    float3 c00 = ViewRay(float2(uv0.x, uv0.y));
    float3 c10 = ViewRay(float2(uv1.x, uv0.y));
    float3 c01 = ViewRay(float2(uv0.x, uv1.y));
    float3 c11 = ViewRay(float2(uv1.x, uv1.y));
    float3 centerDir = normalize(c00 + c10 + c01 + c11);

    float3 planes[4];
    planes[0] = normalize(cross(c00, c10)); // top edge
    planes[1] = normalize(cross(c10, c11)); // right edge
    planes[2] = normalize(cross(c11, c01)); // bottom edge
    planes[3] = normalize(cross(c01, c00)); // left edge
    [unroll]
    for (uint p = 0u; p < 4u; p++)
        if (dot(planes[p], centerDir) < 0.0f) planes[p] = -planes[p]; // orient inward

    if (tileHasGeometry)
    {
        for (uint i = groupIndex; i < numLights; i += 256u)
        {
            LightGPU light = sLights[i];
            float radius = max(light.positionRadius.w, 0.001f);
            float3 cv = mul(view, float4(light.positionRadius.xyz, 1.0f)).xyz;

            bool inside = true;
            [unroll]
            for (uint pp = 0u; pp < 4u; pp++)
                if (dot(planes[pp], cv) < -radius) inside = false;

            float lightLD = -cv.z;
            if (lightLD + radius < minDepth) inside = false;
            if (lightLD - radius > maxDepth) inside = false;

            if (inside)
            {
                uint slot;
                InterlockedAdd(gLightCount, 1u, slot);
                if (slot < MAX_LIGHTS_PER_TILE) gLightList[slot] = i;
                sLightVisibility[i] = 1u;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint count = min(gLightCount, MAX_LIGHTS_PER_TILE);
    if (groupIndex == 0u)
    {
        uint offset = 0u;
        if (count > 0u) InterlockedAdd(sLightIndexCounter[0], count, offset);
        gIndexOffset = offset;
        uint tileIndex = groupId.y * tilesX + groupId.x;
        sLightGrid[tileIndex] = uint2(offset, count);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint j = groupIndex; j < count; j += 256u)
        sLightIndex[gIndexOffset + j] = gLightList[j];
}
