#include "../../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"
#include "../LOD.hlsl"

#define HIZ_RECT_PADDING_PIXELS 1.0f

Texture2D<float> hiZTexture : register(t0);
StructuredBuffer<LightGPU> lights : register(t1);

RWStructuredBuffer<LightDrawInfo>       drawInfos       : register(u0, space1);
RWStructuredBuffer<IndirectDrawCommand> drawArgs        : register(u1, space1);
RWStructuredBuffer<LineVertex>          lineVertices    : register(u2, space1);
RWStructuredBuffer<IndirectDrawCommand> lineDrawCommand : register(u3, space1);
// Per-light visibility (1 = passed frustum + occlusion culling, 0 = culled),
// indexed by light index. Read back to the CPU one frame later to skip shadow
// map rendering for lights that contribute nothing this frame.
RWStructuredBuffer<uint>                lightVisibility : register(u4, space1);

cbuffer Params : register(b0, space2)
{
    float4   frustumPlanes[6];
    uint     numLights;
    uint     mode;
    uint2    outputSize;
    float4x4 viewProjection;
    uint2    hiZSize;
    uint     hiZMipCount;
    uint     enableHiZ;
    float    hiZDepthBias;
    float3   cameraPosition;
    uint     enableFrustum;
    float4x4 invViewProjection;
    uint     showLightRects;
    float3   cameraRight;
    uint     padding0;
    float3   cameraUp;
    uint     padding1;
};

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(invViewProjection, clip);
    return world.xyz / max(abs(world.w), 0.00001f);
}

void AddLine(float3 a, float3 b, uint idx, uint color)
{
    lineVertices[idx].x = a.x;
    lineVertices[idx].y = a.y;
    lineVertices[idx].z = a.z;
    lineVertices[idx].color = color;

    lineVertices[idx + 1u].x = b.x;
    lineVertices[idx + 1u].y = b.y;
    lineVertices[idx + 1u].z = b.z;
    lineVertices[idx + 1u].color = color;
}

void AddScreenRectLine(float4 uvRect, uint color)
{
    uint start;
    InterlockedAdd(lineDrawCommand[1].numVertices, 8u, start);
    if (start + 8u > MAX_LINE_COUNT)
    {
        lineDrawCommand[1].numVertices = MAX_LINE_COUNT;
        return;
    }

    float3 p0 = ReconstructWorldPosition(uvRect.xy, 0.0f);
    float3 p1 = ReconstructWorldPosition(float2(uvRect.z, uvRect.y), 0.0f);
    float3 p2 = ReconstructWorldPosition(uvRect.zw, 0.0f);
    float3 p3 = ReconstructWorldPosition(float2(uvRect.x, uvRect.w), 0.0f);
    AddLine(p0, p1, start + 0u, color);
    AddLine(p1, p2, start + 2u, color);
    AddLine(p2, p3, start + 4u, color);
    AddLine(p3, p0, start + 6u, color);
}

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

    float2 rectPixels = (proj.uvMax - proj.uvMin) * max(float2(outputSize), float2(1.0f, 1.0f));
    proj.screenDiameterNDC = max(proj.ndcMax.x - proj.ndcMin.x,
                                 proj.ndcMax.y - proj.ndcMin.y);
    proj.screenDiameterPixels = max(rectPixels.x, rectPixels.y);
}

void ProjectLightSphere(out ProjectedAABB proj, float3 center, float radius)
{
    // NOTE: keep all sample points in the sphere's center depth plane (no
    // forward/view-axis offset). Offsetting a corner toward the camera makes it
    // cross the near plane for any light within ~radius of the camera, which
    // sets proj.anyBehind and forces the light to skip occlusion culling
    // (RectOccludedHiZ and main() both bail on anyBehind). The +-right/+-up
    // points already define the screen-space rect, so a view-axis offset adds
    // nothing but spurious near-plane crossings.
    InitProjectedAABB(proj);
    ProjectCorner(center, viewProjection, proj);
    ProjectCorner(center + cameraRight * radius, viewProjection, proj);
    ProjectCorner(center - cameraRight * radius, viewProjection, proj);
    ProjectCorner(center + cameraUp * radius, viewProjection, proj);
    ProjectCorner(center - cameraUp * radius, viewProjection, proj);
    FinishProjectedAABB(proj);
}

bool SphereVisible(float3 center, float radius)
{
    [unroll]
    for (uint i = 0; i < 6; i++)
    {
        float4 plane = frustumPlanes[i];
        if (dot(plane.xyz, center) + plane.w + length(plane.xyz) * radius < -0.001f)
            return false;
    }
    return true;
}

bool RectOccludedHiZ(in ProjectedAABB proj)
{
    if (enableHiZ == 0u || hiZMipCount == 0u || hiZSize.x == 0u || hiZSize.y == 0u)
        return false;
    if (proj.anyFront == 0u || proj.anyBehind != 0u || ProjectedRectOutside(proj) || proj.nearestDepth >= 1.0f)
        return false;

    float2 rectMin = saturate(proj.uvMin - HIZ_RECT_PADDING_PIXELS / float2(hiZSize));
    float2 rectMax = saturate(proj.uvMax + HIZ_RECT_PADDING_PIXELS / float2(hiZSize));
    float2 extentPixels = max((rectMax - rectMin) * float2(hiZSize), 1.0f);
    float mip = clamp(ceil(log2(max(extentPixels.x, extentPixels.y))), 0.0f, float(hiZMipCount - 1u));

    float lowerMip = max(mip - 1.0f, 0.0f);
    float2 lowerMipSize = max(floor(float2(hiZSize) / exp2(lowerMip)), 1.0f);
    float2 lowerDims = ceil(rectMax * lowerMipSize) - floor(rectMin * lowerMipSize);
    if (lowerDims.x <= 2.0f && lowerDims.y <= 2.0f)
        mip = lowerMip;

    uint selectedMip = uint(mip);
    uint2 mipSize = max(hiZSize >> selectedMip, uint2(1u, 1u));
    float2 mipSizeF = float2(mipSize);
    uint2 p0 = uint2(max(floor(rectMin * mipSizeF - 0.001f), 0.0f));
    uint2 p1 = min(uint2(floor(rectMax * mipSizeF + 0.001f)), mipSize - 1u);

    uint2 sampleDims = p1 - p0 + 1u;
    if (sampleDims.x > 8u || sampleDims.y > 8u)
        return false;

    float maxOccluderDepth = 0.0f;
    for (uint y = p0.y; y <= p1.y; y++)
    {
        for (uint x = p0.x; x <= p1.x; x++)
            maxOccluderDepth = max(maxOccluderDepth, hiZTexture.Load(int3(x, y, selectedMip)));
    }
    return maxOccluderDepth + hiZDepthBias < proj.nearestDepth;
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;
    if (mode == 0u)
    {
        if (idx == 0u)
        {
            drawArgs[0].numVertices = 6u;
            drawArgs[0].numInstances = 0u;
            drawArgs[0].firstVertex = 0u;
            drawArgs[0].firstInstance = 0u;
        }
        return;
    }

    if (idx >= numLights)
        return;

    // Default to culled. Each thread owns one light index, so every light in
    // [0, numLights) gets written exactly once below; we flip this to 1 only on
    // the path that adds the light to the deferred draw list.
    lightVisibility[idx] = 0u;

    LightGPU light = lights[idx];
    float3 center = light.positionRadius.xyz;
    float radius = max(light.positionRadius.w, 0.001f);
    if (enableFrustum != 0u && !SphereVisible(center, radius))
        return;

    float3 toCamera = cameraPosition - center;
    bool fullscreen = dot(toCamera, toCamera) <= radius * radius;

    ProjectedAABB proj;
    if (fullscreen)
    {
        proj.uvMin = float2(0.0f, 0.0f);
        proj.uvMax = float2(1.0f, 1.0f);
        proj.anyFront = 1u;
        proj.anyBehind = 1u;
        proj.nearestDepth = 0.0f;
    }
    else
    {
        ProjectLightSphere(proj, center, radius);
        if (proj.anyBehind != 0u)
        {
            proj.uvMin = float2(0.0f, 0.0f);
            proj.uvMax = float2(1.0f, 1.0f);
            proj.anyFront = 1u;
            proj.nearestDepth = 0.0f;
        }
        if (proj.anyFront == 0u || ProjectedRectOutside(proj))
            return;

        bool occluded = RectOccludedHiZ(proj);
        if (occluded)
        {
            if (showLightRects != 0u)
                AddScreenRectLine(float4(proj.uvMin, proj.uvMax), 0xFF0000FFu);
            return;
        }
    }

    lightVisibility[idx] = 1u;

    uint visibleIndex;
    InterlockedAdd(drawArgs[0].numInstances, 1u, visibleIndex);
    if (visibleIndex >= MAX_LIGHT_COUNT)
        return;

    drawInfos[visibleIndex].uvRect = float4(proj.uvMin, proj.uvMax);
    drawInfos[visibleIndex].lightIndex = idx;
    drawInfos[visibleIndex].flags = fullscreen ? LIGHT_DRAW_FULLSCREEN : 0u;
    drawInfos[visibleIndex].padding = uint2(0u, 0u);

    if (showLightRects != 0u)
        AddScreenRectLine(drawInfos[visibleIndex].uvRect, fullscreen ? 0xFF00FFFFu : 0xFF40FF40u);
}
