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

#define SINFL_IMPLEMENTATION
#define SDEFL_IMPLEMENTATION
#include "Extern/sdefl.h"
#include "Extern/sinfl.h"
#include "Extern/dynarray.h"

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

s32 LoadFBX(const u8* path, SceneBundle* fbxScene, f1 scale)
{
#if !AX_GAME_BUILD
    ufbx_load_opts opts = { 0 };
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
    
    uscene = ufbx_load_file(path, &opts, &error);
    
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
    fbxScene->numScenes     = 0; // todo
    
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 2048);
    
    u64 totalIndices  = 0, totalVertices = 0;
    for (s32 i = 0; i < fbxScene->numMeshes; i++)
    {
        ufbx_mesh* umesh = uscene->meshes.data[i];
        totalIndices  += umesh->num_triangles * 3;
        totalVertices += umesh->num_vertices;
    }
    
    fbxScene->allVertices = AllocAligned(sizeof(ASkinedVertex) * totalVertices, 4);
    fbxScene->allIndices  = AllocAligned(sizeof(u32) * totalIndices, 4);
    
    if (fbxScene->numMeshes) fbxScene->meshes = (AMesh*)AllocZeroTLSFGlobal(fbxScene->numMeshes, sizeof(AMesh));
    
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
        
        for (s32 j = 0; j < primitive->numVertices; j++)
        {
            // SmallMemCpy(&currentVertex[j].position.x, &umesh->vertex_position.values.data[j], sizeof(float) * 3);
            if (umesh->vertex_uv.exists)
            {
                currentVertex[j].texCoord = Float2ToHalf2((f1*)(umesh->vertex_uv.values.data + j));
            }
            if (umesh->vertex_normal.exists) 
            {
                // currentVertex[j].qtangentXYf16 = PackVec3XYZ10BitToInt(Vec3Load((f1*)(umesh->vertex_normal.values.data + j)));
            }
            if (umesh->vertex_tangent.exists)
            {
                v128f tangent = Vec3Load((f1*)(umesh->vertex_tangent.values.data + j));
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
                    f1  weight = skinWeight.weight;
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
        skin->inverseBindMatrices = AllocateTLSFGlobal(numJoints * sizeof(m44));
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, (sizeof(s32) + 1) * numJoints);
    
        m44* matrices = (m44*)skin->inverseBindMatrices;
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
            u8* buffer = FixedPow2Allocator_AllocateUninitialized(allocator, 512);
            MemsetZero(buffer, 512);
            s32 pathLen = StringLengthSafe(path, 512);
            SmallMemCpy(buffer, path, pathLen);
            
            u8* fbxPath = PathGoBackwards(buffer, pathLen, false);
            // concat: FbxPath/TextureName
            SmallMemCpy(fbxPath, utexture->name.data, utexture->name.length); 
            SmallMemCpy(fbxPath + utexture->name.length, ".png", 4); // FbxPath/TextureName.png
            AFile file = AFileOpen(buffer, AOpenFlag_WriteBinary);
            AFileWrite(utexture->content.data, utexture->content.size, file, 1);
            atexture->source = dynarray_length(images);
            dynarray_push(images, (AImage) { buffer });
            dynarray_push(images, (AImage){ buffer });
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
    
    // copy nodes
    u16 numNodes = (u16)uscene->nodes.count;
    fbxScene->numNodes = numNodes;
    
    if (numNodes) {
        fbxScene->nodes = (ANode*)AllocZeroTLSFGlobal(numNodes * 4, sizeof(ANode));
    }

    for (s32 i = 0; i < numNodes; i++)
    {
        ANode* anode = &fbxScene->nodes[i];
        ufbx_node* unode = uscene->nodes.data[i];
        anode->type = unode->camera != NULL;
        anode->name = GetNameFromFBX(unode->name, allocator);
        anode->numChildren = (s32)unode->children.count;
        anode->children = FixedPow2Allocator_AllocateUninitialized(allocator, (sizeof(s32) + 1 ) * anode->numChildren);
        
        for (s32 j = 0; j < anode->numChildren; j++)
        {
            anode->children[j] = aIndexOf(uscene->nodes.data, unode->children.data[j], (s32)uscene->nodes.count, 8, void_ptr_compare);
            ASSERT(anode->children[j] != -1);
        }
        
        SmallMemCpy(anode->translation, &unode->world_transform.translation.x, sizeof(f3));
        SmallMemCpy(anode->rotation, &unode->world_transform.rotation.x, sizeof(v128f));
        SmallMemCpy(anode->scale, &unode->world_transform.scale.x, sizeof(f3));
        
        if (anode->type == 0)
        {
            anode->index = aIndexOf(uscene->meshes.data, unode->mesh, (s32)uscene->meshes.count, 8, void_ptr_compare);
            if (unode->materials.count > 0)
                fbxScene->meshes[anode->index].primitives[0].material = aIndexOf(uscene->materials.data, unode->materials.data[0], (s32)uscene->materials.count, 8, void_ptr_compare);
        }
        else
            anode->index = aIndexOf(uscene->cameras.data, unode->camera, (s32)uscene->cameras.count, 8, void_ptr_compare);
    }
    
    fbxScene->numImages = dynarray_length(images);
    fbxScene->images    = images;
    fbxScene->allocator = allocator;
    ufbx_free_scene(uscene);
#endif // android
    return 1;
}


/*//////////////////////////////////////////////////////////////////////////*/
/*                            Vertex Load                                   */
/*//////////////////////////////////////////////////////////////////////////*/


static void JointsForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    // convert whatever joi32 format to rgb8u
    const u8* joints      = (const u8*)primitive->vertexAttribs[AAttribIdx_JOINTS];
    s32 jointSize   = GraphicsTypeToSize(primitive->jointType);
    s32 jointOffset = Maxi32((s32)(primitive->jointStride - (jointSize * primitive->jointCount)), 0); // stride - sizeof(rgbau16)
            
    if (joints == NULL) 
    {
        AX_LOG("no joints in skinned mesh renderer");
        for (s32 j = 0; j < primitive->numVertices; j++)
            currVertex[j].joints = 0;
        return;
    }

    for (s32 j = 0; j < primitive->numVertices; j++)
    {
        // Combine 4 indices into one integer to save space
        u32 packedJoints = 0u;
        // iterate over joi32 indices, most of the time 4 indices
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
        joints += jointOffset; // stride offset at the end of the struct
    }
}

static void WeightsForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    const u8* weights = (const u8*)primitive->vertexAttribs[AAttribIdx_WEIGHTS];
            
    // size and offset in bytes
    s32 weightSize   = GraphicsTypeToSize(primitive->weightType);
    s32 weightOffset = Maxi32((s32)(primitive->weightStride - (weightSize * primitive->jointCount)), 0);

    if (weights == NULL)
    {
        AX_LOG("no joints in primitive");
        for (s32 j = 0; j < primitive->numVertices; j++)
            currVertex[j].weights = 1023;
        return;
    }
 
    if (weightSize == 4) // if float, pack it directly
    {
        for (s32 j = 0; j < primitive->numVertices; j++)
        {
            u32 packedWeights = PackXY11Z10UnormToU32(Vec3Load((f1*)weights));
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
            const f1 packMax[3] = { 1023.0f, 1023.0f, 511.0f };
            // don't parse w, we will get it from xyz
            for (s32 k = 0, shift = 0; k < primitive->jointCount && k < 3; k++, shift += 11)
            {
                u32 jointWeight = 0u;
                SmallMemCpy(&jointWeight, weights, weightSize);
                f1 weightMax = (f1)((1u << (weightSize * 8)) - 1);
                f1 norm = (f1)jointWeight / weightMax; // divide by 255 or 65535
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
        // we are combining all vertices and indices into one buffer, that's why we have to add vertex cursor
        currIndices[i] = index + vertexCursor; 
        beforeCopy += indexSize;
    }
}

static void VerticesForPrimitive(APrimitive* primitive, ASkinedVertex* currVertex)
{
    // https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
    primitive->vertices = currVertex;
    const f3* positions   = (const f3*)primitive->vertexAttribs[AAttribIdx_POSITION];
    const f2* texCoords   = (const f2*)primitive->vertexAttribs[AAttribIdx_TEXCOORD_0];
    const f3* normals     = (const f3*)primitive->vertexAttribs[AAttribIdx_NORMAL];
    const v128f* tangents = (const v128f*)primitive->vertexAttribs[AAttribIdx_TANGENT];

    for (s32 v = 0; v < primitive->numVertices; v++)
    {
        v128f tangent = tangents  ? tangents[v]  : VecZero();
        f2 texCoord   = texCoords ? texCoords[v] : (f2){0.0f, 0.0f};
        f3 normal     = normals   ? normals[v]   : (f3){0.5f, 0.5f, 0.0};
        f3 position   = positions[v];

        Float4ToHalf4((h1*)&currVertex[v].positionXY, &positions[v].x);
        currVertex[v].texCoord      = Float2ToHalf2(&texCoord.x);
        currVertex[v].qtangentXYF16 = PackXY11Z10SnormToU32(Vec3Load(&normal.x));
        currVertex[v].qtangentZWF16 = PackXY11Z10SnormToU32(tangent);
    }
}

static void BoundsForPrimitive(APrimitive* primitive)
{
    const f3* positions = (const f3*)primitive->vertexAttribs[AAttribIdx_POSITION];
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
    // SDL_Log("min: %f, %f, %f ", primitive->min[0], primitive->min[1], primitive->min[2]);
    // SDL_Log("max: %f, %f, %f ", primitive->max[0], primitive->max[1], primitive->max[2]);
}

static void PrintMatrix(m44 mtx)
{
    SDL_Log("mtx[3]: %f, %f, %f, %f", mtx.m[3][0], mtx.m[3][1], mtx.m[3][2], mtx.m[3][3]);
    SDL_Log("mtx[2]: %f, %f, %f, %f", mtx.m[2][0], mtx.m[2][1], mtx.m[2][2], mtx.m[2][3]);
    SDL_Log("mtx[1]: %f, %f, %f, %f", mtx.m[1][0], mtx.m[1][1], mtx.m[1][2], mtx.m[1][3]);
    SDL_Log("mtx[0]: %f, %f, %f, %f", mtx.m[0][0], mtx.m[0][1], mtx.m[0][2], mtx.m[0][3]);
    SDL_Log("--------------------------------------");
}

static void GetGLTFAnimations(SceneBundle* gltf)
{
    if (gltf->skins == NULL)
        return;

    s32 rootIndex = Prefab_FindAnimRootNodeIndex(gltf);
    f1 rootScale = gltf->nodes[rootIndex].scale[1];
    v128f rootScaleMul = VecSetR(rootScale, rootScale, rootScale, 1.0f);

    for (s32 s = 0; s < gltf->numSkins; s++)
    {
        ASkin* skin = &gltf->skins[s];
        m44* inverseBindMatrices = AllocateTLSFGlobal(skin->numJoints * sizeof(m44));
        SmallMemCpy(inverseBindMatrices, skin->inverseBindMatrices, sizeof(m44) * skin->numJoints);
        skin->inverseBindMatrices = (f1*)inverseBindMatrices;
        
        for (s32 i = 0; i < skin->numJoints; i++)
        {
            m44 inv = inverseBindMatrices[i];
            if (i != rootIndex)
            {
                inv.r[0] = VecNorm(inv.r[0]);
                inv.r[1] = VecNorm(inv.r[1]);
                inv.r[2] = VecNorm(inv.r[2]);
                inv.r[3] = VecMul(inv.r[3], rootScaleMul); // VecMul(inv.r[3], VecSetR(0.01f, 0.01f, 0.01f, 1.0f));
                inverseBindMatrices[i] = inv;
            }
        }
    }

    if (gltf->numAnimations <= 0)
        return;
    
    s32 totalSamplerInput = 0;
    for (s32 a = 0; a < gltf->numAnimations; a++)
        for (s32 s = 0; s < gltf->animations[a].numSamplers; s++)
            totalSamplerInput += gltf->animations[a].samplers[s].count;
        
    f1* currSampler = (f1*)AllocZeroTLSFGlobal(totalSamplerInput, 4);
    v128f* currOutput = (v128f*)AllocZeroTLSFGlobal(totalSamplerInput, sizeof(v128f));

    for (s32 a = 0; a < gltf->numAnimations; a++)
    {
        for (s32 s = 0; s < gltf->animations[a].numSamplers; s++)
        {
            AAnimSampler* sampler = &gltf->animations[a].samplers[s];
            SmallMemCpy(currSampler, sampler->input, sampler->count * sizeof(f1));
            sampler->input = currSampler;
            currSampler += sampler->count;
                
            if (sampler->interpolation == ASamplerInterpolation_CubicSpline)
                AX_WARN("sampler cubic spline not supported");
                
            if (sampler->inputType != AComponentType_FLOAT)
                AX_WARN("unsupported sampler input type: %d", sampler->inputType);
                
            if (sampler->outputType != AComponentType_FLOAT)
                AX_WARN("unsupported sampler output type: %d", sampler->outputType);

            if (sampler->numComponent != 4)
                AX_WARN("anim sampler num components has to be 4. its: %d", sampler->numComponent);

            if (sampler->outputType != AComponentType_FLOAT)
                AX_WARN("unsupported sampler output type: %d", sampler->outputType);

            for (s32 i = 0; i < sampler->count; i++)
            {
                SmallMemCpy(currOutput + i, sampler->output + (i * sampler->numComponent), sizeof(f1) * sampler->numComponent);
                currOutput[i] = VecLoad(sampler->output + (i * sampler->numComponent));
                if (sampler->numComponent == 3) currOutput[i] = VecSetW(currOutput[i], 0.0f);
            }

            sampler->output = (f1*)currOutput;
            currOutput += sampler->count;
        }
    }
}

s32 CreateVerticesIndices(SceneBundle* gltf)
{
    AMesh* meshes    = gltf->meshes;
    u32 vertexCursor = gGFX.NumVertices;
    u32 indexCursor  = gGFX.NumIndices;

    if ((gGFX.NumVertices + gltf->totalVertices) > MAX_VERTEX || 
        (gGFX.NumIndices  + gltf->totalIndices ) > MAX_INDEX)
        return 0;

    gGFX.NumVertices += gltf->totalVertices;
    gGFX.NumIndices  += gltf->totalIndices;

    gltf->allVertices = gGFX.VertexBuffer + vertexCursor;
    gltf->allIndices  = gGFX.IndexBuffer  + indexCursor;
    
    ASkinedVertex* currVertex = (ASkinedVertex*)gltf->allVertices;
    ASkin* skin = gltf->skins;
    u32* currIndices = (u32*)gltf->allIndices;
    
    for (s32 m = 0; m < gltf->numMeshes; ++m)
    {
        // get number of vertex, getting first attribute count because all of the others are same
        AMesh mesh = meshes[m];
        for (s32 p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];  
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

    for (s32 i = 0; i < gltf->numNodes; i++)
    {
        ANode inputNode = gltf->nodes[i];
        SDL_Log("node pos: %f, %f, %f, scale: %f", inputNode.translation[0], inputNode.translation[1], inputNode.translation[2], inputNode.scale[1]);
    }

    GetGLTFAnimations(gltf);
    FreeGLTFBuffers(gltf);
    return 1;
}


void SaveSceneImages(SceneBundle* scene, const u8* savePath, bool deleteRemaining)
{
    u8 command[2048];
    u8 outputDir[2048] = { 0 };
    
    s32 pathLen = StringLengthSafe(savePath, sizeof(outputDir));
    
    // write texture description into .bdc file
    SmallMemCpy(outputDir, savePath, pathLen);
    ChangeExtension(outputDir, pathLen, "bdc");
    AFile file = AFileOpen(outputDir, AOpenFlag_WriteText);
    
    s32 numDigits = IntToString(command, (s64)scene->numImages, 0);
    command[numDigits] = '\n';
    AFileWrite(command, numDigits + 1, file, 1); // num textures 

    for (s32 i = 0; i < scene->numImages; i++)
    {
        const u8* path = scene->images[i].path;
        s32 len = (s32)StringLengthSafe(path, sizeof(outputDir)-2);
        SmallMemCpy(outputDir, path, len);
        outputDir[len] = '\n';
        AFileWrite(outputDir, len + 1, file, 1); // write original texture path
     
        PathGoBackwards(outputDir, pathLen, true);
        
        s32 textureType = 0;
        for (s32 j = 0; j < scene->numMaterials; j++)
        {
            AMaterial material = scene->materials[j];
            if (material.textures[0].index < scene->numTextures)
            textureType |= scene->textures[material.textures[0].index].source == i;
            
            if (material.baseColorTexture.index < scene->numTextures)
            // if an normal map used as base color, unmark it. (causing problems on sponza)
            textureType &= ~(scene->textures[material.baseColorTexture.index].source == i);
            
            if (material.metallicRoughnessTexture.index < scene->numTextures)
            textureType |= (scene->textures[material.metallicRoughnessTexture.index].source == i) << 1;
            // mixamo animations are exporting specular instead of metallic roughness, 
            // in our engine we don't use specular but using metallic roughess with our engine specular means metallic roughness
            if (material.specularTexture.index < scene->numTextures)
            textureType |= (scene->textures[material.specularTexture.index].source == i) << 1;
        }

        s32 isNormal = (textureType & 1) != 0;
        s32 isMetallicRoughness = (textureType & 2) != 0;

        // choose compression format
        const char* formatFlag = isMetallicRoughness ? "-etc1s" : "-uastc";

        snprintf(command, sizeof(command),
                 "Extern\\basis_universal\\basisu.exe \"%s\" %s -basis -mipmap -mip_smallest 256 %s -output_path \"%s\"",
                 path,
                 formatFlag,
                 isNormal ? " -normal_map" : "",
                 outputDir);

        // compress using basis universal
        s32 result = system(command);
        if (result != 0)
        {
            printf("Failed to compress: %s\n", path);
        }

        // write texture type as text
        s32 numDigits = IntToString(outputDir, textureType, 0);
        outputDir[numDigits] = '\n';
        AFileWrite(outputDir, numDigits + 1, file, 1); // num textures 
        if (deleteRemaining) RemoveFile(path);
    }

    AFileClose(file);
}

// result: 1 fine, 2 some file is not exist, 3 not enough images for scene
s32 LoadSceneImages(const u8* texturePath, Texture* textures, s32 numImages)
{
    if (numImages <= 0)
        return 1;

    AFile file = AFileOpen(texturePath, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
        return 0;

    u8 buffer[2048];

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
        ChangeExtension(buffer, pathLen, "basis");

        u64 size = FileSize(buffer);
        void* mem = ArenaPushGlobal(size);
        
        if (!mem || size == 0)
        {
            const char* reason = size == 0 ? "BasisFileNotExist" : "ArenaNotEnough";
            // textures[i] = rCreateTexture(32, 32, buffer, 0, TexFlags_Nearest, reason); // fill with placeholder
            AX_WARN("%s index:%d, fileSize:%d, path:%s", reason, i, size, texturePath);
            result = 2;
            continue;
        }

        void* basisData = ReadAllFile(buffer, mem, size);
        s32 textureType = AFileReadI32(buffer, sizeof(buffer), file);

        s32 isNormal =  textureType & 1;
        s32 isMetallicRoughness = (textureType >> 1) & 1;

        textures[i].handle = BasisuMakeImage(basisData, size, &textures[i].width, &textures[i].height, &textures[i].format,
                                             (u8)isNormal, (u8)isMetallicRoughness);
        ArenaPopGlobal(size);
    }
    return result;
}

s32 LoadGLTFCached(const char* path, SceneBundle* scene, Texture* textures)
{
    char buffer[1024];
    size_t pathLen = StringLength(path);
    MemCpy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    s32 result = 1;
    if (FileExist(buffer)) {
        result = LoadSceneBundleBinary(buffer, scene);
    }
    else if (ParseGLTF2(path, scene, 1.0f)) {
        CreateVerticesIndices(scene);
        SaveGLTFBinary(scene, buffer);
        ChangeExtension(buffer, newLen, "bdc");
        SaveSceneImages(scene, buffer, FileHasExtension(path, pathLen,".glb"));
    }
    else return 0;
    ChangeExtension(buffer, newLen, "bdc");
    return result && (LoadSceneImages(buffer, textures, scene->numImages) != 0);
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
        
            f1 resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, 
                                                          (const u32*)primitive.indices, 
                                                          numIndices, 
                                                          (const f1*)sceneBundle->allVertices, 
                                                          (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex),
                                                          NULL,
                                                          numIndices - (numIndices >> 1), 
                                                          0.04f,
                                                          &resultError);
            primitive.numIndicesLOD50 = numSimplified;
            MemCpy(primitive.lodIndices50, indicesLod0, numSimplified * sizeof(u32));
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
        
            f1 resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, 
                                                          (const u32*)primitive.indices, 
                                                          numIndices, 
                                                          (const f1*)sceneBundle->allVertices, 
                                                          (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex),
                                                          NULL,
                                                          numIndices - (numIndices >> 2), 
                                                          0.04f,
                                                          &resultError);
            primitive.numIndicesLOD75 = numSimplified;
            MemCpy(primitive.lodIndices75, indicesLod0, numSimplified * sizeof(u32));
            ArenaPopGlobal(numSimplified * sizeof(s32));
        }
    }
}

void OptimizeMesh(const SceneBundle* gltf)
{
    int* remap = ArenaAllocGlobal(gltf->totalIndices * sizeof(s32));
    size_t totalVertices = meshopt_generateVertexRemap(remap,
                                                       (const u32 *)gltf->allIndices,
                                                       (size_t)gltf->totalIndices,
                                                       gltf->allVertices,
                                                       (size_t)gltf->totalVertices,
                                                       sizeof(ASkinedVertex));

    int* temp = ArenaAllocGlobal(gltf->totalIndices * sizeof(s32));
    meshopt_remapIndexBuffer(temp, gltf->allIndices, (size_t)gltf->totalIndices, remap);

    ASkinedVertex* vertexBufferNew = ArenaAllocGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    meshopt_remapVertexBuffer(vertexBufferNew,
                              gltf->allVertices,
                              (size_t)gltf->totalVertices,
                              sizeof(ASkinedVertex),
                              remap);

    MemSet(gltf->allVertices, 0, (size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    MemSet(gltf->allIndices , 0, (size_t)gltf->totalIndices * sizeof(s32));
    
    MemCpy(gltf->allIndices , temp, (size_t)gltf->totalIndices * sizeof(s32));
    MemCpy(gltf->allVertices, vertexBufferNew, (size_t)totalVertices * sizeof(ASkinedVertex));
    
    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(s32)); // remap
    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(s32)); // temp
    ArenaPopGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex)); // vertexBufferNew

    meshopt_optimizeVertexCache(gltf->allIndices , gltf->allIndices, gltf->totalIndices, (size_t)totalVertices);
    meshopt_optimizeVertexFetch(gltf->allVertices, gltf->allIndices, gltf->totalIndices,
                                gltf->allVertices, (size_t)totalVertices, sizeof(ASkinedVertex));
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                            Binary Save                                   */
/*//////////////////////////////////////////////////////////////////////////*/

// ZSTD_CCtx* zstdCompressorCTX = NULL;
const s32 ABMMeshVersion = 42;

u8 IsABMLastVersion(const u8* path)
{
    if (!FileExist(path))
        return false;
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (AFileSize(file) < sizeof(u16) * 16) 
        return false;
    s32 version = 0;
    AFileRead(&version, sizeof(s32), file, 1);
    u64 hex;
    AFileRead(&hex, sizeof(u64), file, 1);
    AFileClose(file);
    return version == ABMMeshVersion && hex == 0xABFABF;
}

static void WriteAMaterialTexture(GLTFTexture texture, AFile file)
{
    u64 data = texture.scale; data <<= sizeof(uint16_t) * 8;
    data |= texture.strength;      data <<= sizeof(uint16_t) * 8;
    data |= texture.index;         data <<= sizeof(uint16_t) * 8;
    data |= texture.texCoord;
    
    AFileWrite(&data, sizeof(u64), file, 1);
}

static void WriteGLTFString(const char* str, AFile file)
{
    s32 nameLen = str ? StringLength(str) : 0;
    AFileWrite(&nameLen, sizeof(s32), file, 1);
    if (str) AFileWrite(str, nameLen + 1, file, 1);
}

s32 SaveGLTFBinary(const SceneBundle* gltf, const u8* path)
{
#if !AX_GAME_BUILD
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
    u16 isSkined = (u16)(gltf->skins != NULL);
    AFileWrite(&isSkined, sizeof(u16), file, 1);
    
    OptimizeMesh(gltf);

    AFileWrite(&gltf->totalIndices, sizeof(s32), file, 1);
    AFileWrite(&gltf->totalVertices, sizeof(s32), file, 1);
    
    u64 vertexSize = isSkined ? sizeof(ASkinedVertex) : sizeof(AVertex);
    u64 allVertexSize = vertexSize * (u64)gltf->totalVertices;
    u64 allIndexSize = (u64)gltf->totalIndices * sizeof(u32);
    
    // Compress and write, vertices and indices
    u64 compressedSize = (u64)(allVertexSize * 0.9);
    char* compressedBuffer = ArenaPushGlobal(compressedSize); // global_arena.buf + global_arena.curr_offset;
    
    struct sdefl sdfl;
    size_t afterCompSize = zsdeflate(&sdfl, compressedBuffer, gltf->allVertices, allVertexSize, 5);
    AFileWrite(&afterCompSize, sizeof(u64), file, 1);
    AFileWrite(compressedBuffer, afterCompSize, file, 1);
    
    afterCompSize = zsdeflate(&sdfl, compressedBuffer, gltf->allIndices, allIndexSize, 5);
    AFileWrite(&afterCompSize, sizeof(u64), file, 1);
    AFileWrite(compressedBuffer, afterCompSize, file, 1);
    // DeAllocateTLSFGlobal(compressedBuffer);
    // Note: anim morph targets aren't saved

    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh mesh = gltf->meshes[i];
        WriteGLTFString(mesh.name, file);
        
        AFileWrite(&mesh.numPrimitives  , sizeof(s32), file, 1);
        
        for (s32 j = 0; j < mesh.numPrimitives; j++)
        {
            APrimitive* primitive = &mesh.primitives[j];
            AFileWrite(&primitive->attributes , sizeof(s32), file, 1);
            AFileWrite(&primitive->indexType  , sizeof(s32), file, 1);
            AFileWrite(&primitive->numIndices , sizeof(s32), file, 1);
            AFileWrite(&primitive->numVertices, sizeof(s32), file, 1);
            AFileWrite(&primitive->indexOffset, sizeof(s32), file, 1);
            AFileWrite(&primitive->jointType  , sizeof(u16), file, 1);
            AFileWrite(&primitive->jointCount , sizeof(u16), file, 1);
            AFileWrite(&primitive->jointStride, sizeof(u16), file, 1);
            AFileWrite(&primitive->material   , sizeof(u16), file, 1);
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
        AFileWrite(skin.inverseBindMatrices, sizeof(m44) * skin.numJoints, file, 1);
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
            AFileWrite(&animation.samplers[j].interpolation, sizeof(float), file, 1);
        }
    }
    
    AFileClose(file);
    ArenaPopGlobal(compressedSize);
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
        (*str)[nameLen + 1] = 0;
    }}

s32 LoadSceneBundleBinary(const u8* path, SceneBundle* gltf)
{
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
    {
        perror("Failed to open file for writing");
        return 0;
    }
    
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 1024);

    s32 version = ABMMeshVersion;
    AFileRead(&version, sizeof(s32), file, 1);
    ASSERT(version == ABMMeshVersion);
    
    u64 reserved[4];
    AFileRead(&reserved, sizeof(u64) * 4, file, 1);
    
    AFileRead(&gltf->scale, sizeof(float), file, 1);
    AFileRead(&gltf->numMeshes, sizeof(u16), file, 1);
    AFileRead(&gltf->numNodes, sizeof(u16), file, 1);
    AFileRead(&gltf->numMaterials, sizeof(u16), file, 1);
    AFileRead(&gltf->numTextures, sizeof(u16), file, 1);
    AFileRead(&gltf->numImages, sizeof(u16), file, 1);
    AFileRead(&gltf->numSamplers, sizeof(u16), file, 1);
    AFileRead(&gltf->numCameras, sizeof(u16), file, 1);
    AFileRead(&gltf->numScenes, sizeof(u16), file, 1);
    AFileRead(&gltf->numSkins, sizeof(u16), file, 1);
    AFileRead(&gltf->numAnimations, sizeof(u16), file, 1);
    AFileRead(&gltf->defaultSceneIndex, sizeof(u16), file, 1);
    u16 isSkined;
    AFileRead(&isSkined, sizeof(u16), file, 1);
    
    AFileRead(&gltf->totalIndices, sizeof(s32), file, 1);
    AFileRead(&gltf->totalVertices, sizeof(s32), file, 1);
    
    size_t vertexSize = sizeof(ASkinedVertex);
    size_t vertexAlignment = 4;

    {
        u64 allVertexSize = gltf->totalVertices * vertexSize;
        u64 allIndexSize  = gltf->totalIndices * sizeof(u32);
        
        gltf->allVertices = AllocAligned(vertexSize * gltf->totalVertices, vertexAlignment); // divide / 4 to get number of floats
        gltf->allIndices = AllocAligned(allIndexSize, 4);
        
        u64 compressedSize;
        AFileRead(&compressedSize, sizeof(u64), file, 1);
        char* compressedBuffer = ArenaPushGlobal(compressedSize); // global_arena.buf + global_arena.curr_offset; //  AllocateTLSFGlobal(allVertexSize);
        AFileRead(compressedBuffer, compressedSize, file, 1);
       
        zsinflate(gltf->allVertices, allVertexSize, compressedBuffer, compressedSize);

        AFileRead(&compressedSize, sizeof(u64), file, 1);
        AFileRead(compressedBuffer, compressedSize, file, 1);
        zsinflate(gltf->allIndices, allIndexSize, compressedBuffer, compressedSize);
    
        ArenaPopGlobal(compressedSize);
    }
    
    char* currVertices = (char*)gltf->allVertices;
    char* currIndices = (char*)gltf->allIndices;
    
    if (gltf->numMeshes > 0) gltf->meshes = AllocZeroTLSFGlobal(gltf->numMeshes, sizeof(AMesh));
    for (s32 i = 0; i < gltf->numMeshes; i++)
    {
        AMesh* mesh = &gltf->meshes[i];
        ReadGLTFString(&mesh->name, file, allocator);
        
        AFileRead(&mesh->numPrimitives, sizeof(s32), file, 1);
        
        mesh->primitives = dynarray_create_prealloc(APrimitive, mesh->numPrimitives);
        
        for (s32 j = 0; j < mesh->numPrimitives; j++)
        {
            APrimitive* primitive = &mesh->primitives[j];
            AFileRead(&primitive->attributes , sizeof(s32), file, 1);
            AFileRead(&primitive->indexType  , sizeof(s32), file, 1);
            AFileRead(&primitive->numIndices , sizeof(s32), file, 1);
            AFileRead(&primitive->numVertices, sizeof(s32), file, 1);
            AFileRead(&primitive->indexOffset, sizeof(s32), file, 1);
            AFileRead(&primitive->jointType, sizeof(u16), file, 1);
            AFileRead(&primitive->jointCount, sizeof(u16), file, 1);
            AFileRead(&primitive->jointStride, sizeof(u16), file, 1);
            
            u64 indexSize = (u64)(GraphicsTypeToSize(primitive->indexType)) * primitive->numIndices;
            primitive->indices = (void*)currIndices;
            currIndices += indexSize;
            
            u64 primitiveVertexSize = (u64)(primitive->numVertices) * vertexSize;
            primitive->vertices = currVertices;
            currVertices += primitiveVertexSize;
            AFileRead(&primitive->material, sizeof(u16), file, 1);
            primitive->hasOutline = false; // always false 
        }
    }
    
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
        skin->inverseBindMatrices = (f1*)AllocateTLSFGlobal(sizeof(m44) * skin->numJoints);
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, skin->numJoints * sizeof(s32));
        AFileRead(skin->inverseBindMatrices, sizeof(m44) * skin->numJoints, file, 1);
        AFileRead(skin->joints, sizeof(s32) * skin->numJoints, file, 1);
    }

    s32 totalAnimSamplerInput = 0;
    AFileRead(&totalAnimSamplerInput, sizeof(s32), file, 1);
    f1* currSamplerInput = NULL;
    v128f* currSamplerOutput = NULL;

    if (totalAnimSamplerInput) {
        currSamplerInput  = (f1*)AllocZeroTLSFGlobal(totalAnimSamplerInput, sizeof(float));
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
            AFileRead(&animation->samplers[j].interpolation, sizeof(float), file, 1);
            s32 count = animation->samplers[j].count;
            animation->samplers[j].input = currSamplerInput;
            animation->samplers[j].output = (f1*)currSamplerOutput;
            currSamplerInput += count;
            currSamplerOutput += count;
        }
    }

    AFileClose(file);
    gltf->allocator = allocator;
    return 1;
}