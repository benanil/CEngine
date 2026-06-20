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

// #include "Scene.h"

#if !AX_GAME_BUILD
	#include "Extern/ufbx.h"
#endif

#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Math/Bitpack.h"
#include "Include/FileSystem.h"
#include "Include/Common.h"
#include "Include/BasisBinding.h"
#include "Include/FastDelta.h"

#define SINFL_IMPLEMENTATION
#define SDEFL_IMPLEMENTATION
#include "Extern/sdefl.h"
#include "Extern/sinfl.h"
#include "Extern/dynarray.h"

#include "Extern/BasisCompressWrapper.h"

extern Graphics gGFX;


/*//////////////////////////////////////////////////////////////////////////*/
/*                              FBX LOAD                                    */
/*//////////////////////////////////////////////////////////////////////////*/

#if !AX_GAME_BUILD

static s32 void_ptr_compare(const void* a, const void* b)
{
    const void* const* ptr_a = (const void* const*)a;
    const void* const* ptr_b = (const void* const*)b;
    if (*ptr_a < *ptr_b) return -1;
    return *ptr_a > *ptr_b ? 1 : 0;
}

static u16 GetFBXTexture(const ufbx_material* umaterial, const ufbx_scene* uscene, ufbx_material_feature feature, ufbx_material_pbr_map pbr, ufbx_material_fbx_map fbx)
{
    if (umaterial->features.features[feature].enabled)
    {
        ufbx_texture* texture = NULL;
        // search for normal texture
        if (umaterial->features.pbr.enabled)
            texture = umaterial->pbr.maps[pbr].texture;
        
        if (!texture)
            texture = umaterial->fbx.maps[fbx].texture;
        
        if (texture)
        {
            u16 textureIndex = aIndexOf(uscene->textures.data, texture, (s32)uscene->textures.count, sizeof(ufbx_texture*), void_ptr_compare);
            ASSERT(textureIndex != -1); // we should find in textures
            return textureIndex;
        }
    }
    return -1;
}

static char* GetNameFromFBX(ufbx_string ustr, FixedPow2Allocator* stringAllocator)
{
    if (ustr.length == 0) return NULL;
    char* name = FixedPow2Allocator_AllocateUninitialized(stringAllocator, ustr.length + 1);
    SmallMemCpy(name, ustr.data, ustr.length);
    name[ustr.length] = 0;
    return name;
}
#endif

s32 LoadFBX(const char* path, SceneBundle* fbxScene, f32 scale)
{
#if !AX_GAME_BUILD
    MemsetZero(fbxScene, sizeof(SceneBundle));
    AX_LOG("fbx import: %s scale=%f", path, scale);

    ufbx_load_opts opts = { 0 };
    // FBX is currently imported as static/non-animated scene data; animation stacks are intentionally ignored.
    opts.evaluate_skinning = false;
    opts.evaluate_caches = false;
    opts.load_external_files = false;
    opts.generate_missing_normals = true;
    opts.ignore_missing_external_files = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f * (1.0f / scale);
    opts.obj_search_mtl_by_filename = true;

    opts.unicode_error_handling = UFBX_UNICODE_ERROR_HANDLING_ABORT_LOADING;
    
    ufbx_error error;
    ufbx_scene* uscene;
    
    uscene = ufbx_load_file((const char*)path, &opts, &error);
    
    if (!uscene) {
        AX_ERROR("fbx mesh load failed! %s", error.info);
        return 0;
    }    
    
    fbxScene->numMeshes     = (u16)uscene->meshes.count;
    fbxScene->numNodes      = (u16)uscene->nodes.count;
    fbxScene->numMaterials  = (u16)uscene->materials.count;
    fbxScene->numImages     = (u16)uscene->texture_files.count; 
    fbxScene->numTextures   = (u16)uscene->textures.count;
    fbxScene->numCameras    = (u16)uscene->cameras.count;
    fbxScene->totalVertices = 0;
    fbxScene->totalIndices  = 0;
    fbxScene->numScenes     = 1;
    fbxScene->defaultSceneIndex = 0;
    fbxScene->numAnimations = 0;
    AX_LOG("fbx parse: nodes=%d meshes=%d materials=%d textures=%d cameras=%d skins=%d",
           fbxScene->numNodes, fbxScene->numMeshes, fbxScene->numMaterials,
           fbxScene->numTextures, fbxScene->numCameras, (s32)uscene->skin_deformers.count);
    if (uscene->skin_deformers.count > 0)
        AX_WARN("fbx contains skin deformers, but this importer is intended for non-skinned meshes only: %s", path);
    
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 2048);
    
    u64 totalIndices  = 0, totalVertices = 0;
    for (s32 i = 0; i < fbxScene->numMeshes; i++)
    {
        ufbx_mesh* umesh = uscene->meshes.data[i];
        if (umesh->num_triangles == 0 || umesh->num_vertices == 0)
            AX_WARN("fbx mesh %d has no renderable triangles/vertices", i);
        totalIndices  += umesh->num_triangles * 3;
        totalVertices += umesh->num_vertices;
    }
    
    fbxScene->allVertices = AllocAligned(sizeof(ASkinedVertex) * totalVertices, 4);
    fbxScene->allIndices  = AllocAligned(sizeof(u32) * totalIndices, 4);
    
    if (fbxScene->numMeshes) fbxScene->meshes = (AMesh*)AllocZeroTLSFGlobal(fbxScene->numMeshes, sizeof(AMesh));
    fbxScene->scenes = (AScene*)AllocZeroTLSFGlobal(1, sizeof(AScene));
    
    u32* currentIndex = (u32*)fbxScene->allIndices;
    ASkinedVertex* currentVertex = (ASkinedVertex*)fbxScene->allVertices;

    u32 vertexCursor = 0u, indexCursor = 0u;
    
    for (s32 i = 0; i < fbxScene->numMeshes; i++)
    {
        AMesh* amesh = &fbxScene->meshes[i];
        ufbx_mesh* umesh = uscene->meshes.data[i];
        amesh->name = GetNameFromFBX(umesh->name, allocator);
        amesh->primitives = dynarray_create_prealloc(APrimitive, 1);
        amesh->numPrimitives = 1;
        
        APrimitive* primitive = &amesh->primitives[0];
        primitive->numIndices  = (s32)umesh->num_triangles * 3;
        primitive->numVertices = (s32)umesh->num_vertices;
        primitive->indexType   = 5; //GraphicType_UnsignedInt;
        primitive->material    = 0; // todo
        primitive->indices     = currentIndex; 
        primitive->vertices    = currentVertex;
        
        primitive->attributes |= AAttribType_POSITION;
        primitive->attributes |= ((s32)umesh->vertex_uv.exists << 1) & AAttribType_TEXCOORD_0;
        primitive->attributes |= ((s32)umesh->vertex_normal.exists << 2) & AAttribType_NORMAL;
        if (!umesh->vertex_uv.exists)
            AX_WARN("fbx mesh %d (%s) has no UVs", i, amesh->name ? amesh->name : "<unnamed>");
        
        for (s32 j = 0; j < primitive->numVertices; j++)
        {
            // SmallMemCpy(&currentVertex[j].position.x, &umesh->vertex_position.values.data[j], sizeof(float) * 3);
            if (umesh->vertex_uv.exists)
            {
                currentVertex[j].texCoord = Float2ToHalf2((f32*)(umesh->vertex_uv.values.data + j));
            }
            if (umesh->vertex_normal.exists) 
            {
                // currentVertex[j].qtangentXYf16 = PackVec3XYZ10BitToInt(Vec3Load((f1*)(umesh->vertex_normal.values.data + j)));
            }
            if (umesh->vertex_tangent.exists)
            {
                v128f tangent = Vec3Load((f32*)(umesh->vertex_tangent.values.data + j));
                VecSetW(tangent, 1.0f);
                // currentVertex[j].qtangentZWF16 = PackVec3XYZ10BitToInt(tangent);
            }
        }

        u32* currIndices = (u32*)primitive->indices;
        u32 indices[64] = {0};
        
        for (s32 j = 0; j < umesh->faces.count; j++)
        {
            ufbx_face face = umesh->faces.data[j];
            u32 num_triangles = ufbx_triangulate_face(indices, ARRAY_SIZE(indices), umesh, face);
            
            for (u32 tri_ix = 0; tri_ix < num_triangles; tri_ix++)
            {
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 0]] + vertexCursor;
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 1]] + vertexCursor;
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 2]] + vertexCursor;
            }
        }
            
        u8 hasSkin = umesh->skin_deformers.count > 0;
        if (hasSkin)
        {
            primitive->attributes |= AAttribType_JOINTS | AAttribType_WEIGHTS;
            ufbx_skin_deformer* deformer = umesh->skin_deformers.data[0];
                
            for (s32 j = 0; j < deformer->vertices.count; j++)
            {
                u32 weightBegin = deformer->vertices.data[j].weight_begin;
                u32 weightResult = 0, shift = 0;
                u32 indexResult  = 0;
            
                for (u32 w = 0; w < deformer->vertices.data[j].num_weights && w < 4; w++, shift += 8)
                {
                    ufbx_skin_weight skinWeight = deformer->weights.data[weightBegin + w];
                    f32  weight = skinWeight.weight;
                    u32 index  = skinWeight.cluster_index;
                    ASSERT(index < 255 && weight <= 1.0f);
                    weightResult |= (u32)(weight * 255.0f) << shift;
                    indexResult  |= index << shift;
                }
            
                currentVertex[j].weights = weightResult;
                currentVertex[j].joints  = indexResult;
            }
        }
        
        fbxScene->totalIndices  += primitive->numIndices;
        fbxScene->totalVertices += primitive->numVertices;
        
        currentIndex  += primitive->numIndices;
        currentVertex += primitive->numVertices;
        
        primitive->indexOffset = indexCursor;
        vertexCursor += primitive->numVertices;
        indexCursor  += primitive->numIndices;
    }

    u32 numSkins = (u32)uscene->skin_deformers.count;
    fbxScene->numSkins = numSkins;
    
    if (numSkins > 0) {
        fbxScene->skins = AllocateTLSFGlobal(numSkins * sizeof(ASkin));
    }

    for (u32 d = 0; d < numSkins; d++)
    {
        ufbx_skin_deformer* deformer = uscene->skin_deformers.data[d];
        ASkin* skin = &fbxScene->skins[d];
        u32 numJoints = (u32)deformer->clusters.count;
        skin->numJoints = numJoints;
        skin->skeleton = 0;
        skin->inverseBindMatrices = AllocateTLSFGlobal(numJoints * sizeof(mat4x4));
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, (sizeof(s32) + 1) * numJoints);
    
        mat4x4* matrices = (mat4x4*)skin->inverseBindMatrices;
        for (u32 j = 0u; j < numJoints; j++)
        {
            ufbx_matrix uMatrix = deformer->clusters.data[j]->geometry_to_bone;
            matrices[j].r[0] = Vec3Load(&uMatrix.cols[0].x); 
            matrices[j].r[1] = Vec3Load(&uMatrix.cols[1].x); 
            matrices[j].r[2] = Vec3Load(&uMatrix.cols[2].x); 
            matrices[j].r[3] = Vec3Load(&uMatrix.cols[3].x); 
            VecSetW(matrices[j].r[3], 1.0f); 
        }
        
        for (u32 j = 0u; j < numJoints; j++)
        {
            skin->joints[j] = aIndexOf(uscene->nodes.data, deformer->clusters.data[j]->bone_node, (s32)uscene->nodes.count, sizeof(void*), void_ptr_compare);
        }
    }
    
    u16 numImages = (u16)uscene->texture_files.count;
    AImage* images = dynarray_create_prealloc(AImage, (s32)uscene->texture_files.count);
    
    for (s32 i = 0; i < numImages; i++)
    {
        images[i].path = GetNameFromFBX(uscene->texture_files.data[i].filename, allocator);
    }
    
    u16 numTextures = (u16)uscene->textures.count;
    fbxScene->numTextures = numTextures;
    fbxScene->numSamplers = numTextures;
    
    if (numTextures)
    {
        fbxScene->textures = (ATexture*)AllocZeroTLSFGlobal(numTextures, sizeof(ATexture));
        fbxScene->samplers = (ASampler*)AllocZeroTLSFGlobal(numTextures, sizeof(ASampler));
    }
    
    for (u16 i = 0; i < numTextures; i++)
    {
        ufbx_texture* utexture = uscene->textures.data[i];
        ATexture* atexture = &fbxScene->textures[i];
        
        atexture->source  = utexture->has_file ? utexture->file_index : 0; // index to images array
        atexture->name    = GetNameFromFBX(utexture->name, allocator);
        
        // is this embedded
        if (utexture->content.size)
        {
            char* buffer = FixedPow2Allocator_AllocateUninitialized(allocator, 512);
            MemsetZero(buffer, 512);
            s32 pathLen = StringLengthSafe(path, 512);
            SmallMemCpy(buffer, path, pathLen);
            
            char* fbxPath = PathGoBackwards(buffer, pathLen, false);
            // concat: FbxPath/TextureName
            SmallMemCpy(fbxPath, utexture->name.data, utexture->name.length); 
            SmallMemCpy(fbxPath + utexture->name.length, ".png", 4); // FbxPath/TextureName.png
            AFile file = AFileOpen(buffer, AOpenFlag_WriteBinary);
            AFileWrite(utexture->content.data, utexture->content.size, file, 1);
            atexture->source = dynarray_length(images);
            dynarray_push(images, (AImage) { buffer });
        }
        
        atexture->sampler = i;
        fbxScene->samplers[i].wrapS = utexture->wrap_u;
        fbxScene->samplers[i].wrapT = utexture->wrap_v;
    }
    
    u16 numMaterials = (u16)uscene->materials.count;
    fbxScene->numMaterials = numMaterials;
    
    if (numMaterials) {
        fbxScene->materials = AllocateTLSFGlobal(sizeof(AMaterial) * numMaterials);
    }

    for (u16 i = 0; i < numMaterials; i++)
    {
        ufbx_material* umaterial = uscene->materials.data[i];
        AMaterial* amaterial = fbxScene->materials + i;
        
        ufbx_texture* normalTexture = NULL;
        // search for normal texture
        if (umaterial->features.pbr.enabled) 
            normalTexture = umaterial->pbr.normal_map.texture;
        
        if (!normalTexture && umaterial->fbx.normal_map.has_value)
            normalTexture = umaterial->fbx.normal_map.texture;
        
        if (normalTexture)
        {
            u16 normalIndex = aIndexOf(uscene->textures.data, normalTexture, (s32)uscene->textures.count, 8, void_ptr_compare);
            ASSERT(normalIndex != -1); // we should find in textures
            amaterial->textures[0].index = normalIndex;
        }
        
        amaterial->textures[1].index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_AMBIENT_OCCLUSION,
                                                                        UFBX_MATERIAL_PBR_AMBIENT_OCCLUSION,
                                                                        UFBX_MATERIAL_FBX_AMBIENT_COLOR);
        
        amaterial->textures[2].index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_EMISSION,
                                                                        UFBX_MATERIAL_PBR_EMISSION_COLOR,
                                                                        UFBX_MATERIAL_FBX_EMISSION_COLOR);
        
        amaterial->baseColorTexture.index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_PBR,
                                                                             UFBX_MATERIAL_PBR_BASE_COLOR,
                                                                             UFBX_MATERIAL_FBX_DIFFUSE_COLOR);
        if (amaterial->baseColorTexture.index == UINT16_MAX)
            amaterial->baseColorTexture.index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_DIFFUSE,
                                                                                 UFBX_MATERIAL_PBR_BASE_COLOR,
                                                                                 UFBX_MATERIAL_FBX_DIFFUSE_COLOR);
        
        amaterial->specularTexture.index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_SPECULAR,
                                                                            UFBX_MATERIAL_PBR_SPECULAR_COLOR,
                                                                            UFBX_MATERIAL_FBX_SPECULAR_COLOR);
        
        
        amaterial->metallicRoughnessTexture.index = GetFBXTexture(umaterial, uscene, UFBX_MATERIAL_FEATURE_DIFFUSE_ROUGHNESS,
                                                                                    UFBX_MATERIAL_PBR_ROUGHNESS,
                                                                                    UFBX_MATERIAL_FBX_VECTOR_DISPLACEMENT_FACTOR);
        
        amaterial->metallicFactor   = MakeFloat16(umaterial->pbr.metalness.value_real);
        amaterial->roughnessFactor  = MakeFloat16(umaterial->pbr.roughness.value_real);
        amaterial->baseColorFactor  = MakeFloat16(umaterial->pbr.base_factor.value_real);
        
        amaterial->specularFactor   = umaterial->features.pbr.enabled ? MakeFloat16(umaterial->pbr.specular_factor.value_real)
                                                                     : MakeFloat16(umaterial->fbx.specular_factor.value_real);
        amaterial->diffuseColor     = PackColor3PtrToUint(&umaterial->fbx.diffuse_color.value_real);
        amaterial->specularColor    = PackColor3PtrToUint(&umaterial->fbx.specular_color.value_real);
        
        amaterial->doubleSided = umaterial->features.double_sided.enabled;
        
        if (umaterial->pbr.emission_factor.value_components == 1)
        {
            amaterial->emissiveFactor[0] = amaterial->emissiveFactor[1] = amaterial->emissiveFactor[2] 
                 = MakeFloat16(umaterial->pbr.emission_factor.value_real);
        }
        else if (umaterial->pbr.emission_factor.value_components > 2)
        {
            amaterial->emissiveFactor[0] = MakeFloat16(umaterial->pbr.emission_factor.value_vec3.x);
            amaterial->emissiveFactor[1] = MakeFloat16(umaterial->pbr.emission_factor.value_vec3.y);
            amaterial->emissiveFactor[2] = MakeFloat16(umaterial->pbr.emission_factor.value_vec3.z);
        }
    }
    
    // Copy local node transforms; SceneBundle_Normalize() will flatten hierarchy and build parent indices.
    u16 numNodes = (u16)uscene->nodes.count;
    fbxScene->numNodes = numNodes;
    
    if (numNodes)
        fbxScene->nodes = (ANode*)AllocZeroTLSFGlobal(numNodes, sizeof(ANode));

    fbxScene->rootNode = aIndexOf(uscene->nodes.data, uscene->root_node, (s32)uscene->nodes.count, sizeof(void*), void_ptr_compare);
    if (fbxScene->rootNode < 0) fbxScene->rootNode = 0;
    fbxScene->scenes[0].numNodes = 1;
    fbxScene->scenes[0].nodes = FixedPow2Allocator_AllocateUninitialized(allocator, sizeof(s32));
    fbxScene->scenes[0].nodes[0] = fbxScene->rootNode;

    for (s32 i = 0; i < numNodes; i++)
    {
        ANode* anode = &fbxScene->nodes[i];
        ufbx_node* unode = uscene->nodes.data[i];
        anode->type = unode->camera ? 1 : 0;
        anode->index = -1;
        anode->parent = -1;
        anode->name = GetNameFromFBX(unode->name, allocator);
        anode->numChildren = (s32)unode->children.count;
        if (anode->numChildren > 0)
            anode->children = FixedPow2Allocator_AllocateUninitialized(allocator, sizeof(s32) * anode->numChildren);
        
        for (s32 j = 0; j < anode->numChildren; j++)
        {
            anode->children[j] = aIndexOf(uscene->nodes.data, unode->children.data[j], (s32)uscene->nodes.count, 8, void_ptr_compare);
            ASSERT(anode->children[j] != -1);
        }
        
        SmallMemCpy(anode->translation, &unode->local_transform.translation.x, sizeof(float3));
        SmallMemCpy(anode->rotation, &unode->local_transform.rotation.x, sizeof(v128f));
        SmallMemCpy(anode->scale, &unode->local_transform.scale.x, sizeof(float3));
        
        if (unode->mesh)
        {
            anode->index = aIndexOf(uscene->meshes.data, unode->mesh, (s32)uscene->meshes.count, 8, void_ptr_compare);
            if (anode->index >= 0 && unode->materials.count > 0)
                fbxScene->meshes[anode->index].primitives[0].material = aIndexOf(uscene->materials.data, unode->materials.data[0], (s32)uscene->materials.count, 8, void_ptr_compare);
        }
        else if (unode->camera)
            anode->index = aIndexOf(uscene->cameras.data, unode->camera, (s32)uscene->cameras.count, 8, void_ptr_compare);
    }
    
    fbxScene->numImages = dynarray_length(images);
    fbxScene->images    = images;
    fbxScene->allocator = allocator;
    fbxScene->scale = scale;
    fbxScene->error = AError_NONE;
    SceneBundle_Normalize(fbxScene);
    AX_LOG("fbx import complete: %s", path);
    ufbx_free_scene(uscene);
#endif // android
    return 1;
}

void SaveSceneImages(SceneBundle* scene, const char* savePath, bool deleteRemaining)
{
    char pathBuf[2048], baseDir[2048], name[512], bdcPath[2048], tmpPath[2048], srcPath[2048];

    // .bdc path
    s32 len = StringLengthSafe(savePath, sizeof(bdcPath));
    SmallMemCpy(bdcPath, savePath, len);
    ChangeExtension(bdcPath, len, "bdc");
    s32 bdcLen = StringLengthSafe(bdcPath, sizeof(bdcPath));
    SmallMemCpy(tmpPath, bdcPath, bdcLen);
    SmallMemCpy(tmpPath + bdcLen, ".tmp", 5);

    AFile file = AFileOpen(tmpPath, AOpenFlag_WriteText);
    GetBaseDir(savePath, baseDir);

    // write count
    s32 n = IntToString(pathBuf, scene->numImages, 0);
    pathBuf[n++] = '\n';
    AFileWrite(pathBuf, n, file, 1);

    basis_encoder_init();

    for (s32 i = 0; i < scene->numImages; i++)
    {
        const char* src = scene->images[i].path;
        if (src == NULL || src[0] == '\0')
        {
            AX_WARN("scene image has no path, writing empty cache entry index:%d", i);
            pathBuf[0] = '\n';
            AFileWrite(pathBuf, 1, file, 1);
            pathBuf[0] = '0';
            pathBuf[1] = '\n';
            AFileWrite(pathBuf, 2, file, 1);
            continue;
        }

        s32 srcLen = (s32)StringLengthSafe(src, sizeof(srcPath) - 1);
        SmallMemCpy(srcPath, src, srcLen);
        srcPath[srcLen] = '\0';

        // write original path
        s32 l = (s32)StringLengthSafe(srcPath, sizeof(pathBuf) - 2);
        SmallMemCpy(pathBuf, srcPath, l);
        pathBuf[l++] = '\n';
        AFileWrite(pathBuf, l, file, 1);

        // build output path = baseDir + name + ".basis"
        s32 baseLen = (s32)StringLengthSafe(baseDir, sizeof(baseDir));
        SmallMemCpy(pathBuf, baseDir, baseLen);

        s32 nameLen = GetFileNameNoExt(srcPath, name);
        SmallMemCpy(pathBuf + baseLen, name, nameLen);
        SmallMemCpy(pathBuf + baseLen + nameLen, ".basis", 7);

        bool isNormal = false;
        bool isMR     = false; // metallic roughness

        for (s32 j = 0; j < scene->numMaterials; j++)
        {
            AMaterial m = scene->materials[j];

            if (m.textures[0].index < scene->numTextures)
                isNormal |= (scene->textures[m.textures[0].index].source == i);

            if (m.metallicRoughnessTexture.index < scene->numTextures)
                isMR |= (scene->textures[m.metallicRoughnessTexture.index].source == i);

            if (m.specularTexture.index < scene->numTextures)
                isMR |= (scene->textures[m.specularTexture.index].source == i);
        }

        // baseColor wins -> it's neither normal nor MR
        for (s32 j = 0; j < scene->numMaterials; j++)
        {
            AMaterial m = scene->materials[j];

            if (m.baseColorTexture.index < scene->numTextures &&
                scene->textures[m.baseColorTexture.index].source == i)
            {
                isNormal = false;
                isMR     = false;
                break;
            }
        }

        s32 type = (isNormal ? 1 : 0) | (isMR ? 2 : 0);
        AX_LOG("output path: %s", pathBuf);

        const s32 quality = 0; // 100 is max
        s32 r = basis_compress_file((const char*)srcPath, (const char*)pathBuf, type, 16, quality, -1);
        if (r) AX_WARN("Failed: %s (%d)\n", srcPath, r);

        n = IntToString(pathBuf, type, 0);
        pathBuf[n++] = '\n';
        AFileWrite(pathBuf, n, file, 1);

        if (deleteRemaining) RemoveFile(srcPath);
    }

    AFileClose(file);
    RemoveFile(bdcPath);
    RenameFile(tmpPath, bdcPath);
}

// result: 1 fine, 2 some file is not exist, 3 not enough images for scene
s32 LoadSceneImages(const char* texturePath, Texture* textures, s32 numImages)
{
    if (numImages <= 0)
        return 1;

    AFile file = AFileOpen(texturePath, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
        return 0;

    char buffer[2048];
    char typeBuffer[64];
    char baseDir[2048];
    char fileName[512];
    char basisPath[2048];
    GetBaseDir(texturePath, baseDir);

    s32 result = 1;
    s32 fileNumImages = AFileReadI32(buffer, sizeof(buffer), file); // First line: numImages
    if (fileNumImages != numImages)
    {
        AX_WARN("basis file num images not equal to requested numImages. file: %d, requested: %d", fileNumImages, numImages);
        numImages = fileNumImages;
        result = 3;
    }

    for (s32 i = 0; i < numImages; i++)
    {
        s32 pathLen = AFileReadLine(buffer, sizeof(buffer), file);
        s32 textureType = AFileReadI32(typeBuffer, sizeof(typeBuffer), file);

        if (pathLen <= 0)
        {
            AX_WARN("basis metadata ended early index:%d, file:%s", i, texturePath);
            result = 3;
            break;
        }

        GetFileNameNoExt(buffer, fileName);
        s32 fileNameLen = (s32)StringLengthSafe(fileName, sizeof(fileName));
        if (fileNameLen <= 0)
        {
            AX_WARN("basis metadata has empty image path index:%d, file:%s", i, texturePath);
            textures[i].handle = NULL;
            textures[i].buffer = NULL;
            textures[i].bufferSize = 0;
            textures[i].type = (u32)textureType;
            result = 3;
            continue;
        }

        s32 baseLen = (s32)StringLengthSafe(baseDir, sizeof(baseDir));
        s32 nameLen = (s32)StringLengthSafe(fileName, sizeof(fileName));
        if (baseLen + nameLen + 7 > (s32)sizeof(basisPath))
        {
            AX_WARN("basis path too long index:%d, file:%s", i, texturePath);
            result = 2;
            continue;
        }

        SmallMemCpy(basisPath, baseDir, baseLen);
        SmallMemCpy(basisPath + baseLen, fileName, nameLen);
        SmallMemCpy(basisPath + baseLen + nameLen, ".basis", 7);

        u64 size = FileSize(basisPath);
        void* mem = SDL_malloc(size);
        
        if (!mem || size == 0)
        {
            const char* reason = size == 0 ? "BasisFileNotExist" : "OSAllocFailed";
            // textures[i] = rCreateTexture(32, 32, buffer, 0, TexFlags_Nearest, reason); // fill with placeholder
            AX_WARN("%s index:%d, fileSize:%llu, path:%s", reason, i, size, basisPath);
            result = 2;
            continue;
        }

        void* basisData = ReadAllFile(basisPath, mem, size);
        s32 isNormal =  textureType & 1;
        s32 isMetallicRoughness = (textureType >> 1) & 1;

        textures[i].type = (u32)textureType;
        textures[i].channels = (textureType & 3u) ? 2u : 4u;
        textures[i].buffer = basisData;
        textures[i].bufferSize = size;
        textures[i].handle = BasisuMakeImage(basisData, size, &textures[i].width, &textures[i].height, &textures[i].format, &textures[i].mipLevels,
                                             (u8)isNormal, (u8)isMetallicRoughness);
        if (!textures[i].handle)
        {
            AX_WARN("basis gpu texture creation failed index:%d, path:%s", i, basisPath);
            result = 2;
        }
    }
    AFileClose(file);
    return result;
}

s32 LoadGLTFCached(const char* path, SceneBundle* scene, Texture* textures, void** outVertexHeapPtr, void** outIndexHeapPtr)
{
    char buffer[1024];
    size_t pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    s32 result = 1;
    if (IsABMLastVersion(buffer)) {
        AX_LOG("asset cache hit: %s", buffer);
        result = LoadSceneBundleBinary(buffer, scene, outVertexHeapPtr, outIndexHeapPtr);
    }
    else if (ParseGLTF(path, scene, 1.0f)) {
        AX_LOG("asset cache rebuild: %s -> %s", path, buffer);
        if (!BakeSceneMeshesAndAnimations(scene, outVertexHeapPtr, outIndexHeapPtr))
        {
            AX_WARN("asset import failed during mesh bake: %s vertices=%d indices=%d", path, scene->totalVertices, scene->totalIndices);
            return 0;
        }
        if (!SaveGLTFBinary(scene, buffer))
        {
            AX_WARN("asset cache save failed: %s", buffer);
            return 0;
        }
        ChangeExtension(buffer, newLen, "bdc");
        SaveSceneImages(scene, buffer, FileHasExtension(path, pathLen,".glb"));
    }
    else {
        AX_WARN("asset import failed: %s", path);
        return 0;
    }
    ChangeExtension(buffer, newLen, "bdc");
    s32 imageResult = LoadSceneImages(buffer, textures, scene->numImages);
    if (imageResult == 0 || imageResult == 3)
    {
        AX_WARN("scene image cache invalid, rebuilding: %s result=%d", buffer, imageResult);
        SaveSceneImages(scene, buffer, false);
        imageResult = LoadSceneImages(buffer, textures, scene->numImages);
    }
    else if (imageResult == 2)
    {
        AX_WARN("scene image cache has missing basis files, keeping metadata: %s", buffer);
    }
    return result && (imageResult != 0);
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                            Binary Save                                   */
/*//////////////////////////////////////////////////////////////////////////*/

// ZSTD_CCtx* zstdCompressorCTX = NULL;
// 79: geometry offsets and index values are stored bundle relative, the
// runtime placement comes from the mega buffer range allocators
const s32 ABMMeshVersion = 79;

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
    s32 nameLen = str ? StringLength(str) : 0;
    AFileWrite(&nameLen, sizeof(s32), file, 1);
    if (str) AFileWrite(str, (uint64_t)(nameLen + 1), file, 1);
}

s32 SaveGLTFBinary(const SceneBundle* gltf, const char* path)
{
#if !AX_GAME_BUILD
    AX_LOG("abm save: %s version=%d meshes=%d nodes=%d skins=%d animations=%d",
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

    // layout: [deflate output | delta indices]
    // deflate output must fit in max(allVertexSize, allIndexSize) bytes
    // delta indices need allIndexSize bytes
    u64 deflateSlotSize = Maxu64(allVertexSize, allIndexSize);
    u64 tempSize        = deflateSlotSize + allIndexSize;
    char* compressedBuffer = ArenaPushGlobal(tempSize);

    char* deflateOutput = compressedBuffer;
    u32*  deltaPtr      = (u32*)(compressedBuffer + deflateSlotSize);

    static struct sdefl sdfl;
    u64 afterCompSize = zsdeflate(&sdfl, deflateOutput, gltf->allVertices, allVertexSize, 5);
    AFileWrite(&afterCompSize, sizeof(u64), file, 1);
    AFileWrite(deflateOutput, afterCompSize, file, 1);

    DeltaEncodingU32(gltf->allIndices, gltf->totalIndices, deltaPtr, bakedVertexBase);
    afterCompSize = zsdeflate(&sdfl, deflateOutput, deltaPtr, allIndexSize, 5);
    AFileWrite(&afterCompSize, sizeof(u64), file, 1);
    AFileWrite(deflateOutput, afterCompSize, file, 1);

    ArenaPopGlobal(tempSize);
    // Cache stores runtime-ready mesh/animation data. Morph target animation is intentionally not serialized yet.

    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh mesh = gltf->meshes[i];
        WriteGLTFString(mesh.name, file);
        
        AFileWrite(&mesh.numPrimitives  , sizeof(s32), file, 1);
        
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
            AFileWrite(primitive->lodAnimatedVertexOffset, sizeof(s32) * 4, file, 1); // already bundle local
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

void ReadGLTFString(char** str, AFile file, FixedPow2Allocator* stringAllocator)
{
    s32 nameLen = 0;
    AFileRead(&nameLen, sizeof(s32), file, 1);
    if (nameLen)    
    {
        *str = FixedPow2Allocator_AllocateUninitialized(stringAllocator, nameLen + 1);
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
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 1024);

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

        u64 deflateSlotSize = Maxu64(allVertexSize, allIndexSize);
        u64 tempSize        = deflateSlotSize + allIndexSize;
        char* compressedBuffer = ArenaPushGlobal(tempSize);

        u64 compressedSize;

        AFileRead(&compressedSize, sizeof(u64), file, 1);
        AFileRead(compressedBuffer, compressedSize, file, 1);
        zsinflate(gltf->allVertices, allVertexSize, compressedBuffer, compressedSize);

        AFileRead(&compressedSize, sizeof(u64), file, 1);
        AFileRead(compressedBuffer, compressedSize, file, 1);
        zsinflate(gltf->allIndices, allIndexSize, compressedBuffer, compressedSize);
        // index values were saved relative to the bundle's vertex base, the
        // prefix sum seed rebases the whole stream to the allocated range
        PrefixSumU32fInplace(gltf->allIndices, gltf->totalIndices, vertexBase);

        ArenaPopGlobal(tempSize);

        Rendering_QueueGeometryUpload(isSkined ? GeometryBuffer_SkinnedVertex : GeometryBuffer_SurfaceVertex,
                                      vertexBase, vertexBase + (u32)gltf->totalVertices);
        Rendering_QueueGeometryUpload(GeometryBuffer_Index, indexBase, indexBase + (u32)gltf->totalIndices);
    }
    
    char* currVertices = (char*)gltf->allVertices;
    char* currIndices = (char*)gltf->allIndices;
    
    if (gltf->numMeshes > 0) gltf->meshes = AllocZeroTLSFGlobal(gltf->numMeshes, sizeof(AMesh));
    s32 totalPrimitives = 0;
    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh* mesh = &gltf->meshes[i];
        ReadGLTFString(&mesh->name, file, allocator);
        
        AFileRead(&mesh->numPrimitives, sizeof(s32), file, 1);
        mesh->primitiveOffset = totalPrimitives;
        totalPrimitives += mesh->numPrimitives;

        mesh->primitives = FixedPow2Allocator_AllocateUninitialized(allocator, sizeof(APrimitive) * mesh->numPrimitives);
        
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
            AFileRead(primitive->lodAnimatedVertexOffset, sizeof(s32) * 4, file, 1);

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
    
    if (gltf->numNodes > 0) gltf->nodes = AllocZeroTLSFGlobal(gltf->numNodes, sizeof(ANode));
    
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
            node->children = FixedPow2Allocator_AllocateUninitialized(allocator, sizeof(s32) * (node->numChildren+1));
            AFileRead(node->children, sizeof(s32) * node->numChildren, file, 1);
        }
        
        ReadGLTFString(&node->name, file, allocator);
    }
    
    if (gltf->numMaterials > 0) gltf->materials = AllocZeroTLSFGlobal(gltf->numMaterials, sizeof(AMaterial));
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
    
    if (gltf->numTextures > 0) gltf->textures = (ATexture*)AllocZeroTLSFGlobal(gltf->numTextures, sizeof(ATexture));
    for (s32 i = 0; i < gltf->numTextures; i++)
    {
        ATexture* texture = &gltf->textures[i];
        AFileRead(&texture->sampler, sizeof(s32), file, 1);
        AFileRead(&texture->source, sizeof(s32), file, 1);
        ReadGLTFString(&texture->name, file, allocator);
    }
    if (gltf->numImages > 0) gltf->images = (AImage*)AllocZeroTLSFGlobal(gltf->numImages, sizeof(AImage));
    for (s32 i = 0; i < gltf->numImages; i++)
    {
        ReadGLTFString(&gltf->images[i].path, file, allocator);
    }
    
    if (gltf->numSamplers > 0) gltf->samplers = (ASampler*)AllocZeroTLSFGlobal(gltf->numSamplers, sizeof(ASampler));
    for (s32 i = 0; i < gltf->numSamplers; i++)
    {
        AFileRead(&gltf->samplers[i], sizeof(ASampler), file, 1);
    }
    
    if (gltf->numCameras > 0) gltf->cameras = (ACamera*)AllocZeroTLSFGlobal(gltf->numCameras, sizeof(ACamera));
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
    
    if (gltf->numScenes > 0) gltf->scenes = (AScene*)AllocZeroTLSFGlobal(gltf->numScenes, sizeof(AScene));
    for (s32 i = 0; i < gltf->numScenes; i++)
    {
        AScene* scene = &gltf->scenes[i];
        ReadGLTFString(&scene->name, file, allocator);
        AFileRead(&scene->numNodes, sizeof(s32), file, 1);
        scene->nodes = FixedPow2Allocator_AllocateUninitialized(allocator, scene->numNodes * sizeof(s32));
        AFileRead(scene->nodes, sizeof(s32) * scene->numNodes, file, 1);
    }

    if (gltf->numSkins > 0) gltf->skins = (ASkin*)AllocZeroTLSFGlobal(gltf->numSkins, sizeof(ASkin));
    for (s32 i = 0; i < gltf->numSkins; i++)
    {
        ASkin* skin = &gltf->skins[i];
        AFileRead(&skin->skeleton, sizeof(s32), file, 1);
        AFileRead(&skin->numJoints, sizeof(s32), file, 1);
        skin->inverseBindMatrices = (f32*)AllocateTLSFGlobal(sizeof(mat4x4) * skin->numJoints);
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, skin->numJoints * sizeof(s32));
        AFileRead(skin->inverseBindMatrices, sizeof(mat4x4) * skin->numJoints, file, 1);
        AFileRead(skin->joints, sizeof(s32) * skin->numJoints, file, 1);
    }

    s32 totalAnimSamplerInput = 0;
    AFileRead(&totalAnimSamplerInput, sizeof(s32), file, 1);
    f32* currSamplerInput = NULL;
    v128f* currSamplerOutput = NULL;

    if (totalAnimSamplerInput) {
        currSamplerInput  = (f32*)AllocZeroTLSFGlobal(totalAnimSamplerInput, sizeof(float));
        currSamplerOutput = (v128f*)AllocZeroTLSFGlobal(totalAnimSamplerInput, sizeof(v128f));
        AFileRead(currSamplerInput, sizeof(float) * totalAnimSamplerInput, file, 1);
        AFileRead(currSamplerOutput, sizeof(v128f) * totalAnimSamplerInput, file, 1);
    }

    if (gltf->numAnimations) gltf->animations = AllocZeroTLSFGlobal(gltf->numAnimations, sizeof(AAnimation));
    for (s32 i = 0; i < gltf->numAnimations; i++)
    {
        AAnimation* animation = &gltf->animations[i];

        AFileRead(&animation->numSamplers, sizeof(s32), file, 1);
        AFileRead(&animation->numChannels, sizeof(s32), file, 1);
        AFileRead(&animation->duration, sizeof(float), file, 1);
        AFileRead(&animation->speed, sizeof(float), file, 1);
        ReadGLTFString(&animation->name, file, allocator);
        animation->channels = AllocateTLSFGlobal(animation->numChannels * sizeof(AAnimChannel));
        AFileRead(animation->channels, sizeof(AAnimChannel) * animation->numChannels, file, 1);
        animation->samplers = AllocateTLSFGlobal(animation->numSamplers * sizeof(AAnimSampler));

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

    AFileClose(file);
    gltf->allocator = allocator;
    AX_LOG("abm load complete: %s meshes=%d nodes=%d skins=%d animations=%d",
           path, gltf->numMeshes, gltf->numNodes, gltf->numSkins, gltf->numAnimations);
    return 1;
}
