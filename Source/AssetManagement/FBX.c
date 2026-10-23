#include "Extern/ufbx.h"
#include "Extern/meshoptimizer/src/meshoptimizer.h"

#include "Include/Common.h"
#include "Include/Algorithm.h"
#include "Include/FileSystem.h"
#include "Include/GLTFParser.h"
#include "Include/Platform.h"
#include "Include/DataStructures/Array.h"
#include "Math/Color.h"

extern void SceneBundle_Normalize(SceneBundle* scene);

/*//////////////////////////////////////////////////////////////////////////*/
/*                              FBX LOAD                                    */
/*//////////////////////////////////////////////////////////////////////////*/

#if !AX_GAME_BUILD
static u16 GetFBXTexture(const ufbx_material* umaterial, const ufbx_scene* uscene, ufbx_material_feature feature, ufbx_material_pbr_map pbr, ufbx_material_fbx_map fbx)
{
    (void)uscene;
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
            // typed_id is the texture's index within uscene->textures (which we mirror 1:1).
            return (u16)texture->typed_id;
        }
    }
    return -1;
}

// ufbx folds the legacy FBX TransparencyFactor/Color into pbr.opacity, so relying on the
// normalized pbr value avoids double counting. Default opacity is 1.0 (fully opaque).
static f32 GetUFBXOpacity(const ufbx_material* material)
{
    f32 opacity = 1.0f;
    if (material->pbr.opacity.has_value || material->pbr.opacity.value_components >= 1)
        opacity = (f32)material->pbr.opacity.value_real;
    return Saturatef32(opacity);
}

static u32 PackUFBXBaseColorFactor(const ufbx_material* material, f32 opacity)
{
    ufbx_vec3 color = { 1.0f, 1.0f, 1.0f };
    f32 factor = 1.0f;

    // Only override the white/1.0 defaults when the material actually defines the value.
    // ufbx always fills value_vec3/value_real with a fallback (often 0) even when the
    // property is unset, so gating on value_components would tint everything black.
    if (material->features.pbr.enabled)
    {
        if (material->pbr.base_color.has_value)         color  = material->pbr.base_color.value_vec3;
        else if (material->fbx.diffuse_color.has_value) color = material->fbx.diffuse_color.value_vec3;

        if (material->pbr.base_factor.has_value)       factor = (f32)material->pbr.base_factor.value_real;
    }
    else
    {
        if (material->fbx.diffuse_color.has_value)     color  = material->fbx.diffuse_color.value_vec3;
        else if (material->pbr.base_color.has_value)   color  = material->pbr.base_color.value_vec3;

        if (material->fbx.diffuse_factor.has_value)    factor = (f32)material->fbx.diffuse_factor.value_real;
    }
    return PackColor3PtrToUint(&color.x);
}

static char* GetNameFromFBX(ufbx_string ustr, Pow2Allocator* stringAllocator)
{
    if (ustr.length == 0) return NULL;
    char* name = Pow2Alloc_Uninitialized(stringAllocator, ustr.length + 1);
    SmallMemCpy(name, ustr.data, ustr.length);
    name[ustr.length] = 0;
    return name;
}

static bool CopyUFBXPath(char* dst, s32 dstSize, const char* baseDir, s32 baseLen, ufbx_string path)
{
    if (dstSize <= 0) return false;
    dst[0] = '\0';
    if (path.length == 0 || path.length >= (size_t)dstSize) return false;

    s32 prefixLen = baseDir ? baseLen : 0;
    s32 copyLen = (s32)path.length;
    if (copyLen <= 0 || prefixLen + copyLen >= dstSize) return false;

    if (prefixLen > 0)
        SmallMemCpy(dst, baseDir, prefixLen);
    SmallMemCpy(dst + prefixLen, path.data, copyLen);
    dst[prefixLen + copyLen] = '\0';
    return true;
}

static s32 CopyUFBXFilename(char* dst, s32 dstSize, ufbx_string path)
{
    if (dstSize <= 0) return 0;
    dst[0] = '\0';
    if (path.length == 0 || path.length >= (size_t)dstSize) return 0;

    s32 start = (s32)path.length;
    while (start > 0 && path.data[start - 1] != '/' && path.data[start - 1] != '\\')
        start--;

    s32 copyLen = (s32)path.length - start;
    if (copyLen >= dstSize) copyLen = dstSize - 1;
    if (copyLen <= 0) return 0;

    SmallMemCpy(dst, path.data + start, copyLen);
    dst[copyLen] = '\0';
    return copyLen;
}

// Resolve an FBX texture file to a real path on this machine so the basis encoder
// (SaveSceneImages) has a source to compress. Prefers <fbxDir>/<relative_filename> because
// the absolute path baked into an FBX usually points at the artist's drive; embedded image
// blobs are written out next to the FBX. Returns an allocator-owned, possibly-empty path.
static char* ResolveFBXImagePath(const char* fbxBaseDir, s32 fbxBaseLen,
                                 ufbx_string filename, ufbx_string absolute_filename, ufbx_string relative_filename,
                                 ufbx_blob content, s32 index, Pow2Allocator* allocator)
{
    enum { PATH_CAP = 1024 };
    char* out = (char*)Pow2Alloc_Uninitialized(allocator, PATH_CAP);
    out[0] = '\0';

    if (content.size > 0)
    {
        // Embedded image data: derive a file name and write the blob beside the FBX.
        char nameBuf[256];
        s32 nameLen = 0;
        const ufbx_string* src = filename.length ? &filename
            : (relative_filename.length ? &relative_filename : NULL);
        if (src)
        {
            nameLen = CopyUFBXFilename(nameBuf, sizeof(nameBuf), *src);
        }
        else
        {
            const char prefix[] = "fbx_embedded_";
            SmallMemCpy(nameBuf, prefix, sizeof(prefix) - 1);
            s32 n = IntToString(nameBuf + sizeof(prefix) - 1, index, 0);
            SmallMemCpy(nameBuf + sizeof(prefix) - 1 + n, ".png", 4);
            nameLen = (s32)(sizeof(prefix) - 1) + n + 4;
        }
        nameBuf[nameLen] = '\0';

        if (fbxBaseLen + nameLen < PATH_CAP)
        {
            SmallMemCpy(out, fbxBaseDir, fbxBaseLen);
            SmallMemCpy(out + fbxBaseLen, nameBuf, nameLen + 1);
            WriteAllBytes(out, (const char*)content.data, (unsigned long)content.size);
        }
        else
        {
            AX_WARN("fbx embedded texture path too long, skipping image %d: %s", index, nameBuf);
            out[0] = '\0';
        }
        return out;
    }

    // External file: FBX-relative path resolves on this machine, try it first.
    if (relative_filename.length && fbxBaseLen + (s32)relative_filename.length < PATH_CAP)
    {
        CopyUFBXPath(out, PATH_CAP, fbxBaseDir, fbxBaseLen, relative_filename);
    }
    // Fall back to the stored absolute path, then the raw filename.
    if ((out[0] == '\0' || !FileExist(out)) && absolute_filename.length && absolute_filename.length < PATH_CAP)
        CopyUFBXPath(out, PATH_CAP, NULL, 0, absolute_filename);
    if (out[0] == '\0' && filename.length && filename.length < PATH_CAP)
        CopyUFBXPath(out, PATH_CAP, NULL, 0, filename);

    if (out[0] == '\0' || !FileExist(out))
        AX_WARN("fbx texture source not found on disk, compression skipped for image %d: %s", index, out[0] ? out : "<empty>");

    return out;
}
#endif

s32 LoadFBX(const char* path, SceneBundle* fbxScene, f32 scale)
{
    #if !AX_GAME_BUILD
    s32 pathLen = StringLength(path);
    bool isObj = FileHasExtension(path, pathLen, ".obj");
    const char* importType = isObj ? "obj" : "fbx";

    MemsetZero(fbxScene, sizeof(SceneBundle));
    AX_LOG("%s import: %s scale=%f", importType, path, scale);

    ufbx_load_opts opts = { 0 };
    // ufbx imports FBX/OBJ as static scene data here; animation stacks are intentionally ignored.
    opts.evaluate_skinning = false;
    opts.evaluate_caches = false;
    opts.load_external_files = isObj; // OBJ needs this for implicitly referenced .mtl files.
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
        AX_WARN("%s mesh load failed! %s", importType, error.info);
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
    AX_LOG("%s parse: nodes=%d meshes=%d materials=%d textures=%d cameras=%d skins=%d",
           importType,
           fbxScene->numNodes, fbxScene->numMeshes, fbxScene->numMaterials,
           fbxScene->numTextures, fbxScene->numCameras, (s32)uscene->skin_deformers.count);
    if (uscene->skin_deformers.count > 0)
        AX_WARN("%s contains skin deformers, but this importer is intended for non-skinned meshes only: %s", importType, path);
    
    Pow2Allocator* allocator = AllocTLSF(sizeof(Pow2Allocator));
    Pow2Alloc_Init(allocator, 2048);

    // Static import only: skinning and animation are intentionally dropped so the
    // baker selects the surface (non-skinned) vertex path. See plan: skip animation.
    fbxScene->numSkins      = 0;
    fbxScene->skins         = NULL;
    fbxScene->numAnimations = 0;
    fbxScene->animations    = NULL;

    if (fbxScene->numMeshes) fbxScene->meshes = (AMesh*)AllocZeroTLSF(fbxScene->numMeshes, sizeof(AMesh));
    fbxScene->scenes = (AScene*)AllocZeroTLSF(1, sizeof(AScene));

    s32 totalIndices = 0, totalVertices = 0;

    // Build the same intermediate representation ParseGLTF produces: per-primitive raw
    // float attribute arrays + an index buffer. BakeSceneMeshesAndAnimations() then
    // compresses, generates LODs and uploads to the GPU geometry heap, identical to GLTF.
    for (s32 i = 0; i < fbxScene->numMeshes; i++)
    {
        AMesh* amesh = &fbxScene->meshes[i];
        ufbx_mesh* umesh = uscene->meshes.data[i];
        amesh->name = GetNameFromFBX(umesh->name, allocator);
        amesh->primitives = ArrayCreatePrealloc(APrimitive, 1);
        amesh->numPrimitives = 1;

        APrimitive* primitive = &amesh->primitives[0];
        MemsetZero(primitive, sizeof(APrimitive));
        primitive->material    = 0;
        primitive->mode        = 4;
        primitive->indiceIndex = UINT16_MAX;
        primitive->indexType   = AComponentType_UNSIGNED_INT;

        if (umesh->num_triangles == 0 || umesh->num_vertices == 0)
        {
            AX_WARN("fbx mesh %d has no renderable triangles/vertices", i);
            continue;
        }

        bool hasUV     = umesh->vertex_uv.exists;
        bool hasNormal = umesh->vertex_normal.exists;
        bool hasColor  = umesh->vertex_color.exists;
        if (!hasUV)
            AX_WARN("fbx mesh %d (%s) has no UVs", i, amesh->name ? amesh->name : "<unnamed>");

        // Flatten every triangulated face corner into temporary attribute streams.
        // cornerCapacity is fixed for the arena push/pop pairing; numCorners is the
        // actually-emitted count fed to meshopt (they match unless a face is degenerate).
        s32 cornerCapacity = (s32)umesh->num_triangles * 3;
        float3* cornerPos = (float3*)ArenaPushGlobal((u64)cornerCapacity * sizeof(float3));
        float2* cornerUV  = hasUV     ? (float2*)ArenaPushGlobal((u64)cornerCapacity * sizeof(float2)) : NULL;
        float3* cornerNrm = hasNormal ? (float3*)ArenaPushGlobal((u64)cornerCapacity * sizeof(float3)) : NULL;
        v128f* cornerColor = hasColor ? (v128f*)ArenaPushGlobal((u64)cornerCapacity * sizeof(v128f)) : NULL;

        u32 triIndices[64];
        s32 corner = 0;
        for (s32 f = 0; f < (s32)umesh->faces.count; f++)
        {
            ufbx_face face = umesh->faces.data[f];
            u32 numTris = ufbx_triangulate_face(triIndices, ARRAY_SIZE(triIndices), umesh, face);
            for (u32 c = 0; c < numTris * 3u && corner < cornerCapacity; c++, corner++)
            {
                u32 ci = triIndices[c];
                ufbx_vec3 p = ufbx_get_vertex_vec3(&umesh->vertex_position, ci);
                cornerPos[corner] = (float3){ p.x, p.y, p.z };
                if (hasUV)
                {
                    // FBX UVs are bottom-left origin (V up); the rest of the pipeline assumes
                    // glTF top-left origin (V down), so flip V to match baked glTF meshes.
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&umesh->vertex_uv, ci);
                    cornerUV[corner] = (float2){ uv.x, 1.0f - uv.y };
                }
                if (hasNormal)
                {
                    ufbx_vec3 n = ufbx_get_vertex_vec3(&umesh->vertex_normal, ci);
                    cornerNrm[corner] = (float3){ n.x, n.y, n.z };
                }
                if (hasColor)
                {
                    ufbx_vec4 color = ufbx_get_vertex_vec4(&umesh->vertex_color, ci);
                    cornerColor[corner] = VecSetR((f32)color.x, (f32)color.y, (f32)color.z, (f32)color.w);
                }
            }
        }
        s32 numCorners = corner;

        // Collapse binary-equal corners into unique vertices + a real index buffer.
        struct meshopt_Stream streams[4];
        size_t streamCount = 0;
        streams[streamCount++] = (struct meshopt_Stream){ cornerPos, sizeof(float3), sizeof(float3) };
        if (hasUV)     streams[streamCount++] = (struct meshopt_Stream){ cornerUV,  sizeof(float2), sizeof(float2) };
        if (hasNormal) streams[streamCount++] = (struct meshopt_Stream){ cornerNrm, sizeof(float3), sizeof(float3) };
        if (hasColor)  streams[streamCount++] = (struct meshopt_Stream){ cornerColor, sizeof(v128f), sizeof(v128f) };

        u32* remap = (u32*)ArenaPushGlobal((u64)numCorners * sizeof(u32));
        size_t uniqueCount = meshopt_generateVertexRemapMulti(remap, NULL, (size_t)numCorners, (size_t)numCorners, streams, streamCount);

        // Final attribute arrays live in the bundle allocator so they survive until bake.
        // One extra position slot pads wide v128f loads in BoundsForPrimitive().
        float3* finalPos = (float3*)Pow2Alloc(allocator, (uniqueCount + 1) * sizeof(float3));
        meshopt_remapVertexBuffer(finalPos, cornerPos, (size_t)numCorners, sizeof(float3), remap);
        primitive->Attributes[AAttribIdx_POSITION] = finalPos;
        primitive->attributes = AAttribType_POSITION;

        if (hasUV)
        {
            float2* finalUV = (float2*)Pow2Alloc(allocator, uniqueCount * sizeof(float2));
            meshopt_remapVertexBuffer(finalUV, cornerUV, (size_t)numCorners, sizeof(float2), remap);
            primitive->Attributes[AAttribIdx_TEXCOORD_0] = finalUV;
            primitive->attributes |= AAttribType_TEXCOORD_0;
        }
        if (hasNormal)
        {
            float3* finalNrm = (float3*)Pow2Alloc(allocator, uniqueCount * sizeof(float3));
            meshopt_remapVertexBuffer(finalNrm, cornerNrm, (size_t)numCorners, sizeof(float3), remap);
            primitive->Attributes[AAttribIdx_NORMAL] = finalNrm;
            primitive->attributes |= AAttribType_NORMAL;
        }
        if (hasColor)
        {
            v128f* finalColor = (v128f*)Pow2Alloc(allocator, uniqueCount * sizeof(v128f));
            meshopt_remapVertexBuffer(finalColor, cornerColor, (size_t)numCorners, sizeof(v128f), remap);
            primitive->Attributes[AAttribIdx_COLOR_0] = finalColor;
            primitive->attributes |= AAttribType_COLOR_0;
            primitive->colorType = AComponentType_FLOAT;
            primitive->colorCount = 4;
            primitive->colorStride = sizeof(v128f);
        }

        u32* finalIdx = (u32*)Pow2Alloc(allocator, (u64)numCorners * sizeof(u32));
        meshopt_remapIndexBuffer(finalIdx, NULL, (size_t)numCorners, remap);

        primitive->numVertices = (s32)uniqueCount;
        primitive->numIndices  = numCorners;
        primitive->indices     = finalIdx;

        ArenaPopGlobal((u64)numCorners * sizeof(u32));
        if (hasColor)  ArenaPopGlobal((u64)cornerCapacity * sizeof(v128f));
        if (hasNormal) ArenaPopGlobal((u64)cornerCapacity * sizeof(float3));
        if (hasUV)     ArenaPopGlobal((u64)cornerCapacity * sizeof(float2));
        ArenaPopGlobal((u64)cornerCapacity * sizeof(float3));

        totalIndices  += primitive->numIndices;
        totalVertices += primitive->numVertices;
    }

    fbxScene->totalIndices  = totalIndices;
    fbxScene->totalVertices = totalVertices;

    if (uscene->skin_deformers.count > 0)
        AX_WARN("%s skin deformers ignored (static import only): %s", importType, path);

    // Build the image list from the textures themselves: ufbx does not always populate
    // scene->texture_files, but every FILE texture carries its own path/content. Resolve and
    // dedup by path so repeated references share one basis-compressed output.
    char fbxBaseDir[1024];
    GetBaseDir(path, fbxBaseDir);
    s32 fbxBaseLen = StringLengthSafe(fbxBaseDir, sizeof(fbxBaseDir));

    u16 numTextures = (u16)uscene->textures.count;
    fbxScene->numTextures = numTextures;
    fbxScene->numSamplers = numTextures;

    AImage* images = ArrayCreatePrealloc(AImage, numTextures ? numTextures : 1);

    if (numTextures)
    {
        fbxScene->textures = (ATexture*)AllocZeroTLSF(numTextures, sizeof(ATexture));
        fbxScene->samplers = (ASampler*)AllocZeroTLSF(numTextures, sizeof(ASampler));
    }

    for (u16 i = 0; i < numTextures; i++)
    {
        ufbx_texture* utexture = uscene->textures.data[i];
        ATexture* atexture = &fbxScene->textures[i];

        atexture->name    = GetNameFromFBX(utexture->name, allocator);
        atexture->sampler = i;
        atexture->source  = 0;
        fbxScene->samplers[i].wrapS = utexture->wrap_u;
        fbxScene->samplers[i].wrapT = utexture->wrap_v;

        // Only FILE textures map to an image source.
        if (utexture->type != UFBX_TEXTURE_FILE || (utexture->filename.length == 0 && utexture->content.size == 0))
            continue;

        char* resolved = ResolveFBXImagePath(fbxBaseDir, fbxBaseLen, utexture->filename,
                                             utexture->absolute_filename, utexture->relative_filename,
                                             utexture->content, i, allocator);
        if (resolved[0] == '\0')
            continue;

        // Reuse an existing image entry when the same file is referenced again.
        s32 found = -1;
        s32 resolvedLen = StringLength(resolved);
        for (s32 k = 0; k < (s32)ArrayLength(images); k++)
            if (images[k].path && StringLength(images[k].path) == resolvedLen &&
                StringEqual(images[k].path, resolved, resolvedLen))
            {
                found = k;
                break;
            }

            if (found >= 0)
                atexture->source = found;
            else
            {
                atexture->source = (s32)ArrayLength(images);
                ArrayPush(images, (AImage){ resolved });
            }
    }
    
    u16 numMaterials = (u16)uscene->materials.count;
    fbxScene->numMaterials = numMaterials;
    
    // Zero-init so unset AMaterial fields get clean defaults. alphaMode/alphaCutoff are
    // consumed by PackMaterialFlags and the opaque/transparent render-set split; leaving them
    // as garbage pulls meshes into the blended pass or alpha-clips them away (renders black).
    if (numMaterials) {
        fbxScene->materials = AllocZeroTLSF(numMaterials, sizeof(AMaterial));
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
        
        // index defaults to UINT16_MAX (no texture); 0 would alias bundle texture slot 0.
        amaterial->textures[0].index = normalTexture ? (u16)normalTexture->typed_id : UINT16_MAX;
        
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
        
        f32 opacity = GetUFBXOpacity(umaterial);
        // metallicFactor/roughnessFactor are unorm16 (value * UINT16_MAX), like the GLTF path.
        // MakeFloat16 (value * 400) would land roughness=1.0 at 400/65535 ~= 0.006 (a mirror),
        // which with only a sun and no IBL renders nearly black. Default to dielectric/matte
        // (0 / 1) when the material does not define them, matching the engine's reset material.
        f32 metalness = umaterial->pbr.metalness.has_value ? (f32)umaterial->pbr.metalness.value_real : 0.0f;
        f32 roughness = umaterial->pbr.roughness.has_value ? (f32)umaterial->pbr.roughness.value_real : 1.0f;
        amaterial->metallicFactor   = PackUnorm16(Saturatef32(metalness));
        amaterial->roughnessFactor  = PackUnorm16(Saturatef32(roughness));
        amaterial->baseColorFactor  = PackUFBXBaseColorFactor(umaterial, opacity);
        amaterial->alphaCutoff      = 0.5f;
        amaterial->alphaMode        = opacity < 0.999f ? AMaterialAlphaMode_Blend : AMaterialAlphaMode_Opaque;
        
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
        fbxScene->nodes = (ANode*)AllocZeroTLSF(numNodes, sizeof(ANode));

    fbxScene->rootNode = uscene->root_node ? (s32)uscene->root_node->typed_id : 0;
    if (fbxScene->rootNode < 0) fbxScene->rootNode = 0;
    fbxScene->scenes[0].numNodes = 1;
    fbxScene->scenes[0].nodes = Pow2Alloc_Uninitialized(allocator, sizeof(s32));
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
            anode->children = Pow2Alloc_Uninitialized(allocator, sizeof(s32) * anode->numChildren);
        
        for (s32 j = 0; j < anode->numChildren; j++)
        {
            anode->children[j] = (s32)unode->children.data[j]->typed_id;
        }
        
        SmallMemCpy(anode->translation, &unode->local_transform.translation.x, sizeof(float3));
        SmallMemCpy(anode->rotation, &unode->local_transform.rotation.x, sizeof(v128f));
        SmallMemCpy(anode->scale, &unode->local_transform.scale.x, sizeof(float3));
        
        if (unode->mesh)
        {
            anode->index = (s32)unode->mesh->typed_id;
            if (anode->index >= 0 && unode->materials.count > 0)
                fbxScene->meshes[anode->index].primitives[0].material = (u16)unode->materials.data[0]->typed_id;
        }
        else if (unode->camera)
            anode->index = (s32)unode->camera->typed_id;
    }
    
    fbxScene->numImages = ArrayLength(images);
    fbxScene->images    = images;
    fbxScene->allocator = allocator;
    fbxScene->scale = scale;
    fbxScene->error = AError_NONE;
    SceneBundle_Normalize(fbxScene);
    AX_LOG("%s import complete: %s", importType, path);
    ufbx_free_scene(uscene);
    #endif // android
    return 1;
}
