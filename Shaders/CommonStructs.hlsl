#ifndef COMMON_STRUCTS
#define COMMON_STRUCTS

struct Entity
{
    float4 position;
    uint2  rotation;
    uint2  scaleSparse;
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
#endif