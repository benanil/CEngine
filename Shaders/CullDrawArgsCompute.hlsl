#include "../Include/RenderLimits.h"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "CommonStructs.hlsl"

struct IndexedDrawCommand
{
    uint numIndices;
    uint numInstances;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

StructuredBuffer<Entity>         entities        : register(t0);
StructuredBuffer<PrimitiveGroup> primitiveGroups : register(t1);

RWStructuredBuffer<uint>               drawDenseIndices      : register(u0, space1);
RWStructuredBuffer<IndexedDrawCommand> drawArgs              : register(u1, space1);
RWStructuredBuffer<uint>               numVisibleInPrimitive : register(u2, space1);

StructuredBuffer<uint> denseToPrimitiveIndex : register(t2);

cbuffer params : register(b0, space2)
{
    float4 frustumPlanes[6];
    uint   numEntities;
    uint   numPrimitiveGroups;
    uint   mode;
    uint   _pad0;
};

float4 UnpackHalf4(uint2 packed)
{
    return float4(UnpackHalf2(packed.x), UnpackHalf2(packed.y));
}

bool AABBVisible(float3 mn, float3 mx)
{
    [unroll]
    for (uint i = 0; i < 5; i++)
    {
        float4 plane = frustumPlanes[i];
        float3 p = lerp(mn, mx, step(0.0f, plane.xyz));
        if (dot(plane.xyz, p) + plane.w < 0.0f)
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

void BuildWorldAABB(Entity entity, PrimitiveGroup group, out float3 worldMin, out float3 worldMax)
{
    float3 localMin = UnpackHalf4(group.aabbMin).xyz;
    float3 localMax = UnpackHalf4(group.aabbMax).xyz;
    float3 center = (localMin + localMax) * 0.5f;
    float3 extent = (localMax - localMin) * 0.5f;

    float4 q = normalize(UnpackRGBA16Snorm(entity.rotation.x, entity.rotation.y));
    float3 s = float3(UnpackVec3XY11Z10Unorm(entity.scaleSparse.x)) * 10.0f;

    float3x3 rotM = M33FromQuaternionF32(q);
    float3 axisX = rotM[0] * s.x;
    float3 axisY = rotM[1] * s.y;
    float3 axisZ = rotM[2] * s.z;

    float3 worldCenter = entity.position.xyz + QMulVec3F32(q, center * s);
    float3 worldExtent = abs(axisX) * extent.x + abs(axisY) * extent.y + abs(axisZ) * extent.z;

    worldMin = worldCenter - worldExtent;
    worldMax = worldCenter + worldExtent;
}

#ifndef MOBILE

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupThreadID)
{
    uint idx = tid.x;

    if (mode == 0)
    {
        if (idx >= numPrimitiveGroups) return;
        PrimitiveGroup group        = primitiveGroups[idx];
        numVisibleInPrimitive[idx]  = 0;
        drawArgs[idx].numIndices    = group.numIndices;
        drawArgs[idx].numInstances  = 0;
        drawArgs[idx].firstIndex    = group.indexOffset;
        drawArgs[idx].vertexOffset  = int(group.vertexOffset);
        drawArgs[idx].firstInstance = 0;
        return;
    }

    bool visible = false;
    uint primitiveIdx = 0;
    uint entityIdx = idx;

    if (idx < numEntities)
    {
        primitiveIdx = denseToPrimitiveIndex[idx];
        PrimitiveGroup group = primitiveGroups[primitiveIdx];

        if (group.valid != 0)
        {
            float3 worldMin, worldMax;
            BuildWorldAABB(entities[idx], group, worldMin, worldMax);
            visible = AABBVisible(worldMin, worldMax);
        }
    }

    // Each lane that shares a primitiveIdx with others in the wave can
    // contribute to a single atomic instead of N separate atomics.
    // We elect one lane per unique primitiveIdx to do the atomic, then
    // distribute offsets back via prefix sum over the matching lanes.
    [loop]
    while (WaveActiveAnyTrue(visible))
    {
        // Pick one visible primitive each iteration. WaveReadLaneFirst can read
        // an invisible lane, so use a reduction that ignores invisible lanes.
        uint wavePrimitiveIdx = WaveActiveMin(visible ? primitiveIdx : ~0u);
        bool sameGroup = visible && (primitiveIdx == wavePrimitiveIdx);
    
        // Count how many lanes in this wave are writing to firstLane's primitive.
        uint groupCount     = WaveActiveCountBits(sameGroup);
        // Each contributing lane gets a unique offset within that batch.
        uint laneOffset     = WavePrefixCountBits(sameGroup);
    
        if (sameGroup)
        {
            uint baseSlot = 0;
            if (WaveIsFirstLane() || laneOffset == 0)
            {
                // One atomic per unique primitiveIdx per wave iteration.
                PrimitiveGroup group = primitiveGroups[primitiveIdx];
                InterlockedAdd(numVisibleInPrimitive[primitiveIdx], groupCount, baseSlot);
                InterlockedMax(drawArgs[primitiveIdx].numInstances, baseSlot + groupCount);
                // Broadcast base slot to all lanes that share this primitive.
                // Store temporarily; other lanes retrieve via WaveReadLaneAt.
            }
    
            // The elected lane is the first one with sameGroup == true.
            uint electedLane = WaveActiveMin(sameGroup ? WaveGetLaneIndex() : ~0u);
            uint baseSlotBroadcast = WaveReadLaneAt(baseSlot, electedLane);
    
            PrimitiveGroup group = primitiveGroups[primitiveIdx];
            drawDenseIndices[group.entityOffset + baseSlotBroadcast + laneOffset] = entityIdx;
    
            visible = false; // mark as handled
        }
    }
}
#else
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;

    if (mode == 0)
    {
        if (idx >= numPrimitiveGroups) return;

        PrimitiveGroup group = primitiveGroups[idx];
        numVisibleInPrimitive[idx] = 0;
        drawArgs[idx].numIndices = group.numIndices;
        drawArgs[idx].numInstances = 0;
        drawArgs[idx].firstIndex = group.indexOffset;
        drawArgs[idx].vertexOffset = int(group.vertexOffset);
        drawArgs[idx].firstInstance = 0;
        return;
    }

    if (idx >= numEntities) return;

    uint primitiveIdx = denseToPrimitiveIndex[idx];
    PrimitiveGroup group = primitiveGroups[primitiveIdx];
    if (group.valid == 0) return;

    float3 worldMin;
    float3 worldMax;
    BuildWorldAABB(entities[idx], group, worldMin, worldMax);
    if (!AABBVisible(worldMin, worldMax)) return;

    uint visibleIdx;
    InterlockedAdd(numVisibleInPrimitive[primitiveIdx], 1, visibleIdx);
    drawDenseIndices[group.entityOffset + visibleIdx] = idx;
    InterlockedMax(drawArgs[primitiveIdx].numInstances, visibleIdx + 1);
}
#endif
