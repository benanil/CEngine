#include "../../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"
#include "../LOD.hlsl"

StructuredBuffer<LightGPU> lights : register(t0, space0);
StructuredBuffer<uint> lightVisibility : register(t1, space0);

RWStructuredBuffer<uint> tileCounts : register(u0, space1);
RWStructuredBuffer<uint> tileLightIndices : register(u1, space1);

cbuffer Params : register(b0, space2)
{
    uint2 outputSize;
    uint2 tileCount;
    uint  numLights;
    uint  mode;
    uint2 padding0;
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 cameraRight;
    float4 cameraUp;
};

void InitProjectedAABB(out ProjectedAABB proj)
{
    proj.ndcMin = float2( 1e30f,  1e30f);
    proj.ndcMax = float2(-1e30f, -1e30f);
    proj.uvMin = 0.0f;
    proj.uvMax = 0.0f;
    proj.nearestDepth = 1.0f;
    proj.screenDiameterNDC = 0.0f;
    proj.screenDiameterPixels = 0.0f;
    proj.anyFront = 0u;
    proj.anyBehind = 0u;
}

void FinishProjectedAABB(inout ProjectedAABB proj)
{
    if (proj.anyFront == 0u)
        return;

    float2 uvA = proj.ndcMin * float2(0.5f, -0.5f) + 0.5f;
    float2 uvB = proj.ndcMax * float2(0.5f, -0.5f) + 0.5f;
    proj.uvMin = saturate(min(uvA, uvB));
    proj.uvMax = saturate(max(uvA, uvB));
}

float4 ProjectLightUVRect(LightGPU light)
{
    float3 center = light.positionRadius.xyz;
    float radius = max(light.positionRadius.w, 0.001f);
    float3 toCamera = cameraPosition.xyz - center;
    if (dot(toCamera, toCamera) <= radius * radius)
        return float4(0.0f, 0.0f, 1.0f, 1.0f);

    ProjectedAABB proj;
    InitProjectedAABB(proj);
    ProjectCorner(center, viewProjection, proj);
    ProjectCorner(center + cameraRight.xyz * radius, viewProjection, proj);
    ProjectCorner(center - cameraRight.xyz * radius, viewProjection, proj);
    ProjectCorner(center + cameraUp.xyz * radius, viewProjection, proj);
    ProjectCorner(center - cameraUp.xyz * radius, viewProjection, proj);
    FinishProjectedAABB(proj);

    if (proj.anyBehind != 0u)
        return float4(0.0f, 0.0f, 1.0f, 1.0f);
    if (proj.anyFront == 0u || ProjectedRectOutside(proj))
        return float4(1.0f, 1.0f, 0.0f, 0.0f);
    return float4(proj.uvMin, proj.uvMax);
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;
    uint totalTiles = tileCount.x * tileCount.y;
    if (mode == 0u)
    {
        if (idx < totalTiles)
            tileCounts[idx] = 0u;
        return;
    }

    if (idx >= numLights || lightVisibility[idx] == 0u)
        return;

    float4 uvRect = ProjectLightUVRect(lights[idx]);
    if (uvRect.x >= uvRect.z || uvRect.y >= uvRect.w)
        return;

    float2 size = max(float2(outputSize), float2(1.0f, 1.0f));
    uint2 tileMin = min(uint2(floor(uvRect.xy * size / float(LIGHT_TILE_SIZE))), tileCount - 1u);
    uint2 tileMax = min(uint2(floor(max(uvRect.zw * size - 1.0f, 0.0f) / float(LIGHT_TILE_SIZE))), tileCount - 1u);

    for (uint y = tileMin.y; y <= tileMax.y; y++)
    {
        for (uint x = tileMin.x; x <= tileMax.x; x++)
        {
            uint tileIdx = y * tileCount.x + x;
            uint slot;
            InterlockedAdd(tileCounts[tileIdx], 1u, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
                tileLightIndices[tileIdx * MAX_LIGHTS_PER_TILE + slot] = idx;
        }
    }
}
