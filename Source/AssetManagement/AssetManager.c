/*********************************************************************************
*    Purpose:                                                                    *
*         Saves or Loads given FBX or GLTF scene, as binary                      *
*         Compresses Vertices for GPU using half precison and xyz10w2 format.    *
*         Uses zstd to reduce size on disk                                       *
*    Author:                                                                     *
*        Anilcan Gulkaya 2024 anilcangulkaya7@gmail.com github @benanil          *
*********************************************************************************/

#include "Include/AssetManager.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/Algorithm.h"
#include "Include/Animation.h" // maxBonePoses
#include "Include/Memory.h"
#include "Include/ImageBake.h"

#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Math/Bitpack.h"

#include "Include/FileSystem.h"
#include "Include/Common.h"

extern Graphics gGFX;

// FBX.c
extern s32 LoadFBX(const char* path, SceneBundle* fbxScene, f32 scale);

u8 IsMeshPath(const char* path) {
    int pathLen = StringLength(path);
    return FileHasExtension(path, pathLen, ".fbx") || FileHasExtension(path, pathLen, ".gltf") ||  
        FileHasExtension(path, pathLen, ".obj") || FileHasExtension(path, pathLen, ".glb") || FileHasExtension(path, pathLen, ".abm");
}

s32 LoadOBJ(const char* path, SceneBundle* objScene, f32 scale)
{
    return LoadFBX(path, objScene, scale);
}
// Parse a source mesh file into the intermediate SceneBundle, dispatching by extension.
// FBX/OBJ import is editor-only (ufbx is excluded from game builds); shipped assets are pre-baked .abm.
s32 ImportBundle(const char* path, SceneBundle* scene, f32 scale)
{
    s32 pathLen = StringLength(path);
    if (FileHasExtension(path, pathLen, ".obj") || 
        FileHasExtension(path, pathLen, ".fbx"))
        return LoadFBX(path, scene, scale);
    else 
        return ParseGLTF(path, scene, scale);
}

// mesh data only. Writes the .bdc image cache synchronously, but on an .abm cache miss it sets
// *outBaked so the caller persists the .abm mesh cache asynchronously (that's the slow part).
// staging images are the caller's concern. out: 0 on failure
s32 LoadBundleMeshCached(const char* path, SceneBundle* bundle, void** outVertexHeapPtr, void** outIndexHeapPtr, bool* outBaked)
{
    *outBaked = false;
    char buffer[1024];
    int pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    if (IsABMLastVersion(buffer))
    {
        AX_LOG("asset cache hit: %s", buffer);
        return LoadSceneBundleBinary(buffer, bundle, outVertexHeapPtr, outIndexHeapPtr);
    }
    // Import and bake do all their temporary work through ArenaPushGlobal. When this runs on an
    // async worker thread (editor mesh import), sharing the main thread's GlobalArena would corrupt
    // its LIFO bump pointer, so give this bake its own scratch arena for the duration. It's a small
    // bump buffer (ARENA_SCRATCH_SIZE); allocations larger than it spill to the thread-safe TLSF heap.
    ArenaScratch bakeArena;
    if (!ArenaBeginScratch(&bakeArena, ARENA_SCRATCH_SIZE, "LoadBundleMeshCached"))
    {
        AX_WARN("asset import failed: scratch arena allocation failed: %s", path);
        return 0;
    }

    if (!ImportBundle(path, bundle, 1.0f))
    {
        AX_WARN("asset import failed: %s", path);
        ArenaEndScratch(&bakeArena);
        return 0;
    }
    AX_LOG("asset cache rebuild: %s -> %s", path, buffer);
    if (!BakeSceneMeshesAndAnimations(bundle, outVertexHeapPtr, outIndexHeapPtr))
    {
        AX_WARN("asset import failed during mesh bake: %s vertices=%d indices=%d", path, bundle->totalVertices, bundle->totalIndices);
        ArenaEndScratch(&bakeArena);
        return 0;
    }
    ArenaEndScratch(&bakeArena);
    ChangeExtension(buffer, newLen, "bdc");
    if (!FileExist(buffer))
    {
        ChangeExtension(buffer, newLen, "glb");
        bool deleteRemaining = FileExist(buffer);
        ChangeExtension(buffer, newLen, "bdc");
        SaveSceneImages(bundle, buffer, deleteRemaining);
    }
    *outBaked = true;
    return 1;
}
/*//////////////////////////////////////////////////////////////////////////*/
/*                            Binary Save                                   */
/*//////////////////////////////////////////////////////////////////////////*/

// ZSTD_CCtx* zstdCompressorCTX = NULL;
// 79: geometry offsets and index values are stored bundle relative, the
// runtime placement comes from the mega buffer range allocators
// 80: surface (static) AVertex.position quantized to xyz unorm16 vs the primitive AABB
// 81: skinned animation uses lod vertex offsets directly
// 82: unindexed primitives store mega-buffer-relative generated indices
const s32 ABMMeshVersion = 84;

u8 IsABMLastVersion(const char* path)
{
    if (!FileExist(path))
    {
        AX_LOG("abm cache miss: %s does not exist", path);
        return false;
    }

    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (AFileSize(file) < sizeof(s32) + sizeof(u64))
    {
        AX_WARN("abm cache invalid: %s is too small", path);
        AFileClose(file);
        return false;
    }

    s32 version = 0;
    AFileRead(&version, sizeof(s32), file, 1);
    u64 hex;
    AFileRead(&hex, sizeof(u64), file, 1);
    AFileClose(file);
    if (hex != 0xABFABF)
        AX_WARN("abm cache invalid: %s has wrong magic", path);
    else if (version != ABMMeshVersion)
        AX_LOG("abm cache stale: %s version=%d expected=%d", path, version, ABMMeshVersion);
    return version == ABMMeshVersion && hex == 0xABFABF;
}

static void WriteAMaterialTexture(GLTFTexture texture, AFile file)
{
    u64 data = texture.scale; data <<= sizeof(uint16_t) * 8;
    data |= texture.strength; data <<= sizeof(uint16_t) * 8;
    data |= texture.index;    data <<= sizeof(uint16_t) * 8;
    data |= texture.texCoord;
    
    AFileWrite(&data, sizeof(u64), file, 1);
}

static void WriteGLTFString(const char* str, AFile file)
{
    if (str == NULL || str == (char*)0xCDCDCDCDCDCDCDCDull)
    {
        s32 nameLen = sizeof("no_name_mat");
        AFileWrite(&nameLen, sizeof(s32), file, 1);
        AFileWrite("no_name_mat", (uint64_t)(nameLen + 1), file, 1);
        return;
    }
    s32 nameLen = str ? StringLength(str) : 0;
    AFileWrite(&nameLen, sizeof(s32), file, 1);
    AFileWrite(str, (uint64_t)(nameLen + 1), file, 1);
}

s32 SaveGLTFBinary(const SceneBundle* gltf, const char* path)
{
#if !AX_GAME_BUILD
    AX_LOG("saving abm: %s version=%d meshes=%d nodes=%d skins=%d animations=%d",
           path, ABMMeshVersion, gltf->numMeshes, gltf->numNodes, gltf->numSkins, gltf->numAnimations);
    if (gltf->allVertices == NULL || gltf->allIndices == NULL || gltf->totalVertices <= 0 || gltf->totalIndices <= 0)
    {
        AX_WARN("abm save skipped: scene is not baked path=%s vertices=%d indices=%d", path, gltf->totalVertices, gltf->totalIndices);
        return 0;
    }
    EnsurePath(path);
    AFile file = AFileOpen(path, AOpenFlag_WriteBinary);
    s32 version = ABMMeshVersion;
    AFileWrite(&version, sizeof(s32), file, 1);
    
    u64 reserved[4] = { 0xABFABF };
    AFileWrite(&reserved, sizeof(u64) * 4, file, 1);
    
    AFileWrite(&gltf->scale, sizeof(float), file, 1);
    AFileWrite(&gltf->numMeshes, sizeof(u16), file, 1);
    AFileWrite(&gltf->numNodes, sizeof(u16), file, 1);
    AFileWrite(&gltf->numMaterials,  sizeof(u16), file, 1);
    AFileWrite(&gltf->numTextures, sizeof(u16), file, 1);
    AFileWrite(&gltf->numImages, sizeof(u16), file, 1);
    AFileWrite(&gltf->numSamplers, sizeof(u16), file, 1);
    AFileWrite(&gltf->numCameras, sizeof(u16), file, 1);
    AFileWrite(&gltf->numScenes, sizeof(u16), file, 1);
    AFileWrite(&gltf->numSkins, sizeof(u16), file, 1);
    AFileWrite(&gltf->numAnimations, sizeof(u16), file, 1);
    AFileWrite(&gltf->defaultSceneIndex, sizeof(u16), file, 1);
    u16 isSkined = (u16)(gltf->numSkins > 0);
    AFileWrite(&isSkined, sizeof(u16), file, 1);
    
    AFileWrite(&gltf->totalIndices, sizeof(s32), file, 1);
    AFileWrite(&gltf->totalVertices, sizeof(s32), file, 1);
    
    u64 vertexSize    = isSkined ? sizeof(ASkinedVertex) : sizeof(AVertex);
    u64 allVertexSize = vertexSize * (u64)gltf->totalVertices;
    u64 allIndexSize  = (u64)gltf->totalIndices * sizeof(u32);

    // offsets and index values are saved relative to the bundle's placement so
    // the cache can be loaded into any allocated range
    u32 bakedVertexBase = isSkined ? (u32)((const ASkinedVertex*)gltf->allVertices - gGFX.SkinnedVertexBuffer)
                                   : (u32)((const AVertex*)gltf->allVertices - gGFX.SurfaceVertexBuffer);
    u32 bakedIndexBase  = (u32)((const u32*)gltf->allIndices - gGFX.IndexBuffer);

    u64 writeSize = vertexSize * gltf->totalVertices;
    AFileWrite(&writeSize, sizeof(u64), file, 1);
    AFileWrite(gltf->allVertices, writeSize, file, 1);

    const u32* indices = (const u32*)gltf->allIndices;
    u32* deltaIndices = (u32*)AllocTLSF(sizeof(u32) * gltf->totalIndices);
    for (u32 i = 0; i < gltf->totalIndices; i++)
    {
        ASSERT(indices[i] >= bakedVertexBase);
        deltaIndices[i] = indices[i] - bakedVertexBase;
        ASSERT(deltaIndices[i] < gltf->totalVertices);
    }
    
    writeSize = sizeof(u32) * gltf->totalIndices;
    AFileWrite(&writeSize, sizeof(u64), file, 1);
    AFileWrite(deltaIndices, writeSize, file, 1);

    DeAllocTLSF(deltaIndices);
    // Cache stores runtime-ready mesh/animation data. Morph target animation is intentionally not serialized yet.
    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh mesh = gltf->meshes[i];
        WriteGLTFString(mesh.name, file);
        
        AFileWrite(&mesh.numPrimitives, sizeof(s32), file, 1);
        
        for (s32 j = 0; j < mesh.numPrimitives; j++)
        {
            APrimitive* primitive = &mesh.primitives[j];
            // all 4 lod slots rebase uniformly, slot 3 holds fallback values
            s32 relIndexOffset = primitive->indexOffset - (s32)bakedIndexBase;
            s32 relLodIndexOffset[4], relLodVertexOffset[4];
            for (s32 lod = 0; lod < 4; lod++)
            {
                relLodIndexOffset[lod]  = primitive->lodIndexOffset[lod] - (s32)bakedIndexBase;
                relLodVertexOffset[lod] = primitive->lodVertexOffset[lod] - (s32)bakedVertexBase;
            }

            AFileWrite(&primitive->attributes , sizeof(s32), file, 1);
            AFileWrite(&primitive->indexType  , sizeof(s32), file, 1);
            AFileWrite(&primitive->numIndices , sizeof(s32), file, 1);
            AFileWrite(&primitive->numVertices, sizeof(s32), file, 1);
            AFileWrite(&relIndexOffset, sizeof(s32), file, 1);
            AFileWrite(relLodIndexOffset, sizeof(s32) * 4, file, 1);
            AFileWrite(primitive->lodNumIndices, sizeof(s32) * 4, file, 1);
            AFileWrite(relLodVertexOffset, sizeof(s32) * 4, file, 1);
            AFileWrite(primitive->lodNumVertices, sizeof(s32) * 4, file, 1);
            AFileWrite(&primitive->jointType  , sizeof(u16), file, 1);
            AFileWrite(&primitive->jointCount , sizeof(u16), file, 1);
            AFileWrite(&primitive->jointStride, sizeof(u16), file, 1);
            AFileWrite(&primitive->material   , sizeof(u16), file, 1);
            AFileWrite(primitive->min, sizeof(v128f), file, 1);
            AFileWrite(primitive->max, sizeof(v128f), file, 1);
        }
    }
    
    for (s32 i = 0; i < gltf->numNodes; i++)
    {
        ANode* node = &gltf->nodes[i];
        AFileWrite(&node->type       , sizeof(s32), file, 1);
        AFileWrite(&node->index      , sizeof(s32), file, 1);
        AFileWrite(&node->translation, sizeof(float) * 3, file, 1);
        AFileWrite(&node->rotation   , sizeof(float) * 4, file, 1);
        AFileWrite(&node->scale      , sizeof(float) * 3, file, 1);
        AFileWrite(&node->numChildren, sizeof(s32), file, 1);
        AFileWrite(&node->parent     , sizeof(s32), file, 1);
        
        if (node->numChildren)
            AFileWrite(node->children, sizeof(s32) * node->numChildren, file, 1);
        
        WriteGLTFString(node->name, file);
    }
    
    for (s32 i = 0; i < gltf->numMaterials; i++)
    {
        AMaterial* material = &gltf->materials[i];
        for (s32 j = 0; j < 3; j++)
        {
            WriteAMaterialTexture(material->textures[j], file);
        }
        
        WriteAMaterialTexture(material->baseColorTexture, file);
        WriteAMaterialTexture(material->specularTexture, file);
        WriteAMaterialTexture(material->metallicRoughnessTexture, file);
        
        u64 data = material->emissiveFactor[0]; data <<= sizeof(u16) * 8;
        data |= material->emissiveFactor[1];         data <<= sizeof(u16) * 8;
        data |= material->emissiveFactor[2];         data <<= sizeof(u16) * 8;
        data |= material->specularFactor;
        AFileWrite(&data, sizeof(u64), file, 1);
        
        data = ((u64)(material->diffuseColor) << 32) | material->specularColor;
        AFileWrite(&data, sizeof(u64), file, 1);
        
        data = ((u64)(material->baseColorFactor) << 32) | (u64)material->doubleSided;
        AFileWrite(&data, sizeof(u64), file, 1);

        u32 packedFactors = ((u32)material->metallicFactor << 16u) | (u32)material->roughnessFactor;
        AFileWrite(&packedFactors, sizeof(u32), file, 1);
        
        AFileWrite(&material->alphaCutoff, sizeof(float), file, 1);
        AFileWrite(&material->alphaMode, sizeof(s32), file, 1);
        
        WriteGLTFString(material->name, file);
    }
    
    for (s32 i = 0; i < gltf->numTextures; i++)
    {
        ATexture texture = gltf->textures[i];
        AFileWrite(&texture.sampler, sizeof(s32), file, 1);
        AFileWrite(&texture.source, sizeof(s32), file, 1);
        WriteGLTFString(texture.name , file);
    }
    
    for (s32 i = 0; i < gltf->numImages; i++)
    {
        WriteGLTFString(gltf->images[i].path, file);
    }
    
    for (s32 i = 0; i < gltf->numSamplers; i++)
    {
        AFileWrite(&gltf->samplers[i], sizeof(ASampler), file, 1);
    }
    
    for (s32 i = 0; i < gltf->numCameras; i++)
    {
        ACamera camera = gltf->cameras[i];
        AFileWrite(&camera.aspectRatio, sizeof(float), file, 1);
        AFileWrite(&camera.yFov, sizeof(float), file, 1);
        AFileWrite(&camera.zFar, sizeof(float), file, 1);
        AFileWrite(&camera.zNear, sizeof(float), file, 1);
        AFileWrite(&camera.type, sizeof(s32), file, 1);
        WriteGLTFString(camera.name , file);
    }
    for (s32 i = 0; i < gltf->numScenes; i++)
    {
        AScene scene = gltf->scenes[i];
        WriteGLTFString(scene.name, file);
        AFileWrite(&scene.numNodes, sizeof(s32), file, 1);
        AFileWrite(scene.nodes, sizeof(s32) * scene.numNodes, file, 1);
    }
    
    for (s32 i = 0; i < gltf->numSkins; i++)
    {
        ASkin skin = gltf->skins[i];
        AFileWrite(&skin.skeleton, sizeof(s32), file, 1);
        AFileWrite(&skin.numJoints, sizeof(s32), file, 1);
        AFileWrite(skin.inverseBindMatrices, sizeof(mat4x4) * skin.numJoints, file, 1);
        AFileWrite(skin.joints, sizeof(s32) * skin.numJoints, file, 1);
        AFileWrite(skin.nodeToJoint, Mins32(gltf->numNodes, 96) * sizeof(u16), file, 1);
    }
    
    s32 totalAnimSamplerInput = 0;
    if (gltf->numAnimations > 0)
    {
        for (s32 a = 0; a < gltf->numAnimations; a++)
            for (s32 s = 0; s < gltf->animations[a].numSamplers; s++)
                totalAnimSamplerInput += gltf->animations[a].samplers[s].count;
    }

    AFileWrite(&totalAnimSamplerInput, sizeof(s32), file, 1);
    if (totalAnimSamplerInput > 0) {
        // all sampler input and outputs are allocated in one buffer each. at the end of the CreateVerticesIndicesSkined function
        AFileWrite(gltf->animations[0].samplers[0].input, sizeof(float) * totalAnimSamplerInput, file, 1);
        AFileWrite(gltf->animations[0].samplers[0].output, sizeof(v128f) * totalAnimSamplerInput, file, 1);
    }

    for (s32 i = 0; i < gltf->numAnimations; i++)
    {
        AAnimation animation = gltf->animations[i];
        AFileWrite(&animation.numSamplers, sizeof(s32), file, 1);
        AFileWrite(&animation.numChannels, sizeof(s32), file, 1);
        AFileWrite(&animation.duration, sizeof(float), file, 1);
        AFileWrite(&animation.speed, sizeof(float), file, 1);
        WriteGLTFString(animation.name, file);

        AFileWrite(animation.channels, sizeof(AAnimChannel) * animation.numChannels, file, 1);
        
        for (s32 j = 0; j < animation.numSamplers; j++)
        {
            AFileWrite(&animation.samplers[j].count, sizeof(s32), file, 1);
            AFileWrite(&animation.samplers[j].numComponent, sizeof(s32), file, 1);
            AFileWrite(&animation.samplers[j].inputType, sizeof(AComponentType), file, 1);
            AFileWrite(&animation.samplers[j].outputType, sizeof(AComponentType), file, 1);
            AFileWrite(&animation.samplers[j].interpolation, sizeof(ASamplerInterpolation), file, 1);
        }
    }
    
    AFileWrite(&gltf->rootNode, sizeof(int), file, 1);

    AFileClose(file);
    AX_LOG("abm save complete: %s vertices=%d indices=%d", path, gltf->totalVertices, gltf->totalIndices);
#endif
    return 1;
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                            Binary Read                                   */
/*//////////////////////////////////////////////////////////////////////////*/

void ReadAMaterialTexture(GLTFTexture* texture, AFile file)
{
    u64 data;
    AFileRead(&data, sizeof(u64), file, 1);
    
    texture->texCoord = data & 0xFFFFu; data >>= sizeof(u16) * 8;
    texture->index    = data & 0xFFFFu; data >>= sizeof(u16) * 8;
    texture->strength = data & 0xFFFFu; data >>= sizeof(u16) * 8;
    texture->scale    = data & 0xFFFFu;
}

void ReadGLTFString(char** str, AFile file, Pow2Allocator* stringAllocator)
{
    s32 nameLen = 0;
    AFileRead(&nameLen, sizeof(s32), file, 1);
    if (nameLen)    
    {
        *str = Pow2Alloc_Uninitialized(stringAllocator, nameLen + 1);
        AFileRead(*str, nameLen + 1, file, 1);
        (*str)[nameLen] = 0;
    }
}

s32 LoadSceneBundleBinary(const char* path, SceneBundle* gltf, void** outVertexHeapPtr, void** outIndexHeapPtr)
{
    AX_LOG("abm load: %s", path);
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
    {
        AX_WARN("Failed to open file for reading: %s", path);
        return 0;
    }
    
    MemsetZero(gltf, sizeof(SceneBundle));
    Pow2Allocator* allocator = AllocTLSF(sizeof(Pow2Allocator));
    Pow2Alloc_Init(allocator, 1024);

    s32 version = 0;
    AFileRead(&version, sizeof(s32), file, 1);
    if (version != ABMMeshVersion)
        AX_WARN("abm version mismatch: %s version=%d expected=%d", path, version, ABMMeshVersion);
    ASSERT(version == ABMMeshVersion);
    
    u64 reserved[4];
    AFileRead(&reserved, sizeof(u64) * 4, file, 1);
    
    AFileRead(&gltf->scale            , sizeof(float), file, 1);
    AFileRead(&gltf->numMeshes        , sizeof(u16), file, 1);
    AFileRead(&gltf->numNodes         , sizeof(u16), file, 1);
    AFileRead(&gltf->numMaterials     , sizeof(u16), file, 1);
    AFileRead(&gltf->numTextures      , sizeof(u16), file, 1);
    AFileRead(&gltf->numImages        , sizeof(u16), file, 1);
    AFileRead(&gltf->numSamplers      , sizeof(u16), file, 1);
    AFileRead(&gltf->numCameras       , sizeof(u16), file, 1);
    AFileRead(&gltf->numScenes        , sizeof(u16), file, 1);
    AFileRead(&gltf->numSkins         , sizeof(u16), file, 1);
    AFileRead(&gltf->numAnimations    , sizeof(u16), file, 1);
    AFileRead(&gltf->defaultSceneIndex, sizeof(u16), file, 1);
    u16 isSkined;
    AFileRead(&isSkined, sizeof(u16), file, 1);
    
    AFileRead(&gltf->totalIndices, sizeof(s32), file, 1);
    AFileRead(&gltf->totalVertices, sizeof(s32), file, 1);
    if (gltf->totalIndices <= 0 || gltf->totalVertices <= 0)
    {
        AX_WARN("abm load failed: invalid geometry counts %s vertices=%d indices=%d", path, gltf->totalVertices, gltf->totalIndices);
        AFileClose(file);
        return 0;
    }
     
    size_t vertexSize = isSkined ? sizeof(ASkinedVertex) : sizeof(AVertex);
    size_t vertexAlignment = 4;

    GeometryBufferKind vertexKind = isSkined ? GeometryBuffer_SkinnedVertex : GeometryBuffer_SurfaceVertex;
    u32 vertexBase = GEOMETRY_ALLOC_FAIL;
    u32 indexBase  = GEOMETRY_ALLOC_FAIL;

    {
        // +1 vertex / +4 index padding, zsinflate may write a few bytes past the
        // exact size and the next range can belong to another live bundle
        void* vertexRaw = NULL;
        void* indexRaw  = NULL;
        vertexBase = GeometryHeapAlloc(vertexKind, (u32)gltf->totalVertices + 1u, &vertexRaw);
        indexBase  = GeometryHeapAlloc(GeometryBuffer_Index, (u32)gltf->totalIndices + 4u, &indexRaw);
        if (vertexBase == GEOMETRY_ALLOC_FAIL || indexBase == GEOMETRY_ALLOC_FAIL)
        {
            AX_ERROR("VERTEX buffer space is not enough for %s", path);
            GeometryHeapFree(vertexKind, vertexRaw);
            GeometryHeapFree(GeometryBuffer_Index, indexRaw);
            return 0;
        }
        if (outVertexHeapPtr) *outVertexHeapPtr = vertexRaw;
        if (outIndexHeapPtr)  *outIndexHeapPtr = indexRaw;

        if (isSkined) gGFX.NumSkinnedVertices += gltf->totalVertices;
        else          gGFX.NumSurfaceVertices += gltf->totalVertices;
        gGFX.NumIndices += gltf->totalIndices;

        gltf->allVertices = isSkined ? (void*)(gGFX.SkinnedVertexBuffer + vertexBase) : (void*)(gGFX.SurfaceVertexBuffer + vertexBase);
        gltf->allIndices  = gGFX.IndexBuffer + indexBase;

        u64 allVertexSize = gltf->totalVertices * vertexSize;
        u64 allIndexSize  = gltf->totalIndices * sizeof(u32);

        u64 compressedSize;
        AFileRead(&compressedSize, sizeof(u64), file, 1);
        AFileRead(gltf->allVertices, compressedSize, file, 1);

        AFileRead(&compressedSize, sizeof(u64), file, 1);
        AFileRead(gltf->allIndices, compressedSize, file, 1);

        u32* indices = (u32*)gltf->allIndices;
        u32 idDiff = 0;
        for (u32 i = 0; i < gltf->totalIndices; i++)
        {
            if (indices[i] >= gltf->totalVertices) 
                idDiff += indices[i] - gltf->totalVertices;
            indices[i] += vertexBase;
        }
        if (idDiff) AX_ERROR("id diff: %d", idDiff);

        Rendering_QueueGeometryUpload(isSkined ? GeometryBuffer_SkinnedVertex : GeometryBuffer_SurfaceVertex,
                                      vertexBase, vertexBase + (u32)gltf->totalVertices);
        Rendering_QueueGeometryUpload(GeometryBuffer_Index, indexBase, indexBase + (u32)gltf->totalIndices);
    }
    
    char* currVertices = (char*)gltf->allVertices;
    char* currIndices = (char*)gltf->allIndices;
    
    if (gltf->numMeshes > 0) gltf->meshes = AllocZeroTLSF(gltf->numMeshes, sizeof(AMesh));
    s32 totalPrimitives = 0;
    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh* mesh = &gltf->meshes[i];
        ReadGLTFString(&mesh->name, file, allocator);
        
        AFileRead(&mesh->numPrimitives, sizeof(s32), file, 1);
        mesh->primitiveOffset = totalPrimitives;
        totalPrimitives += mesh->numPrimitives;

        mesh->primitives = Pow2Alloc_Uninitialized(allocator, sizeof(APrimitive) * mesh->numPrimitives);
        
        for (s32 j = 0; j < mesh->numPrimitives; j++)
        {
            APrimitive* primitive = &mesh->primitives[j];
            AFileRead(&primitive->attributes , sizeof(s32), file, 1);
            AFileRead(&primitive->indexType  , sizeof(s32), file, 1);
            AFileRead(&primitive->numIndices , sizeof(s32), file, 1);
            AFileRead(&primitive->numVertices, sizeof(s32), file, 1);
            AFileRead(&primitive->indexOffset, sizeof(s32), file, 1);
            AFileRead(primitive->lodIndexOffset, sizeof(s32) * 4, file, 1);
            AFileRead(primitive->lodNumIndices, sizeof(s32) * 4, file, 1);
            AFileRead(primitive->lodVertexOffset, sizeof(s32) * 4, file, 1);
            AFileRead(primitive->lodNumVertices, sizeof(s32) * 4, file, 1);

            // stored bundle relative, rebase onto the allocated ranges
            primitive->indexOffset += (s32)indexBase;
            for (s32 lod = 0; lod < 4; lod++)
            {
                primitive->lodIndexOffset[lod]  += (s32)indexBase;
                primitive->lodVertexOffset[lod] += (s32)vertexBase;
            }
            AFileRead(&primitive->jointType  , sizeof(u16), file, 1);
            AFileRead(&primitive->jointCount , sizeof(u16), file, 1);
            AFileRead(&primitive->jointStride, sizeof(u16), file, 1);
            u64 indexSize = (u64)(GraphicsTypeToSize(primitive->indexType)) * primitive->numIndices;
            primitive->indices = (void*)currIndices;
            currIndices += indexSize;
            
            u64 primitiveVertexSize = (u64)(primitive->numVertices) * vertexSize;
            primitive->vertices = currVertices;
            currVertices += primitiveVertexSize;
            AFileRead(&primitive->material, sizeof(u16), file, 1);
            primitive->hasOutline = false; // always false 

            AFileRead(primitive->min, sizeof(v128f), file, 1);
            AFileRead(primitive->max, sizeof(v128f), file, 1);
        }
    }
    gltf->totalPrimitives = totalPrimitives;
    
    if (gltf->numNodes > 0) gltf->nodes = AllocZeroTLSF(gltf->numNodes, sizeof(ANode));
    
    for (s32 i = 0; i < gltf->numNodes; i++)
    {
        ANode* node = &gltf->nodes[i];
        AFileRead(&node->type       , sizeof(s32), file, 1);
        AFileRead(&node->index      , sizeof(s32), file, 1);
        AFileRead(&node->translation, sizeof(float) * 3, file, 1);
        AFileRead(&node->rotation   , sizeof(float) * 4, file, 1);
        AFileRead(&node->scale      , sizeof(float) * 3, file, 1);
        AFileRead(&node->numChildren, sizeof(s32), file, 1);
        AFileRead(&node->parent     , sizeof(s32), file, 1);
        
        if (node->numChildren)
        {
            node->children = Pow2Alloc_Uninitialized(allocator, sizeof(s32) * (node->numChildren+1));
            AFileRead(node->children, sizeof(s32) * node->numChildren, file, 1);
        }
        
        ReadGLTFString(&node->name, file, allocator);
    }
    
    if (gltf->numMaterials > 0) gltf->materials = AllocZeroTLSF(gltf->numMaterials, sizeof(AMaterial));
    for (s32 i = 0; i < gltf->numMaterials; i++)
    {
        AMaterial* material = &gltf->materials[i];
        for (s32 j = 0; j < 3; j++)
        {
            ReadAMaterialTexture(&material->textures[j], file);
        }
        
        ReadAMaterialTexture(&material->baseColorTexture, file);
        ReadAMaterialTexture(&material->specularTexture, file);
        ReadAMaterialTexture(&material->metallicRoughnessTexture, file);
        
        u64 data;
        AFileRead(&data, sizeof(u64), file, 1);
        
        material->specularFactor    = data & 0xFFFF; data >>= sizeof(u16) * 8;
        material->emissiveFactor[2] = data & 0xFFFF; data >>= sizeof(u16) * 8;
        material->emissiveFactor[1] = data & 0xFFFF; data >>= sizeof(u16) * 8;
        material->emissiveFactor[0] = data & 0xFFFF; 
        
        AFileRead(&data, sizeof(u64), file, 1);
        material->diffuseColor  = (data >> 32);
        material->specularColor = data & 0xFFFFFFFF;
        
        AFileRead(&data, sizeof(u64), file, 1);
        material->baseColorFactor = (data >> 32);
        material->doubleSided     = data & 0x1;

        u32 packedFactors;
        AFileRead(&packedFactors, sizeof(u32), file, 1);
        material->metallicFactor  = (u16)(packedFactors >> 16u);
        material->roughnessFactor = (u16)(packedFactors & 0xFFFFu);
        
        AFileRead(&material->alphaCutoff, sizeof(float), file, 1);
        AFileRead(&material->alphaMode, sizeof(s32), file, 1);
        
        ReadGLTFString(&material->name, file, allocator);
    }
    
    if (gltf->numTextures > 0) gltf->textures = (ATexture*)AllocZeroTLSF(gltf->numTextures, sizeof(ATexture));
    for (s32 i = 0; i < gltf->numTextures; i++)
    {
        ATexture* texture = &gltf->textures[i];
        AFileRead(&texture->sampler, sizeof(s32), file, 1);
        AFileRead(&texture->source, sizeof(s32), file, 1);
        ReadGLTFString(&texture->name, file, allocator);
    }
    if (gltf->numImages > 0) gltf->images = (AImage*)AllocZeroTLSF(gltf->numImages, sizeof(AImage));
    for (s32 i = 0; i < gltf->numImages; i++)
    {
        ReadGLTFString(&gltf->images[i].path, file, allocator);
    }
    
    if (gltf->numSamplers > 0) gltf->samplers = (ASampler*)AllocZeroTLSF(gltf->numSamplers, sizeof(ASampler));
    for (s32 i = 0; i < gltf->numSamplers; i++)
    {
        AFileRead(&gltf->samplers[i], sizeof(ASampler), file, 1);
    }
    
    if (gltf->numCameras > 0) gltf->cameras = (ACamera*)AllocZeroTLSF(gltf->numCameras, sizeof(ACamera));
    for (s32 i = 0; i < gltf->numCameras; i++)
    {
        ACamera* camera = &gltf->cameras[i];
        AFileRead(&camera->aspectRatio, sizeof(float), file, 1);
        AFileRead(&camera->yFov, sizeof(float), file, 1);
        AFileRead(&camera->zFar, sizeof(float), file, 1);
        AFileRead(&camera->zNear, sizeof(float), file, 1);
        AFileRead(&camera->type, sizeof(s32), file, 1);
        ReadGLTFString(&camera->name, file, allocator);
    }
    
    if (gltf->numScenes > 0) gltf->scenes = (AScene*)AllocZeroTLSF(gltf->numScenes, sizeof(AScene));
    for (s32 i = 0; i < gltf->numScenes; i++)
    {
        AScene* scene = &gltf->scenes[i];
        ReadGLTFString(&scene->name, file, allocator);
        AFileRead(&scene->numNodes, sizeof(s32), file, 1);
        scene->nodes = Pow2Alloc_Uninitialized(allocator, scene->numNodes * sizeof(s32));
        AFileRead(scene->nodes, sizeof(s32) * scene->numNodes, file, 1);
    }

    if (gltf->numSkins > 0) gltf->skins = (ASkin*)AllocZeroTLSF(gltf->numSkins, sizeof(ASkin));
    for (s32 i = 0; i < gltf->numSkins; i++)
    {
        ASkin* skin = &gltf->skins[i];
        AFileRead(&skin->skeleton, sizeof(s32), file, 1);
        AFileRead(&skin->numJoints, sizeof(s32), file, 1);
        skin->inverseBindMatrices = (f32*)AllocTLSF(sizeof(mat4x4) * skin->numJoints);
        skin->joints = Pow2Alloc_Uninitialized(allocator, skin->numJoints * sizeof(s32));
        AFileRead(skin->inverseBindMatrices, sizeof(mat4x4) * skin->numJoints, file, 1);
        AFileRead(skin->joints, sizeof(s32) * skin->numJoints, file, 1);
        if (version > 83) AFileRead(skin->nodeToJoint, sizeof(u16) * gltf->numNodes, file, 1);
    }

    s32 totalAnimSamplerInput = 0;
    AFileRead(&totalAnimSamplerInput, sizeof(s32), file, 1);
    f32* currSamplerInput = NULL;
    v128f* currSamplerOutput = NULL;

    if (totalAnimSamplerInput) {
        currSamplerInput  = (f32*)AllocZeroTLSF(totalAnimSamplerInput, sizeof(float));
        currSamplerOutput = (v128f*)AllocZeroTLSF(totalAnimSamplerInput, sizeof(v128f));
        AFileRead(currSamplerInput, sizeof(float) * totalAnimSamplerInput, file, 1);
        AFileRead(currSamplerOutput, sizeof(v128f) * totalAnimSamplerInput, file, 1);
    }

    if (gltf->numAnimations) gltf->animations = AllocZeroTLSF(gltf->numAnimations, sizeof(AAnimation));
    for (s32 i = 0; i < gltf->numAnimations; i++)
    {
        AAnimation* animation = &gltf->animations[i];

        AFileRead(&animation->numSamplers, sizeof(s32), file, 1);
        AFileRead(&animation->numChannels, sizeof(s32), file, 1);
        AFileRead(&animation->duration, sizeof(float), file, 1);
        AFileRead(&animation->speed, sizeof(float), file, 1);
        ReadGLTFString(&animation->name, file, allocator);
        animation->channels = AllocTLSF(animation->numChannels * sizeof(AAnimChannel));
        AFileRead(animation->channels, sizeof(AAnimChannel) * animation->numChannels, file, 1);
        animation->samplers = AllocTLSF(animation->numSamplers * sizeof(AAnimSampler));

        for (s32 j = 0; j < animation->numSamplers; j++)
        {
            AFileRead(&animation->samplers[j].count, sizeof(s32), file, 1);
            AFileRead(&animation->samplers[j].numComponent, sizeof(s32), file, 1);
            AFileRead(&animation->samplers[j].inputType, sizeof(AComponentType), file, 1);
            AFileRead(&animation->samplers[j].outputType, sizeof(AComponentType), file, 1);
            AFileRead(&animation->samplers[j].interpolation, sizeof(ASamplerInterpolation), file, 1);
            s32 count = animation->samplers[j].count;
            animation->samplers[j].input = currSamplerInput;
            animation->samplers[j].output = (f32*)currSamplerOutput;
            currSamplerInput += count;
            currSamplerOutput += count;
        }
    }
    
    AFileRead(&gltf->rootNode, sizeof(int), file, 1);

    if (version == 83 && gltf->numSkins > 0)
    {
        MemsetZero(gltf->skins[0].nodeToJoint, sizeof(u16) * Mins32(gltf->numNodes, 96));
    }

    AFileClose(file);
    gltf->allocator = allocator;
    AX_LOG("abm load complete: %s meshes=%d nodes=%d skins=%d animations=%d",
           path, gltf->numMeshes, gltf->numNodes, gltf->numSkins, gltf->numAnimations);
    return 1;
}
