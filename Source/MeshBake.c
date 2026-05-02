#include "Include/AssetManager.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/Algorithm.h"
#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Math/Bitpack.h"
#include "Extern/meshoptimizer/src/meshoptimizer.h"

extern Graphics gGFX;

static void JointsForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    const u8* joints = (const u8*)primitive->vertexAttribs[AAttribIdx_JOINTS];
    s32 jointSize    = GraphicsTypeToSize(primitive->jointType);
    s32 jointOffset  = Maxi32((s32)(primitive->jointStride - (jointSize * primitive->jointCount)), 0);

    if (joints == NULL)
    {
        AX_LOG("no joints in skinned mesh renderer");
        for (s32 j = 0; j < primitive->numVertices; j++)
            currVertex[j].joints = 0;
        return;
    }

    for (s32 j = 0; j < primitive->numVertices; j++)
    {
        u32 packedJoints = 0u;
        for (s32 k = 0, shift = 0; k < primitive->jointCount; k++)
        {
            u32 jointIndex = 0;
            SmallMemCpy(&jointIndex, joints, jointSize);
            ASSERT(jointIndex < 255u && "index has to be smaller than 255");
            packedJoints |= jointIndex << shift;
            shift  += 8;
            joints += jointSize;
        }
        currVertex[j].joints = packedJoints;
        joints += jointOffset;
    }
}

static void WeightsForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    const u8* weights = (const u8*)primitive->vertexAttribs[AAttribIdx_WEIGHTS];
    s32 weightSize   = GraphicsTypeToSize(primitive->weightType);
    s32 weightOffset = Maxi32((s32)(primitive->weightStride - (weightSize * primitive->jointCount)), 0);

    if (weights == NULL)
    {
        AX_LOG("no joints in primitive");
        for (s32 j = 0; j < primitive->numVertices; j++)
            currVertex[j].weights = 1023;
        return;
    }

    if (weightSize == 4)
    {
        for (s32 j = 0; j < primitive->numVertices; j++)
        {
            u32 packedWeights = PackXY11Z10UnormToU32(Vec3Load((f32*)weights));
            if (packedWeights == 0) packedWeights = 1023;
            currVertex[j].weights = packedWeights;
            weights += sizeof(v128f) + weightOffset;
        }
    }
    else
    {
        for (s32 j = 0; j < primitive->numVertices; j++)
        {
            u32 packedWeights = 0;
            const f32 packMax[3] = { 1023.0f, 1023.0f, 511.0f };
            for (s32 k = 0, shift = 0; k < primitive->jointCount && k < 3; k++, shift += 11)
            {
                u32 jointWeight = 0u;
                SmallMemCpy(&jointWeight, weights, weightSize);
                f32 weightMax = (f32)((1u << (weightSize * 8)) - 1);
                f32 norm = (f32)jointWeight / weightMax;
                packedWeights |= (u32)(norm * packMax[k]) << shift;
                weights += weightSize;
            }
            if (packedWeights == 0) packedWeights = 0XFF000000u;
            currVertex[j].weights = packedWeights;
            weights += weightOffset;
        }
    }
}

static void IndicesForPrimitive(APrimitive* primitive, u32* currIndices, const u32 vertexCursor)
{
    if (primitive->indices == NULL)
    {
        primitive->indices = currIndices;
        s32* indices = (s32*)primitive->indices;
        for (s32 i = 0; i < primitive->numIndices; i++)
            indices[i] = i;
        return;
    }

    const u8* beforeCopy = (const u8*)primitive->indices;
    primitive->indices = currIndices;
    s32 indexSize = GraphicsTypeToSize(primitive->indexType);

    for (s32 i = 0; i < primitive->numIndices; i++)
    {
        u32 index = 0;
        SmallMemCpy(&index, beforeCopy, indexSize);
        currIndices[i] = index + vertexCursor;
        beforeCopy += indexSize;
    }
}

static void VerticesForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    primitive->vertices = currVertex;
    const fv3* positions   = (const fv3*)primitive->vertexAttribs[AAttribIdx_POSITION];
    const fv2* texCoords   = (const fv2*)primitive->vertexAttribs[AAttribIdx_TEXCOORD_0];
    const fv3* normals     = (const fv3*)primitive->vertexAttribs[AAttribIdx_NORMAL];
    const v128f* tangents  = (const v128f*)primitive->vertexAttribs[AAttribIdx_TANGENT];

    for (s32 v = 0; v < primitive->numVertices; v++)
    {
        v128f tangent = tangents  ? tangents[v]  : VecZero();
        fv2 texCoord  = texCoords ? texCoords[v] : (fv2){0.0f, 0.0f};
        fv3 normal    = normals   ? normals[v]   : (fv3){0.5f, 0.5f, 0.0};

        Float4ToHalf4((f16*)&currVertex[v].positionXY, &positions[v].x);
        currVertex[v].texCoord = Float2ToHalf2(&texCoord.x);
        currVertex[v].octTbn = PackNormalTangent(Vec3Load(&normal.x), tangent);
    }
}

static void BoundsForPrimitive(APrimitive* primitive)
{
    const fv3* positions = (const fv3*)primitive->vertexAttribs[AAttribIdx_POSITION];
    v128f min = VecSet1(FLT_MAX);
    v128f max = VecNeg(min);
    for (s32 i = 0; i < primitive->numVertices; i++)
    {
        v128f v = VecLoad(&positions[i].x);
        min = VecMin(min, v);
        max = VecMax(max, v);
    }
    VecStore(primitive->min, min);
    VecStore(primitive->max, max);
}

s32 BakeSceneMeshesAndAnimations(SceneBundle* gltf)
{
    AX_LOG("mesh bake: meshes=%d vertices=%d indices=%d skins=%d", gltf->numMeshes, gltf->totalVertices, gltf->totalIndices, gltf->numSkins);
    AMesh* meshes    = gltf->meshes;
    u32 vertexCursor = gGFX.NumVertices;
    u32 indexCursor  = gGFX.NumIndices;

    if ((gGFX.NumVertices + gltf->totalVertices) > MAX_VERTEX ||
        (gGFX.NumIndices  + gltf->totalIndices ) > MAX_INDEX)
    {
        AX_WARN("mesh bake failed: vertex/index buffer capacity exceeded vertices=%d/%d indices=%d/%d",
                gGFX.NumVertices + gltf->totalVertices, MAX_VERTEX,
                gGFX.NumIndices + gltf->totalIndices, MAX_INDEX);
        return 0;
    }

    gGFX.NumVertices += gltf->totalVertices;
    gGFX.NumIndices  += gltf->totalIndices;

    gltf->allVertices = gGFX.VertexBuffer + vertexCursor;
    gltf->allIndices  = gGFX.IndexBuffer  + indexCursor;

    ASkinedVertex* currVertex = (ASkinedVertex*)gltf->allVertices;
    u32* currIndices = (u32*)gltf->allIndices;

    for (s32 m = 0; m < gltf->numMeshes; ++m)
    {
        AMesh mesh = meshes[m];
        for (s32 p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];
            if (primitive->numVertices <= 0 || primitive->numIndices <= 0)
                AX_WARN("mesh %d primitive %d has empty geometry vertices=%d indices=%d", m, p, primitive->numVertices, primitive->numIndices);

            IndicesForPrimitive(primitive, currIndices, vertexCursor);
            VerticesForPrimitive(primitive, currVertex);
            JointsForPrimitive(primitive, currVertex);
            WeightsForPrimitive(primitive, currVertex);
            BoundsForPrimitive(primitive);

            primitive->indexOffset = indexCursor;
            currVertex   += primitive->numVertices;
            currIndices  += primitive->numIndices;
            vertexCursor += primitive->numVertices;
            indexCursor  += primitive->numIndices;
        }
    }

    BakeGLTFAnimations(gltf);
    FreeGLTFBuffers(gltf);
    AX_LOG("mesh bake complete: vertices=%d indices=%d", gltf->totalVertices, gltf->totalIndices);
    return 1;
}

void GenerateLOD_50_GLTF(SceneBundle* sceneBundle)
{
    for (s32 m = 0; m < sceneBundle->numMeshes; m++)
    {
        AMesh mesh = sceneBundle->meshes[m];
        for (s32 p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive primitive = mesh.primitives[p];
            size_t numIndices = (size_t)primitive.numIndices;
            int* indicesLod0 = ArenaAllocGlobal(numIndices * sizeof(s32));
            f32 resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, (const u32*)primitive.indices, numIndices,
                                                          (const f32*)sceneBundle->allVertices, (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex), NULL, numIndices - (numIndices >> 1), 0.04f, &resultError);
            primitive.numIndicesLOD50 = numSimplified;
            MemCopy(primitive.lodIndices50, indicesLod0, numSimplified * sizeof(u32));
            ArenaPopGlobal(numSimplified * sizeof(s32));
        }
    }
}

void GenerateLOD_75_GLTF(SceneBundle* sceneBundle)
{
    for (s32 m = 0; m < sceneBundle->numMeshes; m++)
    {
        AMesh mesh = sceneBundle->meshes[m];
        for (s32 p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive primitive = mesh.primitives[p];
            size_t numIndices = (size_t)primitive.numIndices;
            int* indicesLod0 = ArenaAllocGlobal(numIndices * sizeof(s32));
            f32 resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, (const u32*)primitive.indices, numIndices,
                                                          (const f32*)sceneBundle->allVertices, (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex), NULL, numIndices - (numIndices >> 2), 0.04f, &resultError);
            primitive.numIndicesLOD75 = numSimplified;
            MemCopy(primitive.lodIndices75, indicesLod0, numSimplified * sizeof(u32));
            ArenaPopGlobal(numSimplified * sizeof(s32));
        }
    }
}

void OptimizeMesh(const SceneBundle* gltf)
{
    int* remap = ArenaAllocGlobal(gltf->totalIndices * sizeof(s32));
    size_t totalVertices = meshopt_generateVertexRemap(remap, (const u32 *)gltf->allIndices,
                                                       (size_t)gltf->totalIndices, gltf->allVertices,
                                                       (size_t)gltf->totalVertices, sizeof(ASkinedVertex));

    int* temp = ArenaAllocGlobal(gltf->totalIndices * sizeof(s32));
    meshopt_remapIndexBuffer(temp, gltf->allIndices, (size_t)gltf->totalIndices, remap);

    ASkinedVertex* vertexBufferNew = ArenaAllocGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    meshopt_remapVertexBuffer(vertexBufferNew, gltf->allVertices, (size_t)gltf->totalVertices, sizeof(ASkinedVertex), remap);

    MemSet(gltf->allVertices, 0, (size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    MemSet(gltf->allIndices , 0, (size_t)gltf->totalIndices * sizeof(s32));

    MemCopy(gltf->allIndices , temp, (size_t)gltf->totalIndices * sizeof(s32));
    MemCopy(gltf->allVertices, vertexBufferNew, (size_t)totalVertices * sizeof(ASkinedVertex));

    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(s32));
    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(s32));
    ArenaPopGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex));

    meshopt_optimizeVertexCache(gltf->allIndices , gltf->allIndices, gltf->totalIndices, (size_t)totalVertices);
    meshopt_optimizeVertexFetch(gltf->allVertices, gltf->allIndices, gltf->totalIndices,
                                gltf->allVertices, (size_t)totalVertices, sizeof(ASkinedVertex));
}
