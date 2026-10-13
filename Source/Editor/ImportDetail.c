//------------------------------------------------------------------------
// Import with detail popup
#include "Include/UIWindow.h"
#include "Include/AssetManager.h"
#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/Algorithm.h"
#include "Include/GLTFParser.h"
#include "Include/FileSystem.h"

typedef struct ImportDetailInfo_
{
    u32 meshes;
    u32 nodes;
    u32 materials;
    u32 images;
    u32 vertices;
    u32 indices;
    u32 skins;
    u32 animations;
    f32 maxSkinnedAnimMeters;
    f32 recommendedScale;
    bool animBoundsWarning;
} ImportDetailInfo;

static bool importDetailOpen;
static char importDetailPath[512];
static f32  importDetailScale = 1.0f;
static f32  importDetailEuler[3];
static char importDetailScaleText[32];

static ImportDetailInfo importDetailInfo;
extern WindowState    g_WindowState;

extern Scene* EditorNewScene(void);

static void ImportDetailSetScaleText(f32 scale)
{
    importDetailScale = scale;
    int len = FloatToString(importDetailScaleText, scale, 4);
    importDetailScaleText[len] = '\0';
}

static f32 ImportRecommendAnimationScale(f32 maxMeters)
{
    if (maxMeters <= 0.0f) return 1.0f;
    f32 exact = ANIMATION_MAX_METERS / maxMeters;
    if (exact >= 1.0f)  return 1.0f;
    if (exact >= 0.1f)  return 0.1f;
    if (exact >= 0.01f) return 0.01f;
    return 0.01f;
}

static bool ImportPositionValid(v128f position)
{
    f32 maxAbs = Max3(VecFabs(position));
    return maxAbs == maxAbs && maxAbs < 100000.0f;
}

static void ImportDetailSetAnimationBoundsInfo(ImportDetailInfo* info, const SceneBundle* bundle)
{
    if (!info || !bundle || bundle->numSkins <= 0) return;

    f32 maxMeters = 0.0f;
    bool foundBounds = false;
    for (s32 m = 0; m < bundle->numMeshes; m++)
    {
        const AMesh* mesh = &bundle->meshes[m];
        for (s32 p = 0; p < mesh->numPrimitives; p++)
        {
            const APrimitive* primitive = &mesh->primitives[p];
            const float3* positions = (const float3*)primitive->Attributes[AAttribIdx_POSITION];
            bool skinned = bundle->numSkins > 0;
            if (!positions || (float3*)(0xcdcdcdcdcdcdcdcdull) == positions)
            {
                if (skinned)
                {
                    const ASkinedVertex* vertices = (ASkinedVertex*)primitive->vertices;
                    for (s32 v = 0; v < primitive->numVertices; v++)
                    {
                        v128f position = Half4ToFloat4Vec(&vertices[v].positionXY);
                        if (!ImportPositionValid(position)) continue;
                        maxMeters = Maxf32(maxMeters, Max3(VecFabs(position)));
                        foundBounds = true;
                    }
                }
                else
                {
                    v128f primMin = VecLoad(primitive->min);
                    v128f primExtend = VecMax(VecSub(VecLoad(primitive->max), primMin), VecSet1(1.0e-6f));
                    for (s32 v = 0; v < primitive->numVertices; v++)
                    {
                        const AVertex* v = (const AVertex*)primitive->vertices;
                        v128f position = VecAdd(primMin, VecMul(UnpackUnorm16x4(v->position), primExtend));
                        if (!ImportPositionValid(position)) continue;
                        maxMeters = Maxf32(maxMeters, Max3(VecFabs(position)));
                        foundBounds = true;
                    }
                }
                continue;
            }
            for (s32 v = 0; v < primitive->numVertices; v++)
            {
                v128f position = Vec3Load(&positions[v].x);
                if (!ImportPositionValid(position)) continue;
                maxMeters = Maxf32(maxMeters, Max3(VecFabs(position)));
                foundBounds = true;
            }
        }
    }

    if (!foundBounds)
    {
        AX_LOG("couldn't found bounds of mesh when importing");
        return;
    }
    info->maxSkinnedAnimMeters = maxMeters;
    info->recommendedScale = ImportRecommendAnimationScale(maxMeters);
    info->animBoundsWarning = maxMeters > ANIMATION_MAX_METERS;
}

static bool EditorReadBundleInfoFromScene(const char* path, ImportDetailInfo* info)
{
    Scene* scene = GetActiveScene();
    if (!scene) return false;

    u32 pathLen = (u32)StringLength(path) + 1u;
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        if (scene->bundleRefs[i].bundle && StringEqual(scene->bundleRefs[i].path, path, pathLen))
        {
            const SceneBundle* bundle = scene->bundleRefs[i].bundle;
            *info = (ImportDetailInfo){
                .meshes     = (u32)bundle->numMeshes,
                .nodes      = (u32)bundle->numNodes,
                .materials  = (u32)bundle->numMaterials,
                .images     = (u32)bundle->numImages,
                .vertices   = (u32)bundle->totalVertices,
                .indices    = (u32)bundle->totalIndices,
                .skins      = (u32)bundle->numSkins,
                .animations = (u32)bundle->numAnimations
            };
            ImportDetailSetAnimationBoundsInfo(info, bundle);
            return true;
        }
    }
    return false;
}

static bool EditorReadBundleInfoFromABM(const char* path, ImportDetailInfo* info)
{
    char buffer[1024];
    int pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    ChangeExtension(buffer, pathLen, "abm");
    if (!IsABMLastVersion(buffer)) return false;

    AFile file = AFileOpen(buffer, AOpenFlag_ReadBinary);
    if (!AFileExist(file)) return false;

    s32 version;
    u64 reserved[4];
    f32 scale;
    u16 numMeshes, numNodes, numMaterials, numTextures, numImages, numSamplers;
    u16 numCameras, numScenes, numSkins, numAnimations, defaultSceneIndex, isSkinned;
    s32 totalIndices, totalVertices;
    AFileRead(&version          , sizeof(version)          , file, 1);
    AFileRead(reserved          , sizeof(reserved)         , file, 1);
    AFileRead(&scale            , sizeof(scale)            , file, 1);
    AFileRead(&numMeshes        , sizeof(numMeshes)        , file, 1);
    AFileRead(&numNodes         , sizeof(numNodes)         , file, 1);
    AFileRead(&numMaterials     , sizeof(numMaterials)     , file, 1);
    AFileRead(&numTextures      , sizeof(numTextures)      , file, 1);
    AFileRead(&numImages        , sizeof(numImages)        , file, 1);
    AFileRead(&numSamplers      , sizeof(numSamplers)      , file, 1);
    AFileRead(&numCameras       , sizeof(numCameras)       , file, 1);
    AFileRead(&numScenes        , sizeof(numScenes)        , file, 1);
    AFileRead(&numSkins         , sizeof(numSkins)         , file, 1);
    AFileRead(&numAnimations    , sizeof(numAnimations)    , file, 1);
    AFileRead(&defaultSceneIndex, sizeof(defaultSceneIndex), file, 1);
    AFileRead(&isSkinned        , sizeof(isSkinned)        , file, 1);
    AFileRead(&totalIndices     , sizeof(totalIndices)     , file, 1);
    AFileRead(&totalVertices    , sizeof(totalVertices)    , file, 1);
    AFileClose(file);

    (void)version; (void)reserved; (void)scale; (void)numTextures; (void)numSamplers; 
    (void)numCameras; (void)numScenes; (void)defaultSceneIndex; (void)isSkinned;
    *info = (ImportDetailInfo){
        .meshes     = (u32)numMeshes,
        .nodes      = (u32)numNodes,
        .materials  = (u32)numMaterials,
        .images     = (u32)numImages,
        .vertices   = (u32)Maxs32(totalVertices, 0),
        .indices    = (u32)Maxs32(totalIndices, 0),
        .skins      = (u32)numSkins,
        .animations = (u32)numAnimations
    };
    return true;
}

static bool EditorReadBundleInfoFromGLTF(const char* path, ImportDetailInfo* info)
{
    SceneBundle bundle;
    bool loaded = false;
    int pathLen = StringLength(path);
    if (FileHasExtension(path, pathLen, ".glb") || FileHasExtension(path, pathLen, ".gltf"))
        loaded = ParseGLTF(path, &bundle, 1.0f) != 0;
    else if (FileHasExtension(path, pathLen, ".abm"))
        return EditorReadBundleInfoFromABM(path, info);
    else // fbx obj
        loaded = LoadFBX(path, &bundle, 1.0f) != 0;
    
    if (!loaded) return false;
    *info = (ImportDetailInfo){
        .meshes     = (u32)bundle.numMeshes,
        .nodes      = (u32)bundle.numNodes,
        .materials  = (u32)bundle.numMaterials,
        .images     = (u32)bundle.numImages,
        .vertices   = (u32)bundle.totalVertices,
        .indices    = (u32)bundle.totalIndices,
        .skins      = (u32)bundle.numSkins,
        .animations = (u32)bundle.numAnimations
    };
    ImportDetailSetAnimationBoundsInfo(info, &bundle);
    FreeSceneBundle(&bundle);
    return true;
}

static bool EditorReadBundleInfo(const char* path, ImportDetailInfo* info)
{
    if (EditorReadBundleInfoFromScene(path, info)) return true;
    if (EditorReadBundleInfoFromGLTF(path, info)) return true;
    return EditorReadBundleInfoFromABM(path, info);
}

static bool EditorImportNeedsDetailWarning(const char* normalizedPath)
{
    ImportDetailInfo info;
    if (!EditorReadBundleInfo(normalizedPath, &info)) return false;
    return info.animBoundsWarning;
}

// todo(anil): remove this
void EditorOpenImportDetail(const char* path)
{
    char normalized[512];
    NormalizePath(path, normalized, sizeof(normalized));
    ImportDetailInfo info;
    if (!EditorReadBundleInfo(normalized, &info))
    {
        AX_ERROR("mesh load failed: %s", normalized);
        return;
    }
    MemCopy(importDetailPath, normalized, StringLength(normalized) + 1);
    importDetailInfo = info;
    ImportDetailSetScaleText(info.animBoundsWarning ? info.recommendedScale : 1.0f);
    importDetailEuler[0] = 0.0f;
    importDetailEuler[1] = 0.0f;
    importDetailEuler[2] = 0.0f;
    importDetailOpen = true;
}

void SceneImportDetailPopup(void)
{
    if (!importDetailOpen)
        return;

    float2 windowSize = importDetailInfo.animBoundsWarning ? (float2){ 720.0f, 780.0f } : (float2){ 620.0f, 680.0f };
    float2 center = {
        Maxf32(10.0f, g_WindowState.prev_width * 0.5f - windowSize.x * 0.5f),
        Maxf32(10.0f, g_WindowState.prev_height * 0.5f - windowSize.y * 0.5f)
    };
    if (!UIBeginWindow("Import Mesh", center, windowSize, &importDetailOpen, 0)) return;

    bool skinned = importDetailInfo.skins > 0u;

    CLAY_TEXT(UIStr(GetFileName(importDetailPath)), CLAY_TEXT_CONFIG({
        .fontSize = 16,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    UIDivider(CLAY_ID("ImportDetailDivider"));

    CLAY(CLAY_ID("ImportDetailInfo"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
            .padding = { 6, 0, 0, 0 },
            .childGap = 4,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
    }) {
        UITextU32("Meshes", importDetailInfo.meshes);
        UITextU32("Nodes", importDetailInfo.nodes);
        UITextU32("Materials", importDetailInfo.materials);
        UITextU32("Images", importDetailInfo.images);
        UITextU32("Vertices", importDetailInfo.vertices);
        UITextU32("Triangles", importDetailInfo.indices / 3u);
        UITextU32("Skins", importDetailInfo.skins);
        UITextU32("Animations", importDetailInfo.animations);
    }

    if (skinned && importDetailInfo.animations > 0u)
    {
        u32 defaultAnim = importDetailInfo.animations > 1u ? 1u : 0u;
        char* text = UIFrameStringAlloc(96u);
        if (text)
        {
            u32 len = (u32)StringLength("Default animation: ");
            MemCopy(text, "Default animation: ", len);
            len += (u32)IntToString(text + len, (int64_t)defaultAnim, 0);
            text[len] = '\0';
            CLAY_TEXT(((Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text }),
                      CLAY_TEXT_CONFIG({ .fontSize = 13, .textColor = UIGetClayColor(UIColor_SubText) }));
        }
    }

    if (importDetailInfo.animBoundsWarning)
    {
        char* text = UIFrameStringAlloc(384u);
        if (text)
        {
            u32 len = 0u;
            const char* a = "Warning: this object is bigger than usual ";
            u32 n = (u32)StringLength(a); MemCopy(text + len, a, n); len += n;
            len += (u32)FloatToString(text + len, ANIMATION_MAX_METERS, 3);
            const char* b = "m.\nit mesh reaches ";
            n = (u32)StringLength(b); MemCopy(text + len, b, n); len += n;
            len += (u32)FloatToString(text + len, importDetailInfo.maxSkinnedAnimMeters, 3);
            const char* c = "m\nRecommended import scale: ";
            n = (u32)StringLength(c); MemCopy(text + len, c, n); len += n;
            len += (u32)FloatToString(text + len, importDetailInfo.recommendedScale, 4);
            text[len] = '\0';
            CLAY_TEXT(((Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text }),
                      CLAY_TEXT_CONFIG({ .fontSize = 13, .textColor = UIColorToClay(0xFF30B0FFu), .wrapMode = CLAY_TEXT_WRAP_WORDS }));
        }
    }

    static UITextAreaCustomData scaleData;
    scaleData.type = UICustomType_TextArea;
    scaleData.buffer = importDetailScaleText;
    scaleData.capacity = sizeof(importDetailScaleText);
    scaleData.flags = UITextAreaFlags_CenterX | UITextAreaFlags_CenterY | UITextAreaFlags_NoWrap | UITextAreaFlags_Clip;
    CLAY(CLAY_ID("ImportDetailScaleRow"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(CLAY_STRING("Scale"), CLAY_TEXT_CONFIG({ .fontSize = 15, .textColor = UIGetClayColor(UIColor_Text) }));
        CLAY(CLAY_ID_LOCAL("Spacer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } } }) {}
        CLAY(CLAY_ID_LOCAL("Text"), {
            .layout = { .sizing = { CLAY_SIZING_FIXED(110.0f), CLAY_SIZING_FIXED(28.0f) } },
            .custom = { .customData = &scaleData }
        }) {}
    }

    UIEditFloatN("Euler", importDetailEuler, 3u, -360.0f, 360.0f, 3);

    CLAY(CLAY_ID("ImportDetailButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton("Import", (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            ParseFloat(importDetailScaleText, &importDetailScale);
            Scene* scene = GetActiveScene();
            if (!scene) scene = EditorNewScene();
            u32 bundleIdx = Scene_AddBundleFromPath(scene, importDetailPath);
            if (bundleIdx != INVALID_BUNDLE)
            {
                v128f rotation = VecNorm(QFromEuler(importDetailEuler[0] * MATH_DegToRad,
                                                    importDetailEuler[1] * MATH_DegToRad,
                                                    importDetailEuler[2] * MATH_DegToRad));
                Scene_Spawn(scene, bundleIdx, VecZero(), rotation, VecSet1(importDetailScale), EntityFlags_ColliderEnabled);
            }
            else
                AX_ERROR("import to scene failed: %s", importDetailPath);
            importDetailOpen = false;
        }
        if (UIButton("Cancel", (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            importDetailOpen = false;
        }
    }
    UIEndWindow();
}

static void ImportMeshToSceneFinish(SceneAsyncRequest* request)
{
    Scene* scene = GetActiveScene();
    if (!scene) scene = EditorNewScene();

    u32 bundleIdx = Scene_AddBundleFinalize(scene, request->sceneBundleStage);
    if (bundleIdx == INVALID_BUNDLE)
    {
        AX_ERROR("import to scene failed: %s", request->path);
        return;
    }

    Scene_Spawn(scene, bundleIdx, VecZero(), QIdentity(), VecOne(), EntityFlags_ColliderEnabled);
}

void EditorImportMeshToScene(const char* path)
{
    char normalized[512];
    NormalizePath(path, normalized, sizeof(normalized));
    if (EditorImportNeedsDetailWarning(normalized))
    {
        EditorOpenImportDetail(normalized);
        return;
    }

    SceneAsyncBegin(SceneAsyncOp_ImportMesh, path, "EditorImportMesh", ImportMeshToSceneFinish);
}