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
#include "Include/Memory.h"

// #include "Scene.h"

#if !AX_GAME_BUILD
	#include "Extern/ufbx.h"
#endif

#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Include/FileSystem.h"
#include "Include/Common.h"
#include "Include/BasisBinding.h"

#define SINFL_IMPLEMENTATION
#define SDEFL_IMPLEMENTATION
#include "Extern/sdefl.h"
#include "Extern/sinfl.h"
#include "Extern/dynarray.h"

#include "Extern/stb/stb_sprintf.h"

#include <stdio.h>

// https://copyprogramming.com/howto/how-to-pack-normals-into-gl-int-2-10-10-10-rev
static inline uint32_t Pack_INT_2_10_10_10_REV(Vec3f v) {
    const uint32_t xs = v.x < 0.0f, ys = v.y < 0.0f, zs = v.z < 0.0f;   
    return zs << 29 | ((uint32_t)(v.z * 511 + (zs << 9)) & 511) << 20 |
        ys << 19 | ((uint32_t)(v.y * 511 + (ys << 9)) & 511) << 10 |
        xs << 9  | ((uint32_t)(v.x * 511 + (xs << 9)) & 511);
}

static inline uint32_t Pack_INT_2_10_10_10_REV_VEC(Vector4x32f v)
{
    float x = VecGetX(v), y = VecGetY(v), z = VecGetZ(v), w = VecGetW(v); 

    const uint32_t xs = x < 0.0f, ys = y < 0.0f, zs = z < 0.0f, ws = w < 0.0f;
    return ws << 31 | ((uint32_t)(w       + (ws << 1)) & 1) << 30 |
        zs << 29 | ((uint32_t)(z * 511 + (zs << 9)) & 511) << 20 |
        ys << 19 | ((uint32_t)(y * 511 + (ys << 9)) & 511) << 10 |
        xs << 9  | ((uint32_t)(x * 511 + (xs << 9)) & 511);
}

int GraphicsTypeToSize(GraphicType type)
{
    // BYTE, UNSIGNED_BYTE, SHORT, UNSIGNED_SHORT, INT, UNSIGNED_INT, FLOAT 
    const int TypeToSize[12] = { 1, 1, 2, 2, 4, 4, 4, 2, 4, 4, 8, 2 };
    return TypeToSize[type];
}

static inline Vec3f Unpack_INT_2_10_10_10_REV(uint32_t p) 
{
    Vec3f result;
    const uint32_t tenMask = (1 << 10)-1;
    result.x = 255.0f / ((p >>  0) & tenMask);
    result.y = 255.0f / ((p >> 10) & tenMask);
    result.z = 255.0f / ((p >> 20) & tenMask);
    return result;
}


/*//////////////////////////////////////////////////////////////////////////*/
/*                              FBX LOAD                                    */
/*//////////////////////////////////////////////////////////////////////////*/

#if !AX_GAME_BUILD

static int void_ptr_compare(const void* a, const void* b)
{
    const void* const* ptr_a = (const void* const*)a;
    const void* const* ptr_b = (const void* const*)b;

    if (*ptr_a < *ptr_b) return -1;
    if (*ptr_a > *ptr_b) return 1;
    return 0;
}

static short GetFBXTexture(const ufbx_material* umaterial,
                           const ufbx_scene* uscene, 
                           const ufbx_material_feature feature,
                           const ufbx_material_pbr_map pbr,
                           const ufbx_material_fbx_map fbx)
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
            short textureIndex = aIndexOf(uscene->textures.data, texture, (int)uscene->textures.count, sizeof(ufbx_texture*), void_ptr_compare);
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

int LoadFBX(const char* path, SceneBundle* fbxScene, float scale)
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
    
    fbxScene->numMeshes     = (short)uscene->meshes.count;
    fbxScene->numNodes      = (short)uscene->nodes.count;
    fbxScene->numMaterials  = (short)uscene->materials.count;
    fbxScene->numImages     = (short)uscene->texture_files.count; 
    fbxScene->numTextures   = (short)uscene->textures.count;
    fbxScene->numCameras    = (short)uscene->cameras.count;
    fbxScene->totalVertices = 0;
    fbxScene->totalIndices  = 0;
    fbxScene->numScenes     = 0; // todo
    
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 2048);
    
    uint64_t totalIndices  = 0, totalVertices = 0;
    for (int i = 0; i < fbxScene->numMeshes; i++)
    {
        ufbx_mesh* umesh = uscene->meshes.data[i];
        totalIndices  += umesh->num_triangles * 3;
        totalVertices += umesh->num_vertices;
    }
    
    fbxScene->allVertices = AllocAligned(sizeof(ASkinedVertex) * totalVertices, 4);
    fbxScene->allIndices  = AllocAligned(sizeof(uint32_t) * totalIndices, 4);
    
    if (fbxScene->numMeshes) fbxScene->meshes = (AMesh*)AllocZeroTLSFGlobal(fbxScene->numMeshes, sizeof(AMesh));
    
    uint32_t* currentIndex = (uint32_t*)fbxScene->allIndices;
    ASkinedVertex* currentVertex = (ASkinedVertex*)fbxScene->allVertices;

    uint32_t vertexCursor = 0u, indexCursor = 0u;
    
    for (int i = 0; i < fbxScene->numMeshes; i++)
    {
        AMesh* amesh = &fbxScene->meshes[i];
        ufbx_mesh* umesh = uscene->meshes.data[i];
        amesh->name = GetNameFromFBX(umesh->name, allocator);
        amesh->primitives = dynarray_create_prealloc(APrimitive, 1);
        amesh->numPrimitives = 1;
        
        APrimitive* primitive = &amesh->primitives[0];
        primitive->numIndices  = (int)umesh->num_triangles * 3;
        primitive->numVertices = (int)umesh->num_vertices;
        primitive->indexType   = 5; //GraphicType_UnsignedInt;
        primitive->material    = 0; // todo
        primitive->indices     = currentIndex; 
        primitive->vertices    = currentVertex;
       
        primitive->attributes |= AAttribType_POSITION;
        primitive->attributes |= ((int)umesh->vertex_uv.exists << 1) & AAttribType_TEXCOORD_0;
        primitive->attributes |= ((int)umesh->vertex_normal.exists << 2) & AAttribType_NORMAL;
        
        for (int j = 0; j < primitive->numVertices; j++)
        {
            SmallMemCpy(&currentVertex[j].position.x, &umesh->vertex_position.values.data[j], sizeof(float) * 3);
            if (umesh->vertex_uv.exists) {
                currentVertex[j].texCoord = Float2ToHalf2((float*)(umesh->vertex_uv.values.data + j));
            }
            if (umesh->vertex_normal.exists) {
                currentVertex[j].normal = Pack_INT_2_10_10_10_REV(Vec3FromPtr((float*)(umesh->vertex_normal.values.data + j)));
            }
            if (umesh->vertex_tangent.exists)
            {
                Vector4x32f tangent = Vec3Load((float*)(umesh->vertex_normal.values.data + j));
                VecSetW(tangent, 1.0f);
                currentVertex[j].tangent = Pack_INT_2_10_10_10_REV_VEC(tangent);
            }
        }

        uint32_t* currIndices = (uint32_t*)primitive->indices;
        uint32_t indices[64] = {0};
        
        for (int j = 0; j < umesh->faces.count; j++)
        {
            ufbx_face face = umesh->faces.data[j];
            uint32_t num_triangles = ufbx_triangulate_face(indices, ARRAY_SIZE(indices), umesh, face);
            
            for (uint32_t tri_ix = 0; tri_ix < num_triangles; tri_ix++)
            {
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 0]] + vertexCursor;
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 1]] + vertexCursor;
                *currIndices++ = umesh->vertex_indices.data[indices[tri_ix * 3 + 2]] + vertexCursor;
            }
        }
            
        bool hasSkin = umesh->skin_deformers.count > 0;
        if (hasSkin)
        {
            primitive->attributes |= AAttribType_JOINTS | AAttribType_WEIGHTS;
            ufbx_skin_deformer* deformer = umesh->skin_deformers.data[0];
                
            for (int j = 0; j < deformer->vertices.count; j++)
            {
                uint32_t weightBegin = deformer->vertices.data[j].weight_begin;
                uint32_t weightResult = 0, shift = 0;
                uint32_t indexResult  = 0;
            
                for (uint32_t w = 0; w < deformer->vertices.data[j].num_weights && w < 4; w++, shift += 8)
                {
                    ufbx_skin_weight skinWeight = deformer->weights.data[weightBegin + w];
                    float    weight = skinWeight.weight;
                    uint32_t index  = skinWeight.cluster_index;
                    ASSERT(index < 255 && weight <= 1.0f);
                    weightResult |= (uint32_t)(weight * 255.0f) << shift;
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

    uint32_t numSkins = (uint32_t)uscene->skin_deformers.count;
    fbxScene->numSkins = numSkins;
    
    if (numSkins > 0) {
        fbxScene->skins = AllocateTLSFGlobal(numSkins * sizeof(ASkin));
    }

    for (uint32_t d = 0; d < numSkins; d++)
    {
        ufbx_skin_deformer* deformer = uscene->skin_deformers.data[d];
        ASkin* skin = &fbxScene->skins[d];
        uint32_t numJoints = (uint32_t)deformer->clusters.count;
        skin->numJoints = numJoints;
        skin->skeleton = 0;
        skin->inverseBindMatrices = AllocateTLSFGlobal(numJoints * sizeof(Matrix4));
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, (sizeof(int) + 1) * numJoints);
    
        Matrix4* matrices = (Matrix4*)skin->inverseBindMatrices;
        for (uint32_t j = 0u; j < numJoints; j++)
        {
            ufbx_matrix uMatrix = deformer->clusters.data[j]->geometry_to_bone;
            matrices[j].r[0] = Vec3Load(&uMatrix.cols[0].x); 
            matrices[j].r[1] = Vec3Load(&uMatrix.cols[1].x); 
            matrices[j].r[2] = Vec3Load(&uMatrix.cols[2].x); 
            matrices[j].r[3] = Vec3Load(&uMatrix.cols[3].x); 
            VecSetW(matrices[j].r[3], 1.0f); 
        }
        
        for (uint32_t j = 0u; j < numJoints; j++)
        {
            skin->joints[j] = aIndexOf(uscene->nodes.data, deformer->clusters.data[j]->bone_node, (int)uscene->nodes.count, sizeof(void*), void_ptr_compare);
        }
    }
    
    short numImages = (short)uscene->texture_files.count;
    AImage* images = dynarray_create_prealloc(AImage, (int)uscene->texture_files.count);
    
    for (int i = 0; i < numImages; i++)
    {
        images[i].path = GetNameFromFBX(uscene->texture_files.data[i].filename, allocator);
    }
    
    short numTextures = (short)uscene->textures.count;
    fbxScene->numTextures = numTextures;
    fbxScene->numSamplers = numTextures;
    
    if (numTextures)
    {
        fbxScene->textures = (ATexture*)AllocZeroTLSFGlobal(numTextures, sizeof(ATexture));
        fbxScene->samplers = (ASampler*)AllocZeroTLSFGlobal(numTextures, sizeof(ASampler));
    }
    
    for (short i = 0; i < numTextures; i++)
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
            int pathLen = StringLengthSafe(path, 512);
            SmallMemCpy(buffer, path, pathLen);
            
            char* fbxPath = PathGoBackwards(buffer, pathLen, false);
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
    
    short numMaterials = (short)uscene->materials.count;
    fbxScene->numMaterials = numMaterials;
    
    if (numMaterials) {
        fbxScene->materials = AllocateTLSFGlobal(sizeof(AMaterial) * numMaterials);
    }

    for (short i = 0; i < numMaterials; i++)
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
            short normalIndex = aIndexOf(uscene->textures.data, normalTexture, (int)uscene->textures.count, 8, void_ptr_compare);
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
    short numNodes = (short)uscene->nodes.count;
    fbxScene->numNodes = numNodes;
    
    if (numNodes) {
        fbxScene->nodes = (ANode*)AllocZeroTLSFGlobal(numNodes * 4, sizeof(ANode));
    }

    for (int i = 0; i < numNodes; i++)
    {
        ANode* anode = &fbxScene->nodes[i];
        ufbx_node* unode = uscene->nodes.data[i];
        anode->type = unode->camera != NULL;
        anode->name = GetNameFromFBX(unode->name, allocator);
        anode->numChildren = (int)unode->children.count;
        anode->children = FixedPow2Allocator_AllocateUninitialized(allocator, (sizeof(int) + 1 ) * anode->numChildren);
        
        for (int j = 0; j < anode->numChildren; j++)
        {
            anode->children[j] = aIndexOf(uscene->nodes.data, unode->children.data[j], (int)uscene->nodes.count, 8, void_ptr_compare);
            ASSERT(anode->children[j] != -1);
        }
        
        SmallMemCpy(anode->translation, &unode->world_transform.translation.x, sizeof(Vec3f));
        SmallMemCpy(anode->rotation, &unode->world_transform.rotation.x, sizeof(Vector4x32f));
        SmallMemCpy(anode->scale, &unode->world_transform.scale.x, sizeof(Vec3f));
        
        if (anode->type == 0)
        {
            anode->index = aIndexOf(uscene->meshes.data, unode->mesh, (int)uscene->meshes.count, 8, void_ptr_compare);
            if (unode->materials.count > 0)
                fbxScene->meshes[anode->index].primitives[0].material = aIndexOf(uscene->materials.data, unode->materials.data[0], (int)uscene->materials.count, 8, void_ptr_compare);
        }
        else
            anode->index = aIndexOf(uscene->cameras.data, unode->camera, (int)uscene->cameras.count, 8, void_ptr_compare);
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


void CreateVerticesIndices(SceneBundle* gltf)
{
    AMesh* meshes = gltf->meshes;
    
    // pre allocate all vertices and indices 
    gltf->allVertices = AllocAligned(sizeof(AVertex) * gltf->totalVertices, 4);
    gltf->allIndices  = AllocAligned(gltf->totalIndices * sizeof(uint32_t) + 16, 4); // 16->give little bit of space for memcpy
    
    AVertex* currVertex = (AVertex*)gltf->allVertices;
    uint32_t* currIndex = (uint32_t*)gltf->allIndices;
    
    uint32_t vertexCursor = 0, indexCursor = 0;
    int count = 0;

    for (int m = 0; m < gltf->numMeshes; ++m)
    {
        // get number of vertex, getting first attribute count because all of the others are same
        AMesh mesh = meshes[m];
        for (uint64_t p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];
            char* beforeCopy = (char*)primitive->indices;
            primitive->indices = currIndex;
            primitive->indexOffset = indexCursor;
            primitive->vertices = currVertex;
            
            // https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
            Vec3f* positions = (Vec3f*)primitive->vertexAttribs[0];
            Vec2f* texCoords = (Vec2f*)primitive->vertexAttribs[1];
            Vec3f* normals   = (Vec3f*)primitive->vertexAttribs[2];
            Vector4x32f* tangents     = (Vector4x32f*)primitive->vertexAttribs[3];
            
            for (int v = 0; v < primitive->numVertices; v++)
            {
                Vector4x32f    tangent  = tangents  ? tangents[v]  : VecZero();
                Vec2f texCoord = texCoords ? texCoords[v] : (Vec2f){0.0f, 0.0f};
                Vec3f normal   = normals   ? normals[v]   : (Vec3f){0.5f, 0.5f, 0.0};

                currVertex[v].position  = positions[v];
                currVertex[v].texCoord  = Float2ToHalf2(&texCoord.x);
                currVertex[v].normal    = Pack_INT_2_10_10_10_REV(normal);
                currVertex[v].tangent   = Pack_INT_2_10_10_10_REV_VEC(tangent);
            }

            int indexSize = GraphicsTypeToSize(primitive->indexType);
            
            for (int i = 0; i < primitive->numIndices; i++)
            {
                uint32_t index = 0;
                // index type might be ushort we are converting it to uint32 here.
                SmallMemCpy(&index, beforeCopy, indexSize);
                // we are combining all vertices and indices into one buffer, that's why we have to add vertex cursor
                currIndex[i] = index + vertexCursor;
                beforeCopy += indexSize;
            }

            currVertex += primitive->numVertices;

            primitive->indexOffset = indexCursor;
            indexCursor += primitive->numIndices;

            vertexCursor += primitive->numVertices;
            currIndex += primitive->numIndices;
        }
    }
    
    FreeGLTFBuffers(gltf);
}

void CreateVerticesIndicesSkined(SceneBundle* gltf)
{
    AMesh* meshes = gltf->meshes;
    
    // pre allocate all vertices and indices 
    gltf->allVertices = AllocAligned(sizeof(ASkinedVertex) * gltf->totalVertices, 4);
    gltf->allIndices  = AllocAligned(gltf->totalIndices * sizeof(uint32_t) + 16, 4); // 16->give little bit of space for memcpy
    
    ASkinedVertex* currVertex = (ASkinedVertex*)gltf->allVertices;
    uint32_t* currIndices = (uint32_t*)gltf->allIndices;
    
    int vertexCursor = 0;
    int indexCursor = 0;
    
    for (int m = 0; m < gltf->numMeshes; ++m)
    {
        // get number of vertex, getting first attribute count because all of the others are same
        AMesh mesh = meshes[m];
        for (uint64_t p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];
            char* beforeCopy = (char*)primitive->indices;
            primitive->indices = currIndices;
            int indexSize = GraphicsTypeToSize(primitive->indexType);

            for (int i = 0; i < primitive->numIndices; i++)
            {
                uint32_t index = 0;
                SmallMemCpy(&index, beforeCopy, indexSize);
                // we are combining all vertices and indices into one buffer, that's why we have to add vertex cursor
                currIndices[i] = index + vertexCursor; 
                beforeCopy += indexSize;
            }
            
            // https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
            primitive->vertices = currVertex;
            Vec3f* positions = (Vec3f*)primitive->vertexAttribs[0];
            Vec2f* texCoords = (Vec2f*)primitive->vertexAttribs[1];
            Vec3f* normals   = (Vec3f*)primitive->vertexAttribs[2];
            Vector4x32f*    tangents  = (Vector4x32f*)primitive->vertexAttribs[3];

            for (int v = 0; v < primitive->numVertices; v++)
            {
                Vector4x32f tangent     = tangents  ? tangents[v]  : VecZero();
                Vec2f texCoord = texCoords ? texCoords[v] : (Vec2f){0.0f, 0.0f};
                Vec3f normal   = normals   ? normals[v]   : (Vec3f){0.5f, 0.5f, 0.0};

                currVertex[v].position = positions[v];
                currVertex[v].texCoord = Float2ToHalf2(&texCoord.x);
                currVertex[v].normal   = Pack_INT_2_10_10_10_REV(normal);
                currVertex[v].tangent  = Pack_INT_2_10_10_10_REV_VEC(tangent);
            }

            // convert whatever joint format to rgb8u
            char* joints  = (char*)primitive->vertexAttribs[5];
            char* weights = (char*)primitive->vertexAttribs[6];

            // size and offset in bytes
            int jointSize = GraphicsTypeToSize(primitive->jointType);
            int jointOffset = Max32((int)(primitive->jointStride - (jointSize * primitive->jointCount)), 0); // stride - sizeof(rgbau16)
            // size and offset in bytes
            int weightSize   = GraphicsTypeToSize(primitive->weightType);
            int weightOffset = Max32((int)(primitive->weightStride - (weightSize * primitive->jointCount)), 0);
            
            for (int j = 0; j < primitive->numVertices; j++)
            {
                // Combine 4 indices into one integer to save space
                uint32_t packedJoints = 0u;
                // iterate over joint indices, most of the time 4 indices
                for (int k = 0, shift = 0; k < primitive->jointCount; k++)
                {
                    uint32_t jointIndex = 0;
                    SmallMemCpy(&jointIndex, joints, jointSize); 
                    ASSERT(jointIndex < 255u && "index has to be smaller than 255");
                    packedJoints |= jointIndex << shift;
                    shift += 8;
                    joints += jointSize;
                }

                uint32_t packedWeights;
                if (weightSize == 4) // if float, pack it directly
                {
                    packedWeights = PackColor4PtrToUint((float*)weights);
                    weights += weightSize * 4;
                }
                else
                {
                    for (int k = 0, shift = 0; k < primitive->jointCount && k < 4; k++, shift += 8)
                    {
                        uint32_t jointWeight = 0u;
                        SmallMemCpy(&jointWeight, weights, weightSize); 
                        float weightMax = (float)((1u << (weightSize * 8)) - 1);
                        float norm = (float)jointWeight / weightMax; // divide by 255 or 65535
                        packedWeights |= (uint32_t)(norm * 255.0f) << shift;
                        weights += weightSize;
                    }
                }
                currVertex[j].joints  = packedJoints;
                currVertex[j].weights = packedWeights;

                if (currVertex[j].weights == 0)
                    currVertex[j].weights = 0XFF000000u;

                joints  += jointOffset; // stride offset at the end of the struct
                weights += weightOffset;
            }

            currVertex += primitive->numVertices;
            primitive->indexOffset = indexCursor;
            indexCursor += primitive->numIndices;
            
            vertexCursor += primitive->numVertices;
            currIndices  += primitive->numIndices;
        }
    }
    
    for (int s = 0; s < gltf->numSkins; s++)
    {
        ASkin* skin = &gltf->skins[s];
        Matrix4* inverseBindMatrices = AllocateTLSFGlobal(skin->numJoints * sizeof(Matrix4));
        SmallMemCpy(inverseBindMatrices, skin->inverseBindMatrices, sizeof(Matrix4) * skin->numJoints);
        skin->inverseBindMatrices = (float*)inverseBindMatrices;
    }

    if (gltf->numAnimations)
    {
        int totalSamplerInput = 0;
        for (int a = 0; a < gltf->numAnimations; a++)
            for (int s = 0; s < gltf->animations[a].numSamplers; s++)
                totalSamplerInput += gltf->animations[a].samplers[s].count;
        
        float* currSampler = (float*)AllocZeroTLSFGlobal(totalSamplerInput, 4);
        Vector4x32f* currOutput = (Vector4x32f*)AllocZeroTLSFGlobal(totalSamplerInput, sizeof(Vector4x32f));

        for (int a = 0; a < gltf->numAnimations; a++)
        {
            for (int s = 0; s < gltf->animations[a].numSamplers; s++)
            {
                AAnimSampler* sampler = &gltf->animations[a].samplers[s];
                SmallMemCpy(currSampler, sampler->input, sampler->count * sizeof(float));
                sampler->input = currSampler;
                currSampler += sampler->count;

                for (int i = 0; i < sampler->count; i++)
                {
                    SmallMemCpy(currOutput + i, sampler->output + (i * sampler->numComponent), sizeof(float) * sampler->numComponent);
                    currOutput[i] = VecLoad(sampler->output + (i * sampler->numComponent));
                    if (sampler->numComponent == 3) currOutput[i] = VecSetW(currOutput[i], 0.0f);
                }
                sampler->output = (float*)currOutput;
                currOutput += sampler->count;
            }
        }
    }

    FreeGLTFBuffers(gltf);
}


void SaveSceneImages(SceneBundle* scene, const char* savePath)
{
    char command[2048];
    char outputDir[2048] = { 0 };
    
    int pathLen = StringLengthSafe(savePath, sizeof(outputDir));
    
    // write texture description into .bdc file
    SmallMemCpy(outputDir, savePath, pathLen);
    ChangeExtension(outputDir, pathLen, "bdc");
    AFile file = AFileOpen(outputDir, AOpenFlag_WriteText);
    
    int numDigits = IntToString(command, (int64_t)scene->numImages, 0);
    command[numDigits] = '\n';
    AFileWrite(command, numDigits + 1, file, 1); // num textures 

    for (int i = 0; i < scene->numImages; i++)
    {
        const char* path = scene->images[i].path;
        int len = (int)StringLengthSafe(path, sizeof(outputDir)-2);
        SmallMemCpy(outputDir, path, len);
        outputDir[len] = '\n';
        AFileWrite(outputDir, len + 1, file, 1); // write original texture path
     
        PathGoBackwards(outputDir, pathLen, true);
        
        int textureType = 0;
        for (int j = 0; j < scene->numMaterials; j++)
        {
            textureType |= scene->textures[scene->materials[j].textures[0].index].source == i;
            // if an normal map used as base color, unmark it. (causing problems on sponza)
            textureType &= ~(scene->textures[scene->materials[j].baseColorTexture.index].source == i);
            textureType |= (scene->textures[scene->materials[j].metallicRoughnessTexture.index].source == i) << 1;
            // mixamo animations are exporting specular instead of metallic roughness, 
            // in our engine we don't use specular but using metallic roughess with our engine specular means metallic roughness
            textureType |= (scene->textures[scene->materials[j].specularTexture.index].source == i) << 1;
        }

        int isNormal = (textureType & 1) != 0;
        int isMetallicRoughness = (textureType & 2) != 0;

        // choose compression format
        const char* formatFlag = isMetallicRoughness ? "-etc1s" : "-uastc";

        snprintf(command, sizeof(command),
                 "Extern\\basis_universal\\basisu.exe \"%s\" %s -basis -mipmap -mip_smallest 256 %s -output_path \"%s\"",
                 path,
                 formatFlag,
                 isNormal ? " -normal_map" : "",
                 outputDir);

        // compress using basis universal
        int result = system(command);
        if (result != 0)
        {
            printf("Failed to compress: %s\n", path);
        }

        // write texture type as text
        int numDigits = IntToString(outputDir, textureType, 0);
        outputDir[numDigits] = '\n';
        AFileWrite(outputDir, numDigits + 1, file, 1); // num textures 
    }

    AFileClose(file);
}

// result: 1 fine, 2 some file is not exist, 3 not enough images for scene
int LoadSceneImages(const char* texturePath, Texture* textures, int numImages, SDL_GPUDevice* gpuDevice)
{
    if (numImages <= 0)
        return 1;

    AFile file = AFileOpen(texturePath, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
        return 0;

    char buffer[2048];

    int result = 1;
    int fileNumImages = AFileReadI32(buffer, sizeof(buffer), file); // First line: numImages
    if (fileNumImages != numImages)
    {
        AX_WARN("basis file num images not equal to requested numImages. file: %d, requested: %d", fileNumImages, numImages);
        numImages = fileNumImages;
        result = 3;
    }

    for (int i = 0; i < numImages; i++)
    {
        int pathLen = AFileReadLine(buffer, sizeof(buffer), file);
        ChangeExtension(buffer, pathLen, "basis");

        uint64_t size = FileSize(buffer);
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
        int textureType = AFileReadI32(buffer, sizeof(buffer), file);

        int isNormal =  textureType & 1;
        int isMetallicRoughness = (textureType >> 1) & 1;

        textures[i].handle = BasisuMakeImage(basisData, size, &textures[i].width, &textures[i].height, &textures[i].format,
                                             gpuDevice, (bool)isNormal, (bool)isMetallicRoughness);
        ArenaPopGlobal(size);
    }
    return result;
}

void GenerateLOD_50_GLTF(SceneBundle* sceneBundle)
{
    for (int m = 0; m < sceneBundle->numMeshes; m++)
    {
        AMesh mesh = sceneBundle->meshes[m];

        for (int p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive primitive = mesh.primitives[p];
        
            size_t numIndices = (size_t)primitive.numIndices;
            int* indicesLod0 = ArenaAllocGlobal(numIndices * sizeof(int));
        
            float resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, 
                                                          (const uint32_t*)primitive.indices, 
                                                          numIndices, 
                                                          (const float*)sceneBundle->allVertices, 
                                                          (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex),
                                                          NULL,
                                                          numIndices - (numIndices >> 1), 
                                                          0.04f,
                                                          &resultError);
            primitive.numIndicesLOD50 = numSimplified;
            MemCpy(primitive.lodIndices50, indicesLod0, numSimplified * sizeof(uint32_t));
            ArenaPopGlobal(numSimplified * sizeof(int));
        }
    }
}

void GenerateLOD_75_GLTF(SceneBundle* sceneBundle)
{
    for (int m = 0; m < sceneBundle->numMeshes; m++)
    {
        AMesh mesh = sceneBundle->meshes[m];

        for (int p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive primitive = mesh.primitives[p];
        
            size_t numIndices = (size_t)primitive.numIndices;
            int* indicesLod0 = ArenaAllocGlobal(numIndices * sizeof(int));
        
            float resultError;
            size_t numSimplified = meshopt_simplifySloppy(indicesLod0, 
                                                          (const uint32_t*)primitive.indices, 
                                                          numIndices, 
                                                          (const float*)sceneBundle->allVertices, 
                                                          (size_t)sceneBundle->totalVertices,
                                                          sizeof(ASkinedVertex),
                                                          NULL,
                                                          numIndices - (numIndices >> 2), 
                                                          0.04f,
                                                          &resultError);
            primitive.numIndicesLOD75 = numSimplified;
            MemCpy(primitive.lodIndices75, indicesLod0, numSimplified * sizeof(uint32_t));
            ArenaPopGlobal(numSimplified * sizeof(int));
        }
    }
}

void OptimizeMesh(const SceneBundle* gltf)
{
    int* remap = ArenaAllocGlobal(gltf->totalIndices * sizeof(int));
    size_t totalVertices = meshopt_generateVertexRemap(remap,
                                                       (const uint32_t *)gltf->allIndices,
                                                       (size_t)gltf->totalIndices,
                                                       gltf->allVertices,
                                                       (size_t)gltf->totalVertices,
                                                       sizeof(ASkinedVertex));

    int* temp = ArenaAllocGlobal(gltf->totalIndices * sizeof(int));
    meshopt_remapIndexBuffer(temp, gltf->allIndices, (size_t)gltf->totalIndices, remap);

    ASkinedVertex* vertexBufferNew = ArenaAllocGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    meshopt_remapVertexBuffer(vertexBufferNew,
                              gltf->allVertices,
                              (size_t)gltf->totalVertices,
                              sizeof(ASkinedVertex),
                              remap);

    MemSet(gltf->allVertices, 0, (size_t)gltf->totalVertices * sizeof(ASkinedVertex));
    MemSet(gltf->allIndices , 0, (size_t)gltf->totalIndices * sizeof(int));
    
    MemCpy(gltf->allIndices , temp, (size_t)gltf->totalIndices * sizeof(int));
    MemCpy(gltf->allVertices, vertexBufferNew, (size_t)totalVertices * sizeof(ASkinedVertex));
    
    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(int)); // remap
    ArenaPopGlobal((size_t)gltf->totalIndices * sizeof(int)); // temp
    ArenaPopGlobal((size_t)gltf->totalVertices * sizeof(ASkinedVertex)); // vertexBufferNew

    meshopt_optimizeVertexCache(gltf->allIndices , gltf->allIndices, gltf->totalIndices, (size_t)totalVertices);
    meshopt_optimizeVertexFetch(gltf->allVertices, gltf->allIndices, gltf->totalIndices,
                                gltf->allVertices, (size_t)totalVertices, sizeof(ASkinedVertex));
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                            Binary Save                                   */
/*//////////////////////////////////////////////////////////////////////////*/

// ZSTD_CCtx* zstdCompressorCTX = NULL;
const int ABMMeshVersion = 42;

bool IsABMLastVersion(const char* path)
{
    if (!FileExist(path))
        return false;
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (AFileSize(file) < sizeof(short) * 16) 
        return false;
    int version = 0;
    AFileRead(&version, sizeof(int), file, 1);
    uint64_t hex;
    AFileRead(&hex, sizeof(uint64_t), file, 1);
    AFileClose(file);
    return version == ABMMeshVersion && hex == 0xABFABF;
}

static void WriteAMaterialTexture(GLTFTexture texture, AFile file)
{
    uint64_t data = texture.scale; data <<= sizeof(uint16_t) * 8;
    data |= texture.strength;      data <<= sizeof(uint16_t) * 8;
    data |= texture.index;         data <<= sizeof(uint16_t) * 8;
    data |= texture.texCoord;
    
    AFileWrite(&data, sizeof(uint64_t), file, 1);
}

static void WriteGLTFString(const char* str, AFile file)
{
    int nameLen = str ? StringLength(str) : 0;
    AFileWrite(&nameLen, sizeof(int), file, 1);
    if (str) AFileWrite(str, nameLen + 1, file, 1);
}

int SaveGLTFBinary(const SceneBundle* gltf, const char* path)
{
#if !AX_GAME_BUILD
    AFile file = AFileOpen(path, AOpenFlag_WriteBinary);
    
    int version = ABMMeshVersion;
    AFileWrite(&version, sizeof(int), file, 1);
    
    uint64_t reserved[4] = { 0xABFABF };
    AFileWrite(&reserved, sizeof(uint64_t) * 4, file, 1);
    
    AFileWrite(&gltf->scale, sizeof(float), file, 1);
    AFileWrite(&gltf->numMeshes, sizeof(short), file, 1);
    AFileWrite(&gltf->numNodes, sizeof(short), file, 1);
    AFileWrite(&gltf->numMaterials,  sizeof(short), file, 1);
    AFileWrite(&gltf->numTextures, sizeof(short), file, 1);
    AFileWrite(&gltf->numImages, sizeof(short), file, 1);
    AFileWrite(&gltf->numSamplers, sizeof(short), file, 1);
    AFileWrite(&gltf->numCameras, sizeof(short), file, 1);
    AFileWrite(&gltf->numScenes, sizeof(short), file, 1);
    AFileWrite(&gltf->numSkins, sizeof(short), file, 1);
    AFileWrite(&gltf->numAnimations, sizeof(short), file, 1);
    AFileWrite(&gltf->defaultSceneIndex, sizeof(short), file, 1);
    short isSkined = (short)(gltf->skins != NULL);
    AFileWrite(&isSkined, sizeof(short), file, 1);
    
    OptimizeMesh(gltf);

    AFileWrite(&gltf->totalIndices, sizeof(int), file, 1);
    AFileWrite(&gltf->totalVertices, sizeof(int), file, 1);
    
    uint64_t vertexSize = isSkined ? sizeof(ASkinedVertex) : sizeof(AVertex);
    uint64_t allVertexSize = vertexSize * (uint64_t)gltf->totalVertices;
    uint64_t allIndexSize = (uint64_t)gltf->totalIndices * sizeof(uint32_t);
    
    // Compress and write, vertices and indices
    uint64_t compressedSize = (uint64_t)(allVertexSize * 0.9);
    char* compressedBuffer = ArenaPushGlobal(compressedSize); // global_arena.buf + global_arena.curr_offset;
    
    struct sdefl sdfl;
    size_t afterCompSize = zsdeflate(&sdfl, compressedBuffer, gltf->allVertices, allVertexSize, 5);
    AFileWrite(&afterCompSize, sizeof(uint64_t), file, 1);
    AFileWrite(compressedBuffer, afterCompSize, file, 1);
    
    afterCompSize = zsdeflate(&sdfl, compressedBuffer, gltf->allIndices, allIndexSize, 5);
    AFileWrite(&afterCompSize, sizeof(uint64_t), file, 1);
    AFileWrite(compressedBuffer, afterCompSize, file, 1);
    // DeAllocateTLSFGlobal(compressedBuffer);
    // Note: anim morph targets aren't saved

    for (int i = 0; i < gltf->numMeshes; i++)
    {
        AMesh mesh = gltf->meshes[i];
        WriteGLTFString(mesh.name, file);
        
        AFileWrite(&mesh.numPrimitives  , sizeof(int), file, 1);
        
        for (int j = 0; j < mesh.numPrimitives; j++)
        {
            APrimitive* primitive = &mesh.primitives[j];
            AFileWrite(&primitive->attributes , sizeof(int), file, 1);
            AFileWrite(&primitive->indexType  , sizeof(int), file, 1);
            AFileWrite(&primitive->numIndices , sizeof(int), file, 1);
            AFileWrite(&primitive->numVertices, sizeof(int), file, 1);
            AFileWrite(&primitive->indexOffset, sizeof(int), file, 1);
            AFileWrite(&primitive->jointType  , sizeof(short), file, 1);
            AFileWrite(&primitive->jointCount , sizeof(short), file, 1);
            AFileWrite(&primitive->jointStride, sizeof(short), file, 1);
            AFileWrite(&primitive->material   , sizeof(short), file, 1);
        }
    }
    
    for (int i = 0; i < gltf->numNodes; i++)
    {
        ANode* node = &gltf->nodes[i];
        AFileWrite(&node->type       , sizeof(int), file, 1);
        AFileWrite(&node->index      , sizeof(int), file, 1);
        AFileWrite(&node->translation, sizeof(float) * 3, file, 1);
        AFileWrite(&node->rotation   , sizeof(float) * 4, file, 1);
        AFileWrite(&node->scale      , sizeof(float) * 3, file, 1);
        AFileWrite(&node->numChildren, sizeof(int), file, 1);
        
        if (node->numChildren)
            AFileWrite(node->children, sizeof(int) * node->numChildren, file, 1);
        
        WriteGLTFString(node->name, file);
    }
    
    for (int i = 0; i < gltf->numMaterials; i++)
    {
        AMaterial* material = &gltf->materials[i];
        for (int j = 0; j < 3; j++)
        {
            WriteAMaterialTexture(material->textures[j], file);
        }
        
        WriteAMaterialTexture(material->baseColorTexture, file);
        WriteAMaterialTexture(material->specularTexture, file);
        WriteAMaterialTexture(material->metallicRoughnessTexture, file);
        
        uint64_t data = material->emissiveFactor[0]; data <<= sizeof(short) * 8;
        data |= material->emissiveFactor[1];         data <<= sizeof(short) * 8;
        data |= material->emissiveFactor[2];         data <<= sizeof(short) * 8;
        data |= material->specularFactor;
        AFileWrite(&data, sizeof(uint64_t), file, 1);
        
        data = ((uint64_t)(material->diffuseColor) << 32) | material->specularColor;
        AFileWrite(&data, sizeof(uint64_t), file, 1);
        
        data = ((uint64_t)(material->baseColorFactor) << 32) | (uint64_t)material->doubleSided;
        AFileWrite(&data, sizeof(uint64_t), file, 1);
        
        AFileWrite(&material->alphaCutoff, sizeof(float), file, 1);
        AFileWrite(&material->alphaMode, sizeof(int), file, 1);
        
        WriteGLTFString(material->name, file);
    }
    
    for (int i = 0; i < gltf->numTextures; i++)
    {
        ATexture texture = gltf->textures[i];
        AFileWrite(&texture.sampler, sizeof(int), file, 1);
        AFileWrite(&texture.source, sizeof(int), file, 1);
        WriteGLTFString(texture.name , file);
    }
    
    for (int i = 0; i < gltf->numImages; i++)
    {
        WriteGLTFString(gltf->images[i].path, file);
    }
    
    for (int i = 0; i < gltf->numSamplers; i++)
    {
    	AFileWrite(&gltf->samplers[i], sizeof(ASampler), file, 1);
    }
    
    for (int i = 0; i < gltf->numCameras; i++)
    {
        ACamera camera = gltf->cameras[i];
        AFileWrite(&camera.aspectRatio, sizeof(float), file, 1);
        AFileWrite(&camera.yFov, sizeof(float), file, 1);
        AFileWrite(&camera.zFar, sizeof(float), file, 1);
        AFileWrite(&camera.zNear, sizeof(float), file, 1);
        AFileWrite(&camera.type, sizeof(int), file, 1);
        WriteGLTFString(camera.name , file);
    }
    for (int i = 0; i < gltf->numScenes; i++)
    {
        AScene scene = gltf->scenes[i];
        WriteGLTFString(scene.name, file);
        AFileWrite(&scene.numNodes, sizeof(int), file, 1);
        AFileWrite(scene.nodes, sizeof(int) * scene.numNodes, file, 1);
    }
    
    for (int i = 0; i < gltf->numSkins; i++)
    {
        ASkin skin = gltf->skins[i];
        AFileWrite(&skin.skeleton, sizeof(int), file, 1);
        AFileWrite(&skin.numJoints, sizeof(int), file, 1);
        AFileWrite(skin.inverseBindMatrices, sizeof(Matrix4) * skin.numJoints, file, 1);
        AFileWrite(skin.joints, sizeof(int) * skin.numJoints, file, 1);
    }
    
    int totalAnimSamplerInput = 0;
    if (gltf->numAnimations > 0)
    {
        for (int a = 0; a < gltf->numAnimations; a++)
            for (int s = 0; s < gltf->animations[a].numSamplers; s++)
                totalAnimSamplerInput += gltf->animations[a].samplers[s].count;
    }

    AFileWrite(&totalAnimSamplerInput, sizeof(int), file, 1);
    if (totalAnimSamplerInput > 0) {
        // all sampler input and outputs are allocated in one buffer each. at the end of the CreateVerticesIndicesSkined function
        AFileWrite(gltf->animations[0].samplers[0].input, sizeof(float) * totalAnimSamplerInput, file, 1);
        AFileWrite(gltf->animations[0].samplers[0].output, sizeof(Vector4x32f) * totalAnimSamplerInput, file, 1);
    }

    for (int i = 0; i < gltf->numAnimations; i++)
    {
        AAnimation animation = gltf->animations[i];
        AFileWrite(&animation.numSamplers, sizeof(int), file, 1);
        AFileWrite(&animation.numChannels, sizeof(int), file, 1);
        AFileWrite(&animation.duration, sizeof(float), file, 1);
        AFileWrite(&animation.speed, sizeof(float), file, 1);
        WriteGLTFString(animation.name, file);

        AFileWrite(animation.channels, sizeof(AAnimChannel) * animation.numChannels, file, 1);
        
        for (int j = 0; j < animation.numSamplers; j++)
        {
            AFileWrite(&animation.samplers[j].count, sizeof(int), file, 1);
            AFileWrite(&animation.samplers[j].numComponent, sizeof(int), file, 1);
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
    uint64_t data;
    AFileRead(&data, sizeof(uint64_t), file, 1);
    
    texture->texCoord = data & 0xFFFFu; data >>= sizeof(short) * 8;
    texture->index    = data & 0xFFFFu; data >>= sizeof(short) * 8;
    texture->strength = data & 0xFFFFu; data >>= sizeof(short) * 8;
    texture->scale    = data & 0xFFFFu;
}

void ReadGLTFString(char** str, AFile file, FixedPow2Allocator* stringAllocator)
{
    int nameLen = 0;
    AFileRead(&nameLen, sizeof(int), file, 1);
    if (nameLen)    
    {
        *str = FixedPow2Allocator_AllocateUninitialized(stringAllocator, nameLen + 1);
        AFileRead(*str, nameLen + 1, file, 1);
        (*str)[nameLen + 1] = 0;
    }}

int LoadSceneBundleBinary(const char* path, SceneBundle* gltf)
{
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
    {
        perror("Failed to open file for writing");
        return 0;
    }
    
    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 1024);

    int version = ABMMeshVersion;
    AFileRead(&version, sizeof(int), file, 1);
    ASSERT(version == ABMMeshVersion);
    
    uint64_t reserved[4];
    AFileRead(&reserved, sizeof(uint64_t) * 4, file, 1);
    
    AFileRead(&gltf->scale, sizeof(float), file, 1);
    AFileRead(&gltf->numMeshes, sizeof(short), file, 1);
    AFileRead(&gltf->numNodes, sizeof(short), file, 1);
    AFileRead(&gltf->numMaterials, sizeof(short), file, 1);
    AFileRead(&gltf->numTextures, sizeof(short), file, 1);
    AFileRead(&gltf->numImages, sizeof(short), file, 1);
    AFileRead(&gltf->numSamplers, sizeof(short), file, 1);
    AFileRead(&gltf->numCameras, sizeof(short), file, 1);
    AFileRead(&gltf->numScenes, sizeof(short), file, 1);
    AFileRead(&gltf->numSkins, sizeof(short), file, 1);
    AFileRead(&gltf->numAnimations, sizeof(short), file, 1);
    AFileRead(&gltf->defaultSceneIndex, sizeof(short), file, 1);
    short isSkined;
    AFileRead(&isSkined, sizeof(short), file, 1);
    
    AFileRead(&gltf->totalIndices, sizeof(int), file, 1);
    AFileRead(&gltf->totalVertices, sizeof(int), file, 1);
    
    size_t vertexSize = sizeof(ASkinedVertex);
    size_t vertexAlignment = 4;

    {
        uint64_t allVertexSize = gltf->totalVertices * vertexSize;
        uint64_t allIndexSize  = gltf->totalIndices * sizeof(uint32_t);
        
        gltf->allVertices = AllocAligned(vertexSize * gltf->totalVertices, vertexAlignment); // divide / 4 to get number of floats
        gltf->allIndices = AllocAligned(allIndexSize, 4);
        
        uint64_t compressedSize;
        AFileRead(&compressedSize, sizeof(uint64_t), file, 1);
        char* compressedBuffer = ArenaPushGlobal(compressedSize); // global_arena.buf + global_arena.curr_offset; //  AllocateTLSFGlobal(allVertexSize);
        AFileRead(compressedBuffer, compressedSize, file, 1);
       
        zsinflate(gltf->allVertices, allVertexSize, compressedBuffer, compressedSize);

        AFileRead(&compressedSize, sizeof(uint64_t), file, 1);
        AFileRead(compressedBuffer, compressedSize, file, 1);
        zsinflate(gltf->allIndices, allIndexSize, compressedBuffer, compressedSize);
    
        ArenaPopGlobal(compressedSize);
    }
    
    char* currVertices = (char*)gltf->allVertices;
    char* currIndices = (char*)gltf->allIndices;
    
    if (gltf->numMeshes > 0) gltf->meshes = AllocZeroTLSFGlobal(gltf->numMeshes, sizeof(AMesh));
    for (int i = 0; i < gltf->numMeshes; i++)
    {
        AMesh* mesh = &gltf->meshes[i];
        ReadGLTFString(&mesh->name, file, allocator);
        
        AFileRead(&mesh->numPrimitives, sizeof(int), file, 1);
        
        mesh->primitives = dynarray_create_prealloc(APrimitive, mesh->numPrimitives);
        
        for (int j = 0; j < mesh->numPrimitives; j++)
        {
            APrimitive* primitive = &mesh->primitives[j];
            AFileRead(&primitive->attributes , sizeof(int), file, 1);
            AFileRead(&primitive->indexType  , sizeof(int), file, 1);
            AFileRead(&primitive->numIndices , sizeof(int), file, 1);
            AFileRead(&primitive->numVertices, sizeof(int), file, 1);
            AFileRead(&primitive->indexOffset, sizeof(int), file, 1);
            AFileRead(&primitive->jointType, sizeof(short), file, 1);
            AFileRead(&primitive->jointCount, sizeof(short), file, 1);
            AFileRead(&primitive->jointStride, sizeof(short), file, 1);
            
            uint64_t indexSize = (uint64_t)(GraphicsTypeToSize(primitive->indexType)) * primitive->numIndices;
            primitive->indices = (void*)currIndices;
            currIndices += indexSize;
            
            uint64_t primitiveVertexSize = (uint64_t)(primitive->numVertices) * vertexSize;
            primitive->vertices = currVertices;
            currVertices += primitiveVertexSize;
            AFileRead(&primitive->material, sizeof(short), file, 1);
            primitive->hasOutline = false; // always false 
        }
    }
    
    if (gltf->numNodes > 0) gltf->nodes = AllocZeroTLSFGlobal(gltf->numNodes, sizeof(ANode));
    
    for (int i = 0; i < gltf->numNodes; i++)
    {
        ANode* node = &gltf->nodes[i];
        AFileRead(&node->type       , sizeof(int), file, 1);
        AFileRead(&node->index      , sizeof(int), file, 1);
        AFileRead(&node->translation, sizeof(float) * 3, file, 1);
        AFileRead(&node->rotation   , sizeof(float) * 4, file, 1);
        AFileRead(&node->scale      , sizeof(float) * 3, file, 1);
        AFileRead(&node->numChildren, sizeof(int), file, 1);
        
        if (node->numChildren)
        {
            node->children = FixedPow2Allocator_AllocateUninitialized(allocator, sizeof(int) * (node->numChildren+1));
            AFileRead(node->children, sizeof(int) * node->numChildren, file, 1);
        }
        
        ReadGLTFString(&node->name, file, allocator);
    }
    
    if (gltf->numMaterials > 0) gltf->materials = AllocZeroTLSFGlobal(gltf->numMaterials, sizeof(AMaterial));
    for (int i = 0; i < gltf->numMaterials; i++)
    {
        AMaterial* material = &gltf->materials[i];
        for (int j = 0; j < 3; j++)
        {
            ReadAMaterialTexture(&material->textures[j], file);
        }
        
        ReadAMaterialTexture(&material->baseColorTexture, file);
        ReadAMaterialTexture(&material->specularTexture, file);
        ReadAMaterialTexture(&material->metallicRoughnessTexture, file);
        
        uint64_t data;
        AFileRead(&data, sizeof(uint64_t), file, 1);
        
        material->specularFactor    = data & 0xFFFF; data >>= sizeof(short) * 8;
        material->emissiveFactor[2] = data & 0xFFFF; data >>= sizeof(short) * 8;
        material->emissiveFactor[1] = data & 0xFFFF; data >>= sizeof(short) * 8;
        material->emissiveFactor[0] = data & 0xFFFF; 
        
        AFileRead(&data, sizeof(uint64_t), file, 1);
        material->diffuseColor  = (data >> 32);
        material->specularColor = data & 0xFFFFFFFF;
        
        AFileRead(&data, sizeof(uint64_t), file, 1);
        material->baseColorFactor = (data >> 32);
        material->doubleSided     = data & 0x1;
        
        AFileRead(&material->alphaCutoff, sizeof(float), file, 1);
        AFileRead(&material->alphaMode, sizeof(int), file, 1);
        
        ReadGLTFString(&material->name, file, allocator);
    }
    
    if (gltf->numTextures > 0) gltf->textures = (ATexture*)AllocZeroTLSFGlobal(gltf->numTextures, sizeof(ATexture));
    for (int i = 0; i < gltf->numTextures; i++)
    {
        ATexture* texture = &gltf->textures[i];
        AFileRead(&texture->sampler, sizeof(int), file, 1);
        AFileRead(&texture->source, sizeof(int), file, 1);
        ReadGLTFString(&texture->name, file, allocator);
    }
    if (gltf->numImages > 0) gltf->images = (AImage*)AllocZeroTLSFGlobal(gltf->numImages, sizeof(AImage));
    for (int i = 0; i < gltf->numImages; i++)
    {
        ReadGLTFString(&gltf->images[i].path, file, allocator);
    }
    
    if (gltf->numSamplers > 0) gltf->samplers = (ASampler*)AllocZeroTLSFGlobal(gltf->numSamplers, sizeof(ASampler));
    for (int i = 0; i < gltf->numSamplers; i++)
    {
        AFileRead(&gltf->samplers[i], sizeof(ASampler), file, 1);
    }
    
    if (gltf->numCameras > 0) gltf->cameras = (ACamera*)AllocZeroTLSFGlobal(gltf->numCameras, sizeof(ACamera));
    for (int i = 0; i < gltf->numCameras; i++)
    {
        ACamera* camera = &gltf->cameras[i];
        AFileRead(&camera->aspectRatio, sizeof(float), file, 1);
        AFileRead(&camera->yFov, sizeof(float), file, 1);
        AFileRead(&camera->zFar, sizeof(float), file, 1);
        AFileRead(&camera->zNear, sizeof(float), file, 1);
        AFileRead(&camera->type, sizeof(int), file, 1);
        ReadGLTFString(&camera->name, file, allocator);
    }
    
    if (gltf->numScenes > 0) gltf->scenes = (AScene*)AllocZeroTLSFGlobal(gltf->numScenes, sizeof(AScene));
    for (int i = 0; i < gltf->numScenes; i++)
    {
        AScene* scene = &gltf->scenes[i];
        ReadGLTFString(&scene->name, file, allocator);
        AFileRead(&scene->numNodes, sizeof(int), file, 1);
        scene->nodes = FixedPow2Allocator_AllocateUninitialized(allocator, scene->numNodes * sizeof(int));
        AFileRead(scene->nodes, sizeof(int) * scene->numNodes, file, 1);
    }

    if (gltf->numSkins > 0) gltf->skins = (ASkin*)AllocZeroTLSFGlobal(gltf->numSkins, sizeof(ASkin));
    for (int i = 0; i < gltf->numSkins; i++)
    {
        ASkin* skin = &gltf->skins[i];
        AFileRead(&skin->skeleton, sizeof(int), file, 1);
        AFileRead(&skin->numJoints, sizeof(int), file, 1);
        skin->inverseBindMatrices = (float*)AllocateTLSFGlobal(sizeof(Matrix4) * skin->numJoints);
        skin->joints = FixedPow2Allocator_AllocateUninitialized(allocator, skin->numJoints * sizeof(int));
        AFileRead(skin->inverseBindMatrices, sizeof(Matrix4) * skin->numJoints, file, 1);
        AFileRead(skin->joints, sizeof(int) * skin->numJoints, file, 1);
    }

    int totalAnimSamplerInput = 0;
    AFileRead(&totalAnimSamplerInput, sizeof(int), file, 1);
    float* currSamplerInput;
    Vector4x32f* currSamplerOutput;

    if (totalAnimSamplerInput) {
        currSamplerInput  = (float*)AllocZeroTLSFGlobal(totalAnimSamplerInput, sizeof(float));
        currSamplerOutput = (Vector4x32f*)AllocZeroTLSFGlobal(totalAnimSamplerInput, sizeof(Vector4x32f));
        AFileRead(currSamplerInput, sizeof(float) * totalAnimSamplerInput, file, 1);
        AFileRead(currSamplerOutput, sizeof(Vector4x32f) * totalAnimSamplerInput, file, 1);
    }

    if (gltf->numAnimations) gltf->animations = AllocZeroTLSFGlobal(gltf->numAnimations, sizeof(AAnimation));
    for (int i = 0; i < gltf->numAnimations; i++)
    {
        AAnimation* animation = &gltf->animations[i];

        AFileRead(&animation->numSamplers, sizeof(int), file, 1);
        AFileRead(&animation->numChannels, sizeof(int), file, 1);
        AFileRead(&animation->duration, sizeof(float), file, 1);
        AFileRead(&animation->speed, sizeof(float), file, 1);
        ReadGLTFString(&animation->name, file, allocator);
        animation->channels = AllocateTLSFGlobal(animation->numChannels * sizeof(AAnimChannel));
        AFileRead(animation->channels, sizeof(AAnimChannel) * animation->numChannels, file, 1);
        animation->samplers = AllocateTLSFGlobal(animation->numSamplers * sizeof(AAnimSampler));

        for (int j = 0; j < animation->numSamplers; j++)
        {
            AFileRead(&animation->samplers[j].count, sizeof(int), file, 1);
            AFileRead(&animation->samplers[j].numComponent, sizeof(int), file, 1);
            AFileRead(&animation->samplers[j].interpolation, sizeof(float), file, 1);
            int count = animation->samplers[j].count;
            animation->samplers[j].input = currSamplerInput;
            animation->samplers[j].output = (float*)currSamplerOutput;
            currSamplerInput += count;
            currSamplerOutput += count;
        }
    }

    AFileClose(file);
    gltf->allocator = allocator;
    return 1;
}