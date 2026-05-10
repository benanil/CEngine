#include "../Include/RenderLimits.h"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "CommonStructs.hlsl"

StructuredBuffer<Entity>         entities                 : register(t0);
StructuredBuffer<PrimitiveGroup> primitiveGroups          : register(t1);
StructuredBuffer<uint>           denseToPrimitiveIndex    : register(t2);

RWStructuredBuffer<uint>                drawDenseIndices  : register(u0, space1);
RWStructuredBuffer<IndexedDrawCommand>  drawArgs          : register(u1, space1);
RWStructuredBuffer<LineVertex>          lineVertices      : register(u2, space1);

// first line draw command's first member(numVertices) is number of visible entities
RWStructuredBuffer<IndirectDrawCommand> lineDrawCommand   : register(u3, space1);
RWStructuredBuffer<uint>                visibleDenseIndices : register(u4, space1);
RWStructuredBuffer<uint>                visibilityMask      : register(u5, space1);
RWStructuredBuffer<uint>                visibleCount       : register(u6, space1);
RWStructuredBuffer<IndirectDispatchCommand> dispatchArgs   : register(u7, space1);

cbuffer params : register(b0, space2)
{
    float4 frustumPlanes[6];
    uint   maxEntityID;
    uint   numPrimitiveGroups;
    uint   mode;
    uint   enableVisibilityOutput;
};

float4 UnpackHalf4(uint2 packed)
{
    return float4(UnpackHalf2(packed.x), UnpackHalf2(packed.y));
}

bool AABBVisible(float3 mn, float3 mx)
{
    [unroll]
    for (uint i = 0; i < 6; i++)
    {
        float4 plane = frustumPlanes[i];
        float4 p = float4(lerp(mn, mx, step(0.0f, plane.xyz)), 1.0f);
        if (dot(plane, p) < 0.0f)
            return false;
    }
    return true;
}

float3x3 M33FromQuaternionF32(float4 q)
{
    float3x3 mat;
    const float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0 - (2.0 * (num8 + num7));
    mat[0][1] = 2.0 * (num6 + num5);
    mat[0][2] = 2.0 * (num4 - num3);
    mat[1][0] = 2.0 * (num6 - num5);
    mat[1][1] = 1.0 - (2.0 * (num7 + num9));
    mat[1][2] = 2.0 * (num2 + num);
    mat[2][0] = 2.0 * (num4 + num3);
    mat[2][1] = 2.0 * (num2 - num);
    mat[2][2] = 1.0 - (2.0 * (num8 + num9));
    return mat;
}

float3 QMulVec3F32(float4 quat, float3 vec) {
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0);
}

u32 WangHash(u32 x) { 
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

void AddLine(float3 a, float3 b, u32 idx, u32 color)
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

void AddAABBLine(float3 worldMin, float3 worldMax)
{
    uint start;
    InterlockedAdd(lineDrawCommand[1].numVertices, 24, start);
    uint3 d = uint3(worldMin * 100);

    u32 color = WangHash(d.x + d.y + d.z);
    // if (start + 23 >= MAX_LINE_VERTEX_COUNT) return;
    float3 p000 = float3(worldMin.x, worldMin.y, worldMin.z);
    float3 p100 = float3(worldMax.x, worldMin.y, worldMin.z);
    float3 p010 = float3(worldMin.x, worldMax.y, worldMin.z);
    float3 p110 = float3(worldMax.x, worldMax.y, worldMin.z);

    float3 p001 = float3(worldMin.x, worldMin.y, worldMax.z);
    float3 p101 = float3(worldMax.x, worldMin.y, worldMax.z);
    float3 p011 = float3(worldMin.x, worldMax.y, worldMax.z);
    float3 p111 = float3(worldMax.x, worldMax.y, worldMax.z);

    AddLine(p000, p100, start + 0, color);
    AddLine(p100, p110, start + 2, color);
    AddLine(p110, p010, start + 4, color);
    AddLine(p010, p000, start + 6, color);

    AddLine(p001, p101, start + 8 , color);
    AddLine(p101, p111, start + 10, color);
    AddLine(p111, p011, start + 12, color);
    AddLine(p011, p001, start + 14, color);

    AddLine(p000, p001, start + 16, color);
    AddLine(p100, p101, start + 18, color);
    AddLine(p110, p111, start + 20, color);
    AddLine(p010, p011, start + 22, color);
}

void BuildWorldAABB(Entity entity, PrimitiveGroup group, out float3 worldMin, out float3 worldMax)
{
    float3 localMin = UnpackHalf4(group.aabbMin).xyz;
    float3 localMax = UnpackHalf4(group.aabbMax).xyz;
    float3 center = F3MulF(F3Add(localMin, localMax), 0.5f);
    float3 extent = F3MulF(F3Sub(localMax, localMin), 0.5f);

    float4 q = VecNorm(UnpackRGBA16Snorm(entity.rotation.x, entity.rotation.y));
    float3 s = F3MulF(float3(UnpackVec3XY11Z10Unorm(entity.scale.x)), 10.0f);

    mat3x3 rotM = M33FromQuaternionF32(q);
    float3 axisX = F3MulF(rotM[0], s.x);
    float3 axisY = F3MulF(rotM[1], s.y);
    float3 axisZ = F3MulF(rotM[2], s.z);

    float3 worldCenter = entity.position.xyz + QMulVec3F32(q, center * s);
    float3 worldExtent = F3Add(F3Add(F3MulF(F3Abs(axisX), extent.x),
                                     F3MulF(F3Abs(axisY), extent.y)), 
                                     F3MulF(F3Abs(axisZ), extent.z));

    worldMin = F3Sub(worldCenter, worldExtent);
    worldMax = F3Add(worldCenter, worldExtent);
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;
    if (mode == 0)
    {
        if (idx < numPrimitiveGroups)
        {
            PrimitiveGroup group = primitiveGroups[idx];
            drawArgs[idx].numIndices    = group.numIndices;
            drawArgs[idx].numInstances  = 0;
            drawArgs[idx].firstIndex    = group.indexOffset;
            drawArgs[idx].vertexOffset  = int(group.vertexOffset);
            drawArgs[idx].firstInstance = 0;
        }

        if (idx == 0)
        {
            lineDrawCommand[0].numVertices   = 0; // indirect dispatch X
            lineDrawCommand[0].numInstances  = 1; // indirect dispatch Y
            lineDrawCommand[0].firstVertex   = 1; // indirect dispatch Z
            lineDrawCommand[0].firstInstance = 0; // total visible count

            lineDrawCommand[1].numVertices   = 0;
            lineDrawCommand[1].numInstances  = 1;
            lineDrawCommand[1].firstVertex   = 0;
            lineDrawCommand[1].firstInstance = 0;

            if (enableVisibilityOutput != 0)
            {
                visibleCount[0] = 0;
                dispatchArgs[0].groupCountX = 0;
                dispatchArgs[0].groupCountY = 1;
                dispatchArgs[0].groupCountZ = 1;
            }
        }

        if (enableVisibilityOutput != 0 && idx < maxEntityID)
        {
            visibilityMask[idx] = 0;
        }

        return;
    }

    if (idx >= maxEntityID)
        return;

    uint dense = idx;

    uint primitiveIdx = denseToPrimitiveIndex[dense];
    PrimitiveGroup group = primitiveGroups[primitiveIdx];

    float3 worldMin, worldMax;
    BuildWorldAABB(entities[dense], group, worldMin, worldMax);

    if (!AABBVisible(worldMin, worldMax))
        return;

    if (enableVisibilityOutput == 0)
        AddAABBLine(worldMin, worldMax);
    
    uint localVisibleIdx;
    InterlockedAdd(drawArgs[primitiveIdx].numInstances, 1, localVisibleIdx);

    uint globalVisibleIdx;
    InterlockedAdd(lineDrawCommand[0].firstInstance, 1, globalVisibleIdx);

    drawDenseIndices[group.entityOffset + localVisibleIdx] = dense;

    if (enableVisibilityOutput != 0)
    {
        uint visibleDense = entities[dense].sparse;
        uint old;
        InterlockedCompareExchange(visibilityMask[visibleDense], 0, 1, old);

        if (old == 0)
        {
            uint visibleSlot;
            InterlockedAdd(visibleCount[0], 1, visibleSlot);
            visibleDenseIndices[visibleSlot] = visibleDense;

            if ((visibleSlot & 31u) == 0)
            {
                InterlockedAdd(dispatchArgs[0].groupCountX, 1);
            }
        }
    }
}
