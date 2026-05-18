#ifndef COMMON_STRUCTS
#define COMMON_STRUCTS

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
    uint pageIndex;
    uint flags;
    float2 uvScale;
    float2 uvBias;
} TextureDescriptor;

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

typedef struct LineVertex_
{
    float x, y, z;
    uint color;
} LineVertex;

#endif
