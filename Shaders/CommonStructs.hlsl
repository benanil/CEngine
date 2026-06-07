#ifndef COMMON_STRUCTS
#define COMMON_STRUCTS

#include "Common.hlsl"
#include "../Include/RenderLimits.h"

typedef struct IndexedDrawCommand_
{
    uint numIndices;
    uint numInstances;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
} IndexedDrawCommand;

typedef struct IndirectDrawCommand_
{
    uint numVertices;  
    uint numInstances; 
    uint firstVertex;  
    uint firstInstance;
} IndirectDrawCommand;

typedef struct IndirectDispatchCommand_
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
} IndirectDispatchCommand;

typedef struct Entity_
{
    float4 position;
    uint2  rotation;
    uint  scale;
    uint  sparse;
} Entity;

typedef struct PrimitiveGroup_
{
    uint entityOffset;
    uint numEntities;
    uint capacity;
    uint animatedVertexOffset;
    uint numIndices;
    uint indexOffset;
    uint vertexOffset;
    uint meshIndex;
    uint primitiveIndex;
    uint materialIndex;
    uint valid;
    uint numVertices;
    float4 aabbMin;
    float4 aabbMax;
    uint4 lodIndexOffset;
    uint4 lodNumIndices;
    uint4 lodVertexOffset;
    uint4 lodNumVertices;
    uint4 lodAnimatedVertexOffset;
} PrimitiveGroup;

// Animated vertex cache format, 8 bytes per vertex. 
// packed0/packed1: 39-bit signed fixed-point local position at 4095/500 = 0.002 unit precision
// max animation bounds is 4095 * 0.002 = 8.19mt
// plus 25-bit tangent space packed as 9/9 oct, 6-bit diamond tangent, 1-bit handedness.
typedef struct AnimatedVert_
{
    uint packed0;
    uint packed1;
} AnimatedVert;

typedef struct TextureDescriptor_
{
    float2 uvScale;
    float2 uvBias;
    uint pageIndex;
    uint flags;
    uint2 padding;
} TextureDescriptor;

#define MATERIAL_FLAG_ALPHA_MASK       (1u << 0)
#define MATERIAL_ALPHA_CUTOFF_SHIFT    8u
#define MATERIAL_ALPHA_CUTOFF_MASK     (0xffu << MATERIAL_ALPHA_CUTOFF_SHIFT)

bool MaterialIsAlphaMasked(uint flags)
{
    return (flags & MATERIAL_FLAG_ALPHA_MASK) != 0u;
}

float MaterialAlphaCutoff(uint flags)
{
    return float((flags & MATERIAL_ALPHA_CUTOFF_MASK) >> MATERIAL_ALPHA_CUTOFF_SHIFT) * (1.0f / 255.0f);
}

float MaterialBaseAlpha(uint baseColorFactor)
{
    return float(baseColorFactor >> 24u) * (1.0f / 255.0f);
}

typedef struct MaterialGPU_
{
    uint albedoDescriptor;
    uint normalDescriptor;
    uint metallicRoughnessDescriptor;
    uint flags;
    uint baseColorFactor;
    uint metallicRoughnessFactor;
    uint2 padding;
} MaterialGPU;

void AlphaClipMaterial(MaterialGPU material, float albedoAlpha)
{
    if (MaterialIsAlphaMasked(material.flags) && albedoAlpha * MaterialBaseAlpha(material.baseColorFactor) < MaterialAlphaCutoff(material.flags))
        discard;
}

typedef struct LineVertex_
{
    float x, y, z;
    uint color;
} LineVertex;

#define LIGHT_TYPE_POINT 0u
#define LIGHT_TYPE_SPOT  1u
#define LIGHT_TYPE_RECT  2u

#define LIGHT_FLAG_SHADOWED 1u
#define LIGHT_SHADOW_INDEX_INVALID 0xffffffffu

#define LIGHT_DRAW_FULLSCREEN 1u

typedef struct LightGPU_
{
    float4 positionRadius;
    float4 directionCone;
    float4 colorIntensity;
    uint type;
    uint flags;
    uint shadowIndex;
    uint padding;
} LightGPU;

typedef struct LightDrawInfo_
{
    float4 uvRect;
    uint lightIndex;
    uint flags;
    uint2 padding;
} LightDrawInfo;

#endif
