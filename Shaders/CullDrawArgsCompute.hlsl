#include "Bitpack.hlsl"
#include "Math.hlsl"

struct Entity
{
    float4 position;
    uint2  rotation;
    uint2  scale;
};

struct PrimitiveGroup
{
    uint entityOffset;
    uint numEntities;
    uint capacity;
    uint boneStart;
    uint numIndices;
    uint indexOffset;
    uint vertexOffset;
    uint meshIndex;
    uint primitiveIndex;
    uint valid;
    uint2 aabbMin;
    uint2 aabbMax;
};

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
RWStructuredBuffer<uint>               denseToPrimitiveIndex : register(u2, space1);
RWStructuredBuffer<uint>               numVisibleInPrimitive : register(u3, space1);

cbuffer params : register(b0, space2)
{
    float4 frustumPlanes[6];
    uint   numEntities;
    uint   numPrimitiveGroups;
    uint   mode;
    uint   _pad0;
};

float4 LoadHalf4(uint2 packed)
{
    return float4(UnpackHalf2(packed.x), UnpackHalf2(packed.y));
}

float3 RotateVec(float4 q, float3 v)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

bool AABBVisible(float3 mn, float3 mx)
{
    [unroll]
    for (uint i = 0; i < 5; i++)
    {
        float4 p = float4(frustumPlanes[i].x >= 0.0f ? mx.x : mn.x,
                         frustumPlanes[i].y >= 0.0f ? mx.y : mn.y,
                         frustumPlanes[i].z >= 0.0f ? mx.z : mn.z,
                         1.0f);
        if (dot(frustumPlanes[i], p) < 0.0f)
            return false;
    }
    return true;
}

void BuildWorldAABB(Entity entity, PrimitiveGroup group, out float3 worldMin, out float3 worldMax)
{
    float3 localMin = LoadHalf4(group.aabbMin).xyz;
    float3 localMax = LoadHalf4(group.aabbMax).xyz;
    float3 center = (localMin + localMax) * 0.5f;
    float3 extent = (localMax - localMin) * 0.5f;

    float4 q = normalize(UnpackRGBA16Snorm(entity.rotation.x, entity.rotation.y));
    float3 s = float3(UnpackVec3XY11Z10Unorm(entity.scale.x)) * 10.0f;

    float3 axisX = RotateVec(q, float3(s.x, 0.0f, 0.0f));
    float3 axisY = RotateVec(q, float3(0.0f, s.y, 0.0f));
    float3 axisZ = RotateVec(q, float3(0.0f, 0.0f, s.z));
    float3 worldCenter = entity.position.xyz + axisX * center.x + axisY * center.y + axisZ * center.z;
    float3 worldExtent = abs(axisX) * extent.x + abs(axisY) * extent.y + abs(axisZ) * extent.z;

    worldMin = worldCenter - worldExtent;
    worldMax = worldCenter + worldExtent;
}

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

        for (uint i = 0; i < group.numEntities; i++)
            denseToPrimitiveIndex[group.entityOffset + i] = idx;
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
