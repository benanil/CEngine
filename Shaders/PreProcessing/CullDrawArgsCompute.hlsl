#include "../../Include/RenderLimits.h"
#include "../Bitpack.hlsl"
#include "../Math.hlsl"
#include "../CommonStructs.hlsl"

#define DEBUG_CULLED_AABBS 0
#define SMALL_OBJECT_CULL_ENABLED   1

#define SMALL_CULL_PIXEL_DIAMETER   1.0f
#define SMALL_CULL_DEPTH_THRESHOLD  0.98f

#define LOD0_PIXEL_DIAMETER         128.0f
#define LOD1_PIXEL_DIAMETER         40.0f
#define LOD_PIXEL_QUANTUM           8.0f

#define HIZ_RECT_PADDING_PIXELS     1.0f
#define CLIP_MIN_W                  0.0001f

Texture2D<float>                 hiZTexture            : register(t0);
StructuredBuffer<Entity>         entities              : register(t1);
StructuredBuffer<PrimitiveGroup> primitiveGroups       : register(t2);
StructuredBuffer<uint>           denseToPrimitiveIndex : register(t3);

RWStructuredBuffer<uint>                drawSparseIndices    : register(u0, space1);
RWStructuredBuffer<IndexedDrawCommand>  drawArgs             : register(u1, space1);
RWStructuredBuffer<LineVertex>          lineVertices         : register(u2, space1);
RWStructuredBuffer<IndirectDrawCommand> lineDrawCommand      : register(u3, space1);
RWStructuredBuffer<uint>                visibleSparseIndices : register(u4, space1);
RWStructuredBuffer<uint>                visibilityMask       : register(u5, space1);
RWStructuredBuffer<uint>                visibleCount         : register(u6, space1);
RWStructuredBuffer<IndirectDispatchCommand> dispatchArgs      : register(u7, space1);

cbuffer params : register(b0, space2)
{
    float4 frustumPlanes[6];
    uint   maxEntityID;
    uint   numPrimitiveGroups;
    uint   mode;
    uint   enableVisibilityOutput;
    float4x4 viewProjection;
    uint2  hiZSize;
    uint   hiZMipCount;
    uint   enableHiZ;
    float  hiZDepthBias;
    uint   lodCount;
    uint   sparseIndexLODStride;
    uint   enableLODSelection;
    uint   forcedLOD;
    float  lodDistanceModifier;
    float2 lodPadding;
};

struct ProjectedAABB
{
    float2 ndcMin;
    float2 ndcMax;

    float2 uvMin;
    float2 uvMax;

    float  nearestDepth;
    float  screenDiameterNDC;
    float  screenDiameterPixels;

    uint   anyFront;
    uint   anyBehind;
};

float3x3 M33FromQuaternionF32(float4 q)
{
    float x2 = q.x + q.x;
    float y2 = q.y + q.y;
    float z2 = q.z + q.z;

    float xx = q.x * x2;
    float yy = q.y * y2;
    float zz = q.z * z2;

    float xy = q.x * y2;
    float xz = q.x * z2;
    float yz = q.y * z2;

    float wx = q.w * x2;
    float wy = q.w * y2;
    float wz = q.w * z2;

    float3x3 m;
    m[0] = float3(1.0f - yy - zz, xy + wz,        xz - wy);
    m[1] = float3(xy - wz,        1.0f - xx - zz, yz + wx);
    m[2] = float3(xz + wy,        yz - wx,        1.0f - xx - yy);
    return m;
}

float3 QMulVec3F32(float4 q, float3 v)
{
    return v + cross(q.xyz, cross(q.xyz, v) + v * q.w) * 2.0f;
}

uint WangHash(uint x)
{
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

void AddLine(float3 a, float3 b, uint idx, uint color)
{
    lineVertices[idx].x = a.x;
    lineVertices[idx].y = a.y;
    lineVertices[idx].z = a.z;
    lineVertices[idx].color = color;

    lineVertices[idx + 1].x = b.x;
    lineVertices[idx + 1].y = b.y;
    lineVertices[idx + 1].z = b.z;
    lineVertices[idx + 1].color = color;
}

void AddAABBLineColored(float3 worldMin, float3 worldMax, uint color)
{
    uint start;
    InterlockedAdd(lineDrawCommand[1].numVertices, 24, start);

    if (start + 24 > MAX_LINE_COUNT)
    {
        lineDrawCommand[1].numVertices = MAX_LINE_COUNT;
        return;
    }

    float3 p000 = float3(worldMin.x, worldMin.y, worldMin.z);
    float3 p100 = float3(worldMax.x, worldMin.y, worldMin.z);
    float3 p010 = float3(worldMin.x, worldMax.y, worldMin.z);
    float3 p110 = float3(worldMax.x, worldMax.y, worldMin.z);

    float3 p001 = float3(worldMin.x, worldMin.y, worldMax.z);
    float3 p101 = float3(worldMax.x, worldMin.y, worldMax.z);
    float3 p011 = float3(worldMin.x, worldMax.y, worldMax.z);
    float3 p111 = float3(worldMax.x, worldMax.y, worldMax.z);

    AddLine(p000, p100, start + 0,  color);
    AddLine(p100, p110, start + 2,  color);
    AddLine(p110, p010, start + 4,  color);
    AddLine(p010, p000, start + 6,  color);

    AddLine(p001, p101, start + 8,  color);
    AddLine(p101, p111, start + 10, color);
    AddLine(p111, p011, start + 12, color);
    AddLine(p011, p001, start + 14, color);

    AddLine(p000, p001, start + 16, color);
    AddLine(p100, p101, start + 18, color);
    AddLine(p110, p111, start + 20, color);
    AddLine(p010, p011, start + 22, color);
}

void AddAABBLine(float3 worldMin, float3 worldMax)
{
    uint3 d = uint3(worldMin * 100.0f);
    AddAABBLineColored(worldMin, worldMax, WangHash(d.x + d.y + d.z));
}

void BuildWorldAABB(
    Entity entity,
    PrimitiveGroup group,
    out float3 worldCenter,
    out float3 worldExtent,
    out float3 worldMin,
    out float3 worldMax)
{
    float3 localMin = group.aabbMin.xyz;
    float3 localMax = group.aabbMax.xyz;

    float3 localCenter = (localMin + localMax) * 0.5f;
    float3 localExtent = (localMax - localMin) * 0.5f;

    float4 q = VecNorm(UnpackRGBA16Snorm(entity.rotation.x, entity.rotation.y));
    float3 s = float3(UnpackVec3XY11Z10Unorm(entity.scale.x)) * 10.0f;

    float3x3 rotM = M33FromQuaternionF32(q);

    float3 axisX = rotM[0] * s.x;
    float3 axisY = rotM[1] * s.y;
    float3 axisZ = rotM[2] * s.z;

    worldCenter = entity.position.xyz + QMulVec3F32(q, localCenter * s);

    worldExtent =
        abs(axisX) * localExtent.x +
        abs(axisY) * localExtent.y +
        abs(axisZ) * localExtent.z;

    worldMin = worldCenter - worldExtent;
    worldMax = worldCenter + worldExtent;
}

bool AABBVisible(float3 center, float3 extent)
{
    [unroll]
    for (uint i = 0; i < 6; i++)
    {
        float4 plane = frustumPlanes[i];
        float d = dot(plane.xyz, center) + plane.w;
        float r = dot(abs(plane.xyz), extent);

        if (d + r < -0.001f)
            return false;
    }

    return true;
}

void ProjectCorner(float3 p, inout ProjectedAABB proj)
{
    float4 clip = mul(viewProjection, float4(p, 1.0f));

    if (clip.w <= CLIP_MIN_W)
    {
        proj.anyBehind = 1u;
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    proj.ndcMin = min(proj.ndcMin, ndc.xy);
    proj.ndcMax = max(proj.ndcMax, ndc.xy);

    proj.nearestDepth = min(proj.nearestDepth, saturate(ndc.z));
    proj.anyFront = 1u;
}

void ProjectAABB(out ProjectedAABB proj, float3 mn, float3 mx)
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

    ProjectCorner(float3(mn.x, mn.y, mn.z), proj);
    ProjectCorner(float3(mx.x, mn.y, mn.z), proj);
    ProjectCorner(float3(mn.x, mx.y, mn.z), proj);
    ProjectCorner(float3(mx.x, mx.y, mn.z), proj);

    ProjectCorner(float3(mn.x, mn.y, mx.z), proj);
    ProjectCorner(float3(mx.x, mn.y, mx.z), proj);
    ProjectCorner(float3(mn.x, mx.y, mx.z), proj);
    ProjectCorner(float3(mx.x, mx.y, mx.z), proj);

    if (proj.anyFront == 0u)
        return;

    float2 uvA = proj.ndcMin * float2(0.5f, -0.5f) + 0.5f;
    float2 uvB = proj.ndcMax * float2(0.5f, -0.5f) + 0.5f;

    proj.uvMin = saturate(min(uvA, uvB));
    proj.uvMax = saturate(max(uvA, uvB));

    float2 rectPixels = (proj.uvMax - proj.uvMin) * max(float2(hiZSize), float2(1.0f, 1.0f));

    proj.screenDiameterNDC = max(proj.ndcMax.x - proj.ndcMin.x,
                                 proj.ndcMax.y - proj.ndcMin.y);

    proj.screenDiameterPixels = max(rectPixels.x, rectPixels.y);
}

bool ProjectedRectOutside(in ProjectedAABB proj)
{
    return proj.ndcMax.x < -1.0f ||
           proj.ndcMin.x >  1.0f ||
           proj.ndcMax.y < -1.0f ||
           proj.ndcMin.y >  1.0f;
}

bool AABBTooSmallAndFar(in ProjectedAABB proj)
{
    #if SMALL_OBJECT_CULL_ENABLED
    if (enableHiZ == 0u)
        return false;

    if (proj.anyFront == 0u || proj.anyBehind != 0u)
        return false;

    return proj.screenDiameterPixels < SMALL_CULL_PIXEL_DIAMETER &&
           proj.nearestDepth >= SMALL_CULL_DEPTH_THRESHOLD;
    #else
    return false;
    #endif
}

bool AABBOccludedHiZ(in ProjectedAABB proj)
{
    if (enableHiZ == 0u || hiZMipCount == 0u || hiZSize.x == 0u || hiZSize.y == 0u)
        return false;

    if (proj.anyFront == 0u || proj.anyBehind != 0u)
        return false;

    if (ProjectedRectOutside(proj))
        return false;

    if (proj.nearestDepth >= 1.0f)
        return false;

    float2 rectMin = proj.uvMin;
    float2 rectMax = proj.uvMax;

    float2 rectPadding = HIZ_RECT_PADDING_PIXELS / float2(hiZSize);
    rectMin = saturate(rectMin - rectPadding);
    rectMax = saturate(rectMax + rectPadding);

    float2 extentPixels = max((rectMax - rectMin) * float2(hiZSize), 1.0f);

    float mip = clamp(
        ceil(log2(max(extentPixels.x, extentPixels.y))),
        0.0f,
        float(hiZMipCount - 1u));

    float lowerMip = max(mip - 1.0f, 0.0f);

    float2 lowerMipSize = max(floor(float2(hiZSize) / exp2(lowerMip)), 1.0f);
    float2 lowerDims = ceil(rectMax * lowerMipSize) - floor(rectMin * lowerMipSize);

    if (lowerDims.x <= 2.0f && lowerDims.y <= 2.0f)
        mip = lowerMip;

    int selectedMip = int(mip);

    uint2 mipSize = max(hiZSize >> uint(selectedMip), uint2(1u, 1u));
    float2 mipSizeF = float2(mipSize);

    uint2 p0 = uint2(max(floor(rectMin * mipSizeF - 0.001f), 0.0f));
    uint2 p1 = min(uint2(floor(rectMax * mipSizeF + 0.001f)), mipSize - 1u);

    uint2 sampleDims = p1 - p0 + 1u;

    // Avoid accidentally turning one huge object into a slow 64+ sample path.
    if (sampleDims.x > 8u || sampleDims.y > 8u)
        return false;

    float maxOccluderDepth = 0.0f;

    for (uint sy = p0.y; sy <= p1.y; sy++)
    {
        for (uint sx = p0.x; sx <= p1.x; sx++)
        {
            maxOccluderDepth = max(maxOccluderDepth, hiZTexture.Load(int3(sx, sy, selectedMip)));
        }
    }

    return maxOccluderDepth + hiZDepthBias < proj.nearestDepth;
}

uint SelectLOD(in ProjectedAABB proj)
{
    if (lodCount <= 1u)
        return 0u;

    if (forcedLOD < lodCount)
        return forcedLOD;

    if (enableLODSelection == 0u)
        return 0u;

    if (proj.anyFront == 0u)
        return 0u;

    float screenDiameter = proj.screenDiameterPixels * lodDistanceModifier;
    screenDiameter = floor(screenDiameter / LOD_PIXEL_QUANTUM) * LOD_PIXEL_QUANTUM;

    if (screenDiameter > LOD0_PIXEL_DIAMETER)
        return 0u;

    if (screenDiameter > LOD1_PIXEL_DIAMETER)
        return min(1u, lodCount - 1u);

    return min(2u, lodCount - 1u);
}

void Initialize(uint idx)
{
    uint numDraws = numPrimitiveGroups * lodCount;

    if (idx < numDraws)
    {
        uint primitiveIdx = idx / lodCount;
        uint lod = idx - primitiveIdx * lodCount;

        PrimitiveGroup group = primitiveGroups[primitiveIdx];
        drawArgs[idx].numIndices    = group.lodNumIndices[lod];
        drawArgs[idx].numInstances  = 0;
        drawArgs[idx].firstIndex    = group.lodIndexOffset[lod];
        drawArgs[idx].vertexOffset  = 0;
        drawArgs[idx].firstInstance = 0;
    }

    if (idx == 0)
    {
        lineDrawCommand[0].numVertices   = 0;
        lineDrawCommand[0].numInstances  = 1;
        lineDrawCommand[0].firstVertex   = 1;
        lineDrawCommand[0].firstInstance = 0;

        lineDrawCommand[1].numVertices   = 0;
        lineDrawCommand[1].numInstances  = 1;
        lineDrawCommand[1].firstVertex   = 0;
        lineDrawCommand[1].firstInstance = 0;

        if (enableVisibilityOutput != 0u)
        {
            visibleCount[0] = 0;

            dispatchArgs[0].groupCountX = 0;
            dispatchArgs[0].groupCountY = 1;
            dispatchArgs[0].groupCountZ = 1;

            dispatchArgs[1].groupCountX = numPrimitiveGroups * lodCount;
            dispatchArgs[1].groupCountY = 0;
            dispatchArgs[1].groupCountZ = 0;
        }
    }

    if (enableVisibilityOutput != 0u && idx < maxEntityID)
    {
        visibilityMask[idx] = 0;
    }
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;

    if (mode == 0u)
    {
        Initialize(idx);
        return;
    }

    if (idx >= maxEntityID)
        return;

    uint sparse = idx;
    uint primitiveIdx = denseToPrimitiveIndex[sparse];

    PrimitiveGroup group = primitiveGroups[primitiveIdx];

    float3 worldCenter;
    float3 worldExtent;
    float3 worldMin;
    float3 worldMax;
    BuildWorldAABB(entities[sparse], group, worldCenter, worldExtent, worldMin, worldMax);

    bool frustumVisible = AABBVisible(worldCenter, worldExtent);

    bool needProjection = frustumVisible && (enableHiZ != 0u ||
                                             enableLODSelection != 0u ||
                                             SMALL_OBJECT_CULL_ENABLED != 0);
    ProjectedAABB proj;
    bool tooSmallFar = false;
    bool hiZOccluded = false;

    if (needProjection)
    {
        ProjectAABB(proj, worldMin, worldMax);
        tooSmallFar = AABBTooSmallAndFar(proj);

        if (!tooSmallFar)
            hiZOccluded = AABBOccludedHiZ(proj);
    }

    bool visible = frustumVisible && !tooSmallFar && !hiZOccluded;

#if DEBUG_CULLED_AABBS
    if (!frustumVisible) AddAABBLineColored(worldMin, worldMax, 0xFF0000FFu);
    else if (tooSmallFar) AddAABBLineColored(worldMin, worldMax, 0x00FFFFFFu);
    else if (hiZOccluded) AddAABBLineColored(worldMin, worldMax, 0xFFFF00FFu);
    else return;
#else
    if (!visible)
        return;
#endif

    uint lod = 0u;

    if ((enableLODSelection != 0u || forcedLOD < lodCount) && lodCount > 1u)
    {
        if (forcedLOD >= lodCount && !needProjection)
            ProjectAABB(proj, worldMin, worldMax);

        lod = SelectLOD(proj);
    }

    uint drawIdx = primitiveIdx * lodCount + lod;

    uint localVisibleIdx;
    InterlockedAdd(drawArgs[drawIdx].numInstances, 1, localVisibleIdx);

    uint globalVisibleIdx;
    InterlockedAdd(lineDrawCommand[0].firstInstance, 1, globalVisibleIdx);

    drawSparseIndices[lod * sparseIndexLODStride + group.entityOffset + localVisibleIdx] = sparse;

    if (enableVisibilityOutput != 0u)
    {
        uint visibleSparse = entities[sparse].sparse;

        uint old;
        InterlockedCompareExchange(visibilityMask[visibleSparse], 0, 1, old);

        InterlockedMax(dispatchArgs[1].groupCountY, (localVisibleIdx + 32u) / 32u);
        InterlockedMax(dispatchArgs[1].groupCountZ, (group.lodNumVertices[lod] + 31u) / 32u);

        if (old == 0u)
        {
            uint visibleSlot;
            InterlockedAdd(visibleCount[0], 1, visibleSlot);

            visibleSparseIndices[visibleSlot] = visibleSparse;

            if ((visibleSlot & 31u) == 0u)
            {
                InterlockedAdd(dispatchArgs[0].groupCountX, 1);
            }
        }
    }
}
