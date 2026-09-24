// .scene text save/load: line separated description of a scene's bundles, entities,
// lights and texture tables, with the texture pages dumped raw in the platform's gpu
// format (.ctex) so loading skips transcoding and packing entirely. basis baking of
// the pages moves to the game build step
#include "Include/SceneSerializer.h"
#include "Include/AssetManager.h"
#include "Include/FileSystem.h"
#include "Include/Algorithm.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Rendering.h"
#include "Include/Bitset.h"
#include "Include/ParallelFor.h"
#include "Math/Bitpack.h"

#define SCENE_FILE_VERSION 6

// first descriptors of a texture system are the built in defaults (TextureSystem.c)
enum { SceneSer_DefaultDescriptors = 4 };

static const char* const kAtlasSuffix[TextureClass_Count] = { "_albedo.ctex", "_normal.ctex", "_mr.ctex" };

/*//////////////////////////////////////////////////////////////////////////*/
/*                            Text Read / Write                             */
/*//////////////////////////////////////////////////////////////////////////*/


static v128f SceneUnpackLegacyScaleXY11Z10(u32 packed)
{
    v128u i = VeciSrl(VeciSet1(packed), VeciSetR(0, 11, 22, 31));
    i = VeciAnd(i, VeciSetR(0x7FF, 0x7FF, 0x3FF, 0));
    return VecMul(VecI32ToF32(i), VecSetR(1.0f / 2047.0f, 1.0f / 2047.0f, 1.0f / 1023.0f, 0.0f));
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                                  Save                                    */
/*//////////////////////////////////////////////////////////////////////////*/

// warning this has missing attributes, and last one is indexbuffer size
static void WritePrimBuffer(AFile file, const char* name, char* buffer, const APrimitive* p, void* data, size_t size)
{
    char line[64] = {};
    s32 nameLen = (u32)StringLength(name);
    MemCopy(line, name, nameLen);
    line[nameLen++] = ':';
    u64 base64NumBytes = EncodeBase64(buffer, (u8*)data, size);
    WEnd(file, line, WInt(line + nameLen, (s64)base64NumBytes));
    AFileWrite(buffer, base64NumBytes, file, 1);
    AFileWrite("\n", 1, file, 1);
}

static void ReadPrimitiveBuffer(AFile file, void* dst)
{
    char line[64] = {};
    if (!AFileReadLine(line, sizeof(line), file))
        return;
    s64 base64NumBytes;
    ParseNumberI64(line, &base64NumBytes);
    // AFileSeek(1, file);// skip newline
    char* encodedTmp = AllocTLSF((size_t)base64NumBytes + 1);
    AFileRead(encodedTmp, base64NumBytes, file, 1);
    encodedTmp[base64NumBytes] = '\0';
    // Decode directly into target buffer
    DecodeBase64((char*)dst, encodedTmp, base64NumBytes);
    DeAllocTLSF(encodedTmp);
    AFileReadLine(line, sizeof(line), file);
}

static void WriteProceduralBundle(const SceneBundleRef* ref, AFile file)
{
    const APrimitive* p = &ref->bundle->meshes[0].primitives[0];
    size_t maxRawBytes = Maxu64(sizeof(v128f) * p->numVertices, sizeof(u32) * p->numIndices);
    size_t maxBase64Bytes = 4 * ((maxRawBytes + 2) / 3) + 1;
    char* buffer = AllocTLSF(maxBase64Bytes);
    AFileWriteInt(file, (u64)p->numVertices, true);
    AFileWriteInt(file, (u64)p->numIndices , true);
    s32 pathLen = StringLength(ref->path);
    ref->path[pathLen] = '\n';
    AFileWrite(ref->path, pathLen + 1, file, 1);
    ref->path[pathLen] = '\0';
    WritePrimBuffer(file, "positions", buffer, p, p->Attributes[AAttribIdx_POSITION]  , sizeof(float3) * p->numVertices);
    WritePrimBuffer(file, "texcoords", buffer, p, p->Attributes[AAttribIdx_TEXCOORD_0], sizeof(float2) * p->numVertices);
    WritePrimBuffer(file, "normals"  , buffer, p, p->Attributes[AAttribIdx_NORMAL]    , sizeof(float3) * p->numVertices);
    WritePrimBuffer(file, "tangents" , buffer, p, p->Attributes[AAttribIdx_TANGENT]   , sizeof(v128f ) * p->numVertices);
    WritePrimBuffer(file, "indices"  , buffer, p, p->indices, sizeof(u32) * p->numIndices);
    DeAllocTLSF(buffer);
}

static SceneBundle* ReadProceduralBundle(AFile file, s32 version, char* nameBuffer)
{
    char line[64] = {};
    s32 numVertex, numIndex;

    if (!AFileReadLine(line, sizeof(line), file)) return NULL;
    ParsePositiveNumber(line, &numVertex);
    if (!AFileReadLine(line, sizeof(line), file)) return NULL;
    ParsePositiveNumber(line, &numIndex);
    MeshBuilder builder = MeshBuilder_Create(numVertex, numIndex);
    if (version >= 6)
    {
        s32 nameLen = AFileReadLine(nameBuffer, 1024, file);
        nameBuffer[nameLen] = '/0';
    }
    ReadPrimitiveBuffer(file, builder.positions);
    ReadPrimitiveBuffer(file, builder.texCoords);
    ReadPrimitiveBuffer(file, builder.normals);
    ReadPrimitiveBuffer(file, builder.tangents);
    ReadPrimitiveBuffer(file, builder.indices);
    return builder.bundle;
}

s32 SceneSerializer_Save(Scene* scene, const char* path)
{
    double startTime = TimeSinceStartup();
    TextureSystem* ts = &scene->textureSystem;

    // dump the page atlases raw in the gpu format, the text file references them by
    // name. empty scenes (only default descriptors) skip the dumps entirely
    char atlasPath[TextureClass_Count][1024];
    s32 atlasBaked[TextureClass_Count] = { 0, 0, 0 };

    if (ts->compressed && ts->numDescriptors > SceneSer_DefaultDescriptors)
    {
        for (u32 c = 0; c < TextureClass_Count; c++)
        {
            ChangeExtensionAndCopy(path, kAtlasSuffix[c], atlasPath[c], sizeof(atlasPath[c]));
            atlasBaked[c] = TextureSystem_SaveBakedClass(ts, c, atlasPath[c]);
        }
    }
    else if (!ts->compressed)
        AX_WARN("texture pages are not block compressed, scene saves without page dumps");

    // write the text to a tmp file, swap it in when complete
    char tmpPath[1024];
    int pathLen = StringLength(path);
    if (pathLen + 5 > (int)sizeof(tmpPath)) return 0;
    MemCopy(tmpPath, path, pathLen);
    MemCopy(tmpPath + pathLen, ".tmp", 5);

    // binary write keeps the file free of the text mode utf8 BOM
    AFile file = AFileOpen(tmpPath, AOpenFlag_WriteBinary);
    if (!AFileExist(file)) return 0;

    char line[1024];
    char* p;

    p = WStr(line, "axscene");
    p = WInt(p, SCENE_FILE_VERSION);
    WEnd(file, line, p);

    p = WStr(line, "sun");
    p = WFlt(p, g_RenderSettings.sunYaw);
    p = WFlt(p, g_RenderSettings.sunPitch);
    WEnd(file, line, p);

    for (u32 c = 0; c < TextureClass_Count; c++)
    {
        p = WStr(line, "atlas");
        p = WInt(p, (s64)c);
        p = WInt(p, atlasBaked[c] > 0 ? (s64)ts->classes[c].openPages : 0);
        p = WStr(p, " ");
        p = WStr(p, atlasBaked[c] > 0 ? GetFileName(atlasPath[c]) : "-");
        WEnd(file, line, p);
    }

    // bundle indices are stable handles and may contain holes; persist only the live bundles,
    // a reload re-adds them sequentially into a hole-free scene.
    // skip procedural meshes as well
    u32 liveBundles = 0;
    for (u32 b = 0; b < scene->numBundles; b++)
    {
        const SceneBundleRef* ref = &scene->bundleRefs[b];
        liveBundles += ref->bundle != NULL;
    }

    p = WStr(line, "bundles");
    p = WInt(p, (s64)liveBundles);
    WEnd(file, line, p);
    for (u32 b = 0; b < scene->numBundles; b++)
    {
        const SceneBundleRef* ref = &scene->bundleRefs[b];
        if (!ref->bundle) continue;
        p = WStr(line, "bundle");
        p = WInt(p, ref->skinned != 0);
        p = WInt(p, (s64)ref->materialOffset);
        p = WInt(p, (s64)ref->bundle->numMaterials);
        MeshType meshType = IsBundlePrimitive(ref->bundle);
        meshType = ref->isRuntime && meshType == MeshType_Default ? MeshType_Runtime : meshType;

        p = WInt(p, (s64)meshType);
        if (meshType == MeshType_Runtime)
        {
            WEnd(file, line, p);
            WriteProceduralBundle(ref, file);
        }
        else
        {
            WEnd(file, line, WStr(p, ref->path));
        }
    }

    p = WStr(line, "descriptors");
    p = WInt(p, (s64)ts->numDescriptors);
    WEnd(file, line, p);
    for (u32 i = 0; i < ts->numDescriptors; i++)
    {
        const TextureDescriptor* desc = &ts->descriptors[i];
        p = WStr(line, "desc");
        p = WInt(p, (s64)desc->pageIndex);
        p = WInt(p, (s64)(u32)(desc->uvBias.x  * TEXTURE_PAGE_SIZE + 0.5f));
        p = WInt(p, (s64)(u32)(desc->uvBias.y  * TEXTURE_PAGE_SIZE + 0.5f));
        p = WInt(p, (s64)(u32)(desc->uvScale.x * TEXTURE_PAGE_SIZE + 0.5f));
        p = WInt(p, (s64)(u32)(desc->uvScale.y * TEXTURE_PAGE_SIZE + 0.5f));
        p = WInt(p, (s64)desc->flags);
        WEnd(file, line, p);
    }

    p = WStr(line, "materials");
    p = WInt(p, (s64)ts->materialWatermark);
    WEnd(file, line, p);
    for (u32 i = 0; i < ts->materialWatermark; i++)
    {
        const MaterialGPU* material = &ts->materials[i];
        p = WStr(line, "mat");
        p = WInt(p, (s64)material->albedoDescriptor);
        p = WInt(p, (s64)material->normalDescriptor);
        p = WInt(p, (s64)material->metallicRoughnessDescriptor);
        p = WInt(p, (s64)material->flags);
        p = WInt(p, (s64)material->baseColorFactor);
        p = WInt(p, (s64)material->metallicRoughnessFactor);
        WEnd(file, line, p);
    }

    p = WStr(line, "lights");
    p = WInt(p, (s64)scene->numLights);
    WEnd(file, line, p);
    for (u32 i = 0; i < scene->numLights; i++)
    {
        const LightGPU* light = &scene->lights[i];
        p = WStr(line, "light");
        p = WInt(p, (s64)light->type);
        p = WInt(p, (s64)light->flags);
        for (u32 k = 0; k < 4u; k++) p = WFlt(p, light->positionRadius[k]);
        f32 directionCone[4];
        f32 colorIntensity[4];
        LightGPU_GetDirectionCone(light, directionCone);
        LightGPU_GetColor3(light, colorIntensity);
        colorIntensity[3] = LightGPU_GetIntensity(light);
        for (u32 k = 0; k < 4u; k++) p = WFlt(p, directionCone[k]);
        for (u32 k = 0; k < 4u; k++) p = WFlt(p, colorIntensity[k]);
        WEnd(file, line, p);
    }

    // raw render set entities in (group, local) order, the dense layout reproduces on load.
    // rotation and scale stay in their packed forms so the round trip is exact
    for (u32 s = 0; s < 2u; s++)
    {
        const RenderSet* set = s == 0u ? &scene->surfaceSet : &scene->skinnedSet;
        p = WStr(line, "entities");
        p = WInt(p, (s64)s);
        p = WInt(p, (s64)set->numEntities);
        WEnd(file, line, p);

        for (u32 g = 0; g < set->numGroups; g++)
        {
            const PrimitiveGroup* group = &set->primitiveGroups[g];
            for (u32 e = 0; e < group->numEntities; e++)
            {
                const Entity* entity = &set->entities[group->entityOffset + e];
                AX_ALIGN(16) float position[4];
                VecStore(position, entity->position);

                p = WStr(line, "ent");
                p = WInt(p, (s64)g);
                for (u32 k = 0; k < 3u; k++) p = WFlt(p, position[k]);
                p = WInt(p, (s64)(u32)(entity->rotation & 0xFFFFFFFFull));
                p = WInt(p, (s64)(u32)(entity->rotation >> 32u));
                p = WInt(p, (s64)entity->scale);
                p = WInt(p, (s64)entity->sparseIdx);
                p = WInt(p, (s64)entity->flags);
                WEnd(file, line, p);
            }
        }
    }

    // physics overrides: only surface entities whose body deviates from the default
    // static-mesh collider. reapplied on load once the async collider build finishes.
    const RenderSet* surfaceSet = &scene->surfaceSet;
    u32 physCount = 0;
    for (u32 g = 0; g < surfaceSet->numGroups; g++)
    {
        const PrimitiveGroup* group = &surfaceSet->primitiveGroups[g];
        for (u32 e = 0; e < group->numEntities; e++)
        {
            ScenePhysicsRecord rec;
            physCount += Physics_GetEntityOverride(scene, surfaceSet->entities[group->entityOffset + e].sparseIdx, &rec);
        }
    }

    p = WStr(line, "physics");
    p = WInt(p, (s64)physCount);
    WEnd(file, line, p);
    for (u32 g = 0; g < surfaceSet->numGroups; g++)
    {
        const PrimitiveGroup* group = &surfaceSet->primitiveGroups[g];
        for (u32 e = 0; e < group->numEntities; e++)
        {
            ScenePhysicsRecord rec;
            if (!Physics_GetEntityOverride(scene, surfaceSet->entities[group->entityOffset + e].sparseIdx, &rec))
                continue;
            p = WStr(line, "phys");
            p = WInt(p, (s64)rec.sparseIdx);
            p = WInt(p, (s64)rec.bodyType);
            p = WInt(p, (s64)rec.shapeType);
            p = WInt(p, (s64)rec.lockBits);
            p = WFlt(p, rec.friction);
            p = WFlt(p, rec.restitution);
            p = WFlt(p, rec.density);
            p = WFlt(p, rec.linearDamping);
            p = WFlt(p, rec.angularDamping);
            p = WFlt(p, rec.gravityScale);
            p = WFlt(p, rec.sleepThreshold);
            WEnd(file, line, p);
        }
    }

    AFileClose(file);
    RemoveFile(path);
    RenameFile(tmpPath, path);
    AX_LOG("scene saved: %s bundles=%d entities=%d lights=%d %.2fs",
           path, scene->numBundles, scene->surfaceSet.numEntities + scene->skinnedSet.numEntities,
           scene->numLights, TimeSinceStartup() - startTime);
    return 1;
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                                  Load                                    */
/*//////////////////////////////////////////////////////////////////////////*/


// reads one line and checks it starts with the expected keyword. the line keeps its
// trailing newline from AFileReadLine, trim it so path tails parse clean.
// out: token tail or NULL
static const char* ReadRecord(AFile file, char* buffer, int bufferSize, const char* keyword)
{
    int len = AFileReadLine(buffer, bufferSize, file);
    if (len <= 0)
    {
        AX_WARN("scene record missing, expected '%s'", keyword);
        return NULL;
    }
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r' || buffer[len - 1] == ' '))
        buffer[--len] = '\0';
    int keyLen = StringLength(keyword);
    if (len < keyLen || !StringEqual(buffer, keyword, keyLen))
    {
        AX_WARN("scene record mismatch, expected '%s' got '%.48s'", keyword, buffer);
        return NULL;
    }
    return buffer + keyLen;
}

static s32 ParseSceneFile(const char* path, SceneFileData* data)
{
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
    {
        AX_ERROR("scene file not found: %s", path);
        return 0;
    }

    char baseDir[1024];
    char line[2048];
    const char* p;
    GetBaseDir(path, baseDir);
    s32 baseLen = StringLength(baseDir);

    u32 version = 0;
    if (!(p = ReadRecord(file, line, sizeof(line), "axscene")))
    {
        // tolerate a utf8 BOM in front of the first record (text mode writers add one)
        bool bom = line[0] == '\xEF' && line[1] == '\xBB' && line[2] == '\xBF' &&
                   StringEqual(line + 3, "axscene", 7);
        if (!bom) goto fail;
        p = line + 3 + 7;
    }
    RU32(p, &version);
    if (version > SCENE_FILE_VERSION)
    {
        AX_ERROR("scene file version %d not supported: %s", version, path);
        AFileClose(file);
        return 0;
    }

    if (!(p = ReadRecord(file, line, sizeof(line), "sun"))) goto fail;
    p = RFlt(p, &data->sunYaw);
    RFlt(p, &data->sunPitch);

    for (u32 c = 0; c < TextureClass_Count; c++)
    {
        u32 classIdx = 0;
        if (!(p = ReadRecord(file, line, sizeof(line), "atlas"))) goto fail;
        p = RU32(p, &classIdx);
        p = RU32(p, &data->atlasLayers[c]);
        while (*p == ' ') p++;
        data->atlasPath[c][0] = '\0';
        if (data->atlasLayers[c] > 0u && *p && *p != '-')
        {
            int nameLen = StringLength(p);
            if (baseLen + nameLen + 1 <= (int)sizeof(data->atlasPath[c]))
            {
                MemCopy(data->atlasPath[c], baseDir, baseLen);
                MemCopy(data->atlasPath[c] + baseLen, p, nameLen + 1);
            }
        }
    }

    if (!(p = ReadRecord(file, line, sizeof(line), "bundles"))) goto fail;
    RU32(p, &data->numBundles);
    if (data->numBundles > MAX_SCENE_BUNDLES) goto fail;
    data->bundlePaths       = (char*)CDAllocTLSF((u64)Maxu32(data->numBundles, 1u) * 1024u);
    data->bundleSkinned     = (u32*)CDAllocTLSF(Maxu32(data->numBundles, 1u) * sizeof(u32));
    data->bundleMaterialOff = (u32*)CDAllocTLSF(Maxu32(data->numBundles, 1u) * sizeof(u32));
    data->runtimeBundles    = (SceneBundle**)CDAllocTLSF(Maxu32(data->numBundles, 1u) * sizeof(SceneBundle*));
    for (u32 b = 0; b < data->numBundles; b++)
    {
        u32 numMaterials = 0;
        if (!(p = ReadRecord(file, line, sizeof(line), "bundle"))) 
            goto fail;
        p = RU32(p, &data->bundleSkinned[b]);
        p = RU32(p, &data->bundleMaterialOff[b]);
        p = RU32(p, &numMaterials);
        u32 meshType; // MeshType
        p = RU32(p, &meshType);
        char* nameBuffer = data->bundlePaths + ((u64)b * 1024u);
        if (meshType == MeshType_Runtime)
        {
            data->runtimeBundles[b] = ReadProceduralBundle(file, version, nameBuffer);
            continue;
        }
        else if (meshType > MeshType_Runtime) // unit mesh
        {
            data->runtimeBundles[b] = GetUnitPrimitive(meshType);
            const char* name = GetPrimitiveName(meshType);
            MemCopy(nameBuffer, name, StringLength(name) + 1);
            continue;
        }

        char* bundlePath = data->bundlePaths + (u64)b * 1024u;
        while (*p == ' ') p++;
        int nameLen = StringLength(p);
        if (nameLen <= 0 || nameLen >= 1024) 
            goto fail;
        MemCopy(bundlePath, p, nameLen + 1);
    }

    if (!(p = ReadRecord(file, line, sizeof(line), "descriptors")))
        goto fail;
    RU32(p, &data->numDescriptors);
    if (data->numDescriptors > MAX_TEXTURE_DESCRIPTORS) goto fail;
    data->descriptors = (TextureDescriptor*)AllocTLSF(Maxu32(data->numDescriptors, 1u) * sizeof(TextureDescriptor));
    for (u32 i = 0; i < data->numDescriptors; i++)
    {
        u32 page = 0, x = 0, y = 0, w = 0, h = 0, flags = 0;
        if (!(p = ReadRecord(file, line, sizeof(line), "desc")))
            goto fail;
        p = RU32(p, &page);
        p = RU32(p, &x);
        p = RU32(p, &y);
        p = RU32(p, &w);
        p = RU32(p, &h);
        RU32(p, &flags);
        TextureDescriptor* desc = &data->descriptors[i];
        desc->pageIndex = page;
        desc->flags = flags;
        desc->uvBias.x  = (float)x / (float)TEXTURE_PAGE_SIZE;
        desc->uvBias.y  = (float)y / (float)TEXTURE_PAGE_SIZE;
        desc->uvScale.x = (float)w / (float)TEXTURE_PAGE_SIZE;
        desc->uvScale.y = (float)h / (float)TEXTURE_PAGE_SIZE;
    }

    if (!(p = ReadRecord(file, line, sizeof(line), "materials")))
        goto fail;
    RU32(p, &data->materialWatermark);
    if (data->materialWatermark > MAX_GPU_MATERIALS) goto fail;
    data->materials = (MaterialGPU*)AllocTLSF(Maxu32(data->materialWatermark, 1u) * sizeof(MaterialGPU));
    for (u32 i = 0; i < data->materialWatermark; i++)
    {
        MaterialGPU* material = &data->materials[i];
        if (!(p = ReadRecord(file, line, sizeof(line), "mat")))
            goto fail;
        u32 albedoDescriptor = 0u;
        u32 normalDescriptor = 0u;
        u32 metallicRoughnessDescriptor = 0u;
        u32 flags = 0u;
        p = RU32(p, &albedoDescriptor);
        p = RU32(p, &normalDescriptor);
        p = RU32(p, &metallicRoughnessDescriptor);
        p = RU32(p, &flags);
        p = RU32(p, &material->baseColorFactor);
        RU32(p, &material->metallicRoughnessFactor);
        material->albedoDescriptor = (u16)albedoDescriptor;
        material->normalDescriptor = (u16)normalDescriptor;
        material->metallicRoughnessDescriptor = (u16)metallicRoughnessDescriptor;
        material->flags = (u16)flags;
    }

    if (!(p = ReadRecord(file, line, sizeof(line), "lights")))
        goto fail;
    RU32(p, &data->numLights);
    if (data->numLights > MAX_SCENE_LIGHTS)
        goto fail;
    data->lights = (LightGPU*)AllocTLSF(Maxu32(data->numLights, 1u) * sizeof(LightGPU));
    for (u32 i = 0; i < data->numLights; i++)
    {
        LightGPU* light = &data->lights[i];
        if (!(p = ReadRecord(file, line, sizeof(line), "light")))
            goto fail;
        u32 type = 0u;
        u32 flags = 0u;
        p = RU32(p, &type);
        p = RU32(p, &flags);
        for (u32 k = 0; k < 4u; k++) p = RFlt(p, &light->positionRadius[k]);
        f32 directionCone[4];
        f32 colorIntensity[4];
        for (u32 k = 0; k < 4u; k++) p = RFlt(p, &directionCone[k]);
        for (u32 k = 0; k < 4u; k++) p = RFlt(p, &colorIntensity[k]);
        LightGPU_SetDirectionCone(light, directionCone);
        LightGPU_SetColor3(light, colorIntensity);
        LightGPU_SetIntensity(light, colorIntensity[3]);
        light->type = (u8)type;
        light->flags = (u8)flags;
        light->shadowIndex = LIGHT_SHADOW_INDEX_INVALID;
    }

    for (u32 s = 0; s < 2; s++)
    {
        u32 setIdx = 0;
        if (!(p = ReadRecord(file, line, sizeof(line), "entities")))
            goto fail;
        p = RU32(p, &setIdx);
        RU32(p, &data->numEntities[s]);
        if (setIdx != s || data->numEntities[s] > (s == 1u ? MAX_ANIM_INSTANCES : MAX_ENTITY)) goto fail;
        data->entities[s] = (SceneEntRecord*)AllocTLSF(Maxu32(data->numEntities[s], 1u) * sizeof(SceneEntRecord));
        for (u32 i = 0; i < data->numEntities[s]; i++)
        {
            SceneEntRecord* record = &data->entities[s][i];
            u32 rotLo = 0, rotHi = 0;
            if (!(p = ReadRecord(file, line, sizeof(line), "ent"))) 
                goto fail;
            p = RU32(p, &record->primGroupIdx);
            for (u32 k = 0; k < 3u; k++) p = RFlt(p, &record->position[k]);
            p = RU32(p, &rotLo);
            p = RU32(p, &rotHi);
            record->scale = 0;
            if (version == 1)
            {
                u32 legacyScale = 0;
                p = RU32(p, &legacyScale);
                record->scale = EntityPackWorldScale(VecMulf(SceneUnpackLegacyScaleXY11Z10(legacyScale), ENTITY_MAX_SCALE));
            }
            if (version >= 2)
                p = RU64(p, &record->scale);

            p = RU32(p, &record->sparseIdx);
            record->flags = EntityFlags_ColliderEnabled;
            if (version >= 5) 
                p = RU32(p, &record->flags);
            record->rotation = (u64)rotLo | ((u64)rotHi << 32u);
        }
    }

    // physics overrides section (version 4+); absent in older files
    if (version >= 4u)
    {
        if (!(p = ReadRecord(file, line, sizeof(line), "physics")))
            goto fail;
        RU32(p, &data->numPhysics);
        if (data->numPhysics > MAX_ENTITY) 
            goto fail;
        data->physics = (ScenePhysicsRecord*)AllocTLSF(Maxu32(data->numPhysics, 1u) * sizeof(ScenePhysicsRecord));
        for (u32 i = 0; i < data->numPhysics; i++)
        {
            ScenePhysicsRecord* rec = &data->physics[i];
            if (!(p = ReadRecord(file, line, sizeof(line), "phys")))
                goto fail;
            p = RU32(p, &rec->sparseIdx);
            p = RU32(p, &rec->bodyType);
            p = RU32(p, &rec->shapeType);
            p = RU32(p, &rec->lockBits);
            p = RFlt(p, &rec->friction);
            p = RFlt(p, &rec->restitution);
            p = RFlt(p, &rec->density);
            p = RFlt(p, &rec->linearDamping);
            p = RFlt(p, &rec->angularDamping);
            p = RFlt(p, &rec->gravityScale);
            p = RFlt(p, &rec->sleepThreshold);
        }
    }

    AFileClose(file);
    return 1;
fail:
    AX_ERROR("scene file parse failed: %s", path);
    AFileClose(file);
    return 0;
}

static void BuildColliderEndCallback(void* data, s32 result)
{
    Scene* scene = (Scene*)data;
    Physics_ApplyPendingOverrides(scene); // no-op when nothing was persisted
    scene->physicsReady = true;
}

void SceneFileData_Destroy(SceneFileData* data)
{
    if (PTR_VALID(data->bundlePaths))    DeAllocTLSF(data->bundlePaths);
    if (PTR_VALID(data->bundleSkinned))  DeAllocTLSF(data->bundleSkinned);
    if (PTR_VALID(data->runtimeBundles)) DeAllocTLSF(data->runtimeBundles);
    if (PTR_VALID(data->descriptors))    DeAllocTLSF(data->descriptors);
    if (PTR_VALID(data->materials))      DeAllocTLSF(data->materials);
    if (PTR_VALID(data->lights))         DeAllocTLSF(data->lights);
    MemSet(data, 0, sizeof(SceneFileData));
    DeAllocTLSF(data);
}

SceneFileData* SceneSerializer_LoadStage(const char* path)
{
    SceneFileData* data = AllocTLSFArray(SceneFileData, 1);
    MemsetZero(data, sizeof(data));
    if (!ParseSceneFile(path, data))
    {
        SceneFileData_Destroy(data);
        return NULL;
    }
    SceneBundleStage* stages = CAllocTLSFArray(SceneBundleStage, Maxu32(data->numBundles, 1u));
    data->stages = stages;
    data->baked = true;
    for (u32 b = 0; b < data->numBundles; b++)
    {
        char* path = data->bundlePaths + ((u64)b * 1024u);
        bool isRuntime = PTR_VALID(data->runtimeBundles[b]);
        stages[b].path           = StringDuplicate(path);
        stages[b].skinned        = data->bundleSkinned[b] != 0u;
        stages[b].materialOffset = data->bundleMaterialOff[b];
        stages[b].bundle         = data->runtimeBundles[b]; // for custom meshes this is not null
        if (isRuntime) {
            stages[b].cacheKey = MurmurHash((u64)stages[b].bundle);
        }
        else {
            stages[b].cacheKey = StringToHash64(stages[b].path);
        }
    }

    return data;
}

// Stages every bundle (mesh, plus textures on the slow path) across worker threads via
// ParallelFor, then publishes serially on the main thread. Shared by both load paths below;
// baked bundles are already-atlased (mesh only), everything else goes through stage/finalize.
// out: false on any bundle failure, scene is left with whatever finalized before the failure
static bool SceneSerializer_LoadBundles(Scene* scene, SceneFileData* data, const char* path)
{
    SceneBundleStage* stages = data->stages;
    // the fast path needs compressed pages, every referenced atlas on disk and the tables
    bool baked = scene->textureSystem.compressed != 0u && data->numDescriptors >= SceneSer_DefaultDescriptors;
    for (u32 c = 0; c < TextureClass_Count && baked; c++)
        if (data->atlasLayers[c] > 0u && (data->atlasPath[c][0] == '\0' || !FileExist(data->atlasPath[c])))
            baked = false;
    data->baked = baked;
    
    bool ok = true;
    for (u32 b = 0; b < data->numBundles; b++)
    {
        u32 bundleIdx = baked ? Scene_AddBundleBakedFinalize(scene, &stages[b])
                              : Scene_AddBundleFinalize(scene, &stages[b]);
        if (bundleIdx == INVALID_BUNDLE) { ok = false; continue; }
        if (scene->bundleRefs[bundleIdx].materialOffset != data->bundleMaterialOff[b])
            AX_WARN("scene bundle material offset drifted: %s %d != %d", data->bundlePaths + (u64)b * 1024u,
                    scene->bundleRefs[bundleIdx].materialOffset, data->bundleMaterialOff[b]);
    }

    DeAllocTLSF(stages);

    if (baked)
    {
        const char* atlasPaths[TextureClass_Count];
        for (u32 c = 0; c < TextureClass_Count; c++)
            atlasPaths[c] = data->atlasLayers[c] > 0u ? data->atlasPath[c] : NULL;

        if (TextureSystem_RestoreBaked(&scene->textureSystem, atlasPaths,
                                       data->descriptors, data->numDescriptors,
                                       data->materials, data->materialWatermark))
        {
            scene->texturesBaked = 1;
        }
        else
        {
            AX_WARN("baked atlas restore failed, repacking from bundle caches: %s", path);
            if (!Scene_RepackTextures(scene))
            {
                SceneFileData_Destroy(data);
                return 0;
            }
        }
        if (data->materialWatermark > scene->numMaterials)
            scene->numMaterials = data->materialWatermark;
    }
    else AX_LOG("scene loaded through the slow path (no baked atlases): %s", path);
    
    return ok;
}

s32 SceneSerializer_Load(Scene* scene, const char* path, SceneFileData* data)
{
    double startTime = TimeSinceStartup();
    
    if (!data)
    {
        data = SceneSerializer_LoadStage(path);
        ParallelFor(data->numBundles, 1u, SceneStageRange, &(SceneStageRangeCtx){ data->stages, data->baked });
    }

    if (!SceneSerializer_LoadBundles(scene, data, path))
    {
        return 0;
    }

    // entities restore straight into the render sets, records are in (group, local) order
    // so the dense layout and sparse ids come back exactly as saved
    for (u32 s = 0; s < 2u; s++)
    {
        bool isSkinned = s == 1u;
        RenderSet* set = s == 0u ? &scene->surfaceSet : &scene->skinnedSet;

        u32* primitiveCounts = (u32*)ArenaAllocGlobal(set->numGroups * sizeof(u32));
        MemSet(primitiveCounts, 0, set->numGroups * sizeof(u32));
        u32 validEntities = 0;

        for (u32 i = 0; i < data->numEntities[s]; i++)
        {
            const SceneEntRecord* record = &data->entities[s][i];
            if (record->primGroupIdx >= set->numGroups)
            {
                AX_WARN("scene entity group out of range: %d >= %d", record->primGroupIdx, set->numGroups);
                continue;
            }
            if (record->sparseIdx >= set->maxEntities)
            {
                AX_WARN("scene entity sparse id out of range: %d >= %d", record->sparseIdx, set->maxEntities);
                continue;
            }
            primitiveCounts[record->primGroupIdx]++;
            validEntities++;
        }

        u32 entityOffset = 0;
        for (u32 g = 0; g < set->numGroups; g++)
        {
            PrimitiveGroup* group = &set->primitiveGroups[g];
            group->entityOffset = entityOffset;
            u32 numEntities = primitiveCounts[g];
            group->numEntities = 0;
            group->capacity = numEntities;
            entityOffset += numEntities;
        }
        set->numEntities = validEntities;
        primitiveCounts = NULL;
        ArenaPopGlobal(set->numGroups * sizeof(u32)); // primitiveCounts

        for (u32 i = 0; i < data->numEntities[s]; i++)
        {
            const SceneEntRecord* record = &data->entities[s][i];
            u32 groupIdx = record->primGroupIdx;
            if (groupIdx >= set->numGroups || record->sparseIdx >= set->maxEntities)
                continue;

            PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
            u32 denseIdx = group->entityOffset + group->numEntities;
            Entity* entity = &set->entities[denseIdx];
            MemsetZero(entity, sizeof(entity));
            group->numEntities++;
            entity->position  = Vec3Load(record->position);
            entity->rotation  = record->rotation;
            entity->scale     = record->scale;
            entity->primitiveIdx = groupIdx;
            entity->sparseIdx = record->sparseIdx;
            entity->flags = record->flags;

            BitsetSet(set->sparseSlots, (s32)record->sparseIdx);
            if (set->sparseID[record->sparseIdx] == INVALID_ENTITY || denseIdx < set->sparseID[record->sparseIdx])
                set->sparseID[record->sparseIdx] = denseIdx;

            if (isSkinned)
            {
                u32 bundleIdx = Scene_FindBundleForRenderGroup(scene, true, groupIdx);
                if (bundleIdx != INVALID_BUNDLE)
                {
                    GPUAnimationInstance instance = {
                        .animIdx = Scene_DefaultAnimation(scene, bundleIdx),
                        .timeOffset = 0.0f
                    };
                    AnimationSystem_SetInstance(&scene->animSystem, entity->sparseIdx, instance);
                }
            }
        }
        RenderSet_Validate(set, isSkinned ? "load skinned" : "load surface");
    }

    if (data->numLights > 0)
        MemCopy(scene->lights, data->lights, data->numLights * sizeof(LightGPU));
    scene->numLights = data->numLights;

    g_RenderSettings.sunYaw   = data->sunYaw;
    g_RenderSettings.sunPitch = data->sunPitch;
    scene->renderDataDirty = 1;

    if (scene->pendingPhysics) DeAllocTLSF(scene->pendingPhysics);
    scene->pendingPhysics = NULL;
    scene->numPendingPhysics = 0;
    if (data->numPhysics > 0)
    {
        scene->pendingPhysics = AllocTLSFArray(ScenePhysicsRecord, data->numPhysics);
        if (scene->pendingPhysics)
        {
            MemCopy(scene->pendingPhysics, data->physics, data->numPhysics * sizeof(ScenePhysicsRecord));
            scene->numPendingPhysics = data->numPhysics;
        }
    }

    scene->physicsReady = false;
    // every mesh instance is static for now: give each a static rigid body with a triangle collider
    Scene_BuildStaticCollidersAsync(scene, BuildColliderEndCallback);

    AX_LOG("scene loaded: %s bundles=%d entities=%d lights=%d baked=%d %.2fs",
           path, scene->numBundles, scene->surfaceSet.numEntities + scene->skinnedSet.numEntities,
           scene->numLights, scene->texturesBaked, TimeSinceStartup() - startTime);
    
    SceneFileData_Destroy(data);
    return 1;
}