// scene hierarchy and texture system inspector windows
#include "EditorInternal.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Include/Scene.h"
#include "Include/SceneSerializer.h"
#include "Include/FileSystem.h"
#include "Include/Rendering.h"
#include "Math/Quaternion.h"
#include "Math/Bitpack.h"

extern SDL_GPUDevice* g_GPUDevice;
extern WindowState g_WindowState;

//------------------------------------------------------------------------
// Scene Window

// hierarchy selection, bundle row when node is -1, nothing when bundle is INVALID_BUNDLE
static u32 sceneSelectedBundle = INVALID_BUNDLE;
static s32 sceneSelectedNode   = -1;
static u32 sceneTreeRowBudget;
static bool sceneInfoOpen = true;
static bool sceneLightsOpen = true;
static s32 sceneSelectedLight = -1;

// editor authored scene, created on demand by New / Open Scene
static Scene g_EditorScene;
static bool  g_EditorSceneInit;

static bool sceneSavePopupOpen;
static char sceneSaveName[128];

#define EDITOR_SCENE_FOLDER "Assets/Scenes"

Scene* EditorNewScene(void)
{
    if (g_EditorSceneInit)
        Scene_Destroy(&g_EditorScene);
    Scene_Init(&g_EditorScene);
    g_EditorSceneInit = true;
    Scene_MakeActive(&g_EditorScene);
    RendererSetLights(NULL, 0u);
    sceneSelectedBundle = INVALID_BUNDLE;
    sceneSelectedNode = -1;
    sceneSelectedLight = -1;
    return &g_EditorScene;
}

// asset browser paths use backslashes, the bundle cache and .scene files key on the
// path string, keep everything forward slashed like the code side
static void EditorNormalizePath(const char* path, char* out, u32 outSize)
{
    u32 i = 0;
    for (; path[i] && i + 1u < outSize; i++)
        out[i] = path[i] == '\\' ? '/' : path[i];
    out[i] = '\0';
}

// skinned spawns default to the bundle's second animation when there is one,
// animation 0 is usually the bind pose
static u32 EditorDefaultAnimation(const Scene* scene, u32 bundleIdx)
{
    const SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    u32 numAnims = (u32)ref->bundle->numAnimations;
    if (numAnims == 0u) return 0u;
    return ref->animOffset + (numAnims > 1u ? 1u : 0u);
}

// spawns one instance and assigns the default animation to skinned ones
static void EditorSpawnBundleAt(Scene* scene, u32 bundleIdx, v128f position, v128f rotation, v128f scale)
{
    if (!Scene_Spawn(scene, bundleIdx, position, rotation, scale))
        return;

    if (scene->bundleRefs[bundleIdx].skinned && scene->skinnedSet.nextSparseID > 0u)
    {
        // skinned spawns share one freshly allocated sparse id, the animation
        // instance pool is indexed by it
        u32 sparseIdx = scene->skinnedSet.nextSparseID - 1u;
        GPUAnimationInstance instance = { .animIdx = EditorDefaultAnimation(scene, bundleIdx), .timeOffset = 0.0f };
        AnimationSystem_SetInstance(&scene->animSystem, sparseIdx, instance);
    }
}

static void EditorSpawnBundle(Scene* scene, u32 bundleIdx, f32 scale)
{
    EditorSpawnBundleAt(scene, bundleIdx, VecZero(), QIdentity(), VecSet1(scale));
}

void EditorImportMeshToScene(const char* path)
{
    Scene* scene = Scene_GetActive();
    if (!scene) scene = EditorNewScene();

    char normalized[512];
    EditorNormalizePath(path, normalized, sizeof(normalized));
    u32 bundleIdx = Scene_AddBundleAuto(scene, normalized);
    if (bundleIdx == INVALID_BUNDLE)
    {
        AX_ERROR("import to scene failed: %s", normalized);
        return;
    }
    EditorSpawnBundle(scene, bundleIdx, 1.0f);
}

//------------------------------------------------------------------------
// Import with detail popup

static bool importDetailOpen;
static char importDetailPath[512];
static const SceneBundle* importDetailBundle; // peeked cache reference, released on close
static f32  importDetailScale = 1.0f;

void EditorOpenImportDetail(const char* path)
{
    if (importDetailBundle)
    {
        Scene_ReleaseBundlePeek(importDetailPath);
        importDetailBundle = NULL;
    }

    char normalized[512];
    EditorNormalizePath(path, normalized, sizeof(normalized));
    const SceneBundle* bundle = Scene_AcquireBundlePeek(normalized);
    if (!bundle)
    {
        AX_ERROR("mesh load failed: %s", normalized);
        return;
    }
    MemCopy(importDetailPath, normalized, StringLength(normalized) + 1);
    importDetailBundle = bundle;
    importDetailScale = 1.0f;
    importDetailOpen = true;
}

static void SceneImportDetailPopup(void)
{
    if (!importDetailOpen)
    {
        if (importDetailBundle)
        {
            Scene_ReleaseBundlePeek(importDetailPath);
            importDetailBundle = NULL;
        }
        return;
    }
    if (!importDetailBundle)
    {
        importDetailOpen = false;
        return;
    }

    float2 center = { g_WindowState.prev_width * 0.5f - 210.0f, g_WindowState.prev_height * 0.5f - 220.0f };
    if (!UIBeginWindow("Import Mesh", center, (float2){ 420.0f, 440.0f }, &importDetailOpen, UIWindowFlags_NoResize)) return;

    const SceneBundle* bundle = importDetailBundle;
    bool skinned = bundle->numSkins > 0;

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
        UITextU32("Meshes", (u32)bundle->numMeshes);
        UITextU32("Nodes", (u32)bundle->numNodes);
        UITextU32("Materials", (u32)bundle->numMaterials);
        UITextU32("Images", (u32)bundle->numImages);
        UITextU32("Vertices", (u32)bundle->totalVertices);
        UITextU32("Triangles", (u32)bundle->totalIndices / 3u);
        UITextU32("Skins", (u32)bundle->numSkins);
        UITextU32("Animations", (u32)bundle->numAnimations);
    }

    if (skinned && bundle->numAnimations > 0)
    {
        u32 defaultAnim = bundle->numAnimations > 1 ? 1u : 0u;
        const char* animName = bundle->animations[defaultAnim].name;
        char* text = UIFrameStringAlloc(96u);
        if (text)
        {
            u32 len = (u32)StringLength("Default animation: ");
            MemCopy(text, "Default animation: ", len);
            if (animName && animName[0])
            {
                u32 nameLen = Minu32((u32)StringLength(animName), 64u);
                MemCopy(text + len, animName, nameLen);
                len += nameLen;
            }
            else
                len += (u32)IntToString(text + len, (int64_t)defaultAnim, 0);
            text[len] = '\0';
            CLAY_TEXT(((Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text }),
                      CLAY_TEXT_CONFIG({ .fontSize = 13, .textColor = UIGetClayColor(UIColor_SubText) }));
        }
    }

    UIEditFloat(CLAY_ID("ImportDetailScale"), CLAY_STRING("Scale"), &importDetailScale, 0.001f, 10.0f, 0.001, 3);

    CLAY(CLAY_ID("ImportDetailButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton(CLAY_ID("ImportDetailOk"), CLAY_STRING("Import"), (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            Scene* scene = Scene_GetActive();
            if (!scene) scene = EditorNewScene();
            u32 bundleIdx = Scene_AddBundleAuto(scene, importDetailPath);
            if (bundleIdx != INVALID_BUNDLE)
                EditorSpawnBundle(scene, bundleIdx, importDetailScale);
            else
                AX_ERROR("import to scene failed: %s", importDetailPath);
            importDetailOpen = false;
        }
        if (UIButton(CLAY_ID("ImportDetailCancel"), CLAY_STRING("Cancel"), (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            importDetailOpen = false;
        }
    }
    UIEndWindow();
}

void EditorOpenScene(const char* path)
{
    char normalized[512];
    EditorNormalizePath(path, normalized, sizeof(normalized));
    Scene* scene = EditorNewScene();
    if (!SceneSerializer_Load(scene, normalized))
        AX_ERROR("scene load failed: %s", normalized);
}

static void EditorSaveSceneAs(const char* name)
{
    Scene* scene = Scene_GetActive();
    if (!scene || name[0] == '\0') return;

    CreateFolder(EDITOR_SCENE_FOLDER);
    char path[512];
    MemsetZero(path, sizeof(path));
    if (!CombinePaths(path, sizeof(path), EDITOR_SCENE_FOLDER, name)) return;
    int pathLen = StringLength(path);
    if (!FileHasExtension(path, pathLen, ".scene"))
    {
        if (pathLen + 7 > (int)sizeof(path)) return;
        MemCopy(path + pathLen, ".scene", 7);
    }
    SceneSerializer_Save(scene, path);
}

// caller owned tree expansion state, node bits start closed,
// bundle flags are stored inverted so bundle rows start open
#define SCENE_TREE_MAX_NODES 16000
static u32 sceneNodeOpenBits[MAX_SCENE_BUNDLES][SCENE_TREE_MAX_NODES / 32];
static u8  sceneBundleClosed[MAX_SCENE_BUNDLES];

static bool SceneNodeIsOpen(u32 bundleIdx, u32 nodeIdx)
{
    if (nodeIdx >= SCENE_TREE_MAX_NODES) return false;
    return (sceneNodeOpenBits[bundleIdx][nodeIdx >> 5u] >> (nodeIdx & 31u)) & 1u;
}

static void SceneNodeFlipOpen(u32 bundleIdx, u32 nodeIdx)
{
    if (nodeIdx >= SCENE_TREE_MAX_NODES) return;
    sceneNodeOpenBits[bundleIdx][nodeIdx >> 5u] ^= 1u << (nodeIdx & 31u);
}

static Clay_String SceneNodeLabel(const ANode* node, s32 nodeIdx)
{
    if (node->name && node->name[0]) return UIStr(node->name);

    char* text = UIFrameStringAlloc(20u);
    if (!text) return CLAY_STRING("Node");
    u32 len = 5u;
    MemCopy(text, "Node ", len);
    len += (u32)IntToString(text + len, (int64_t)nodeIdx, 0);
    text[len] = '\0';
    return (Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text };
}

// right click selects the hovered row so the context menu targets it, last frame's layout
static bool SceneRowRightClicked(Clay_ElementId id)
{
    if (!GetMousePressed(MouseButton_Right)) return false;
    Clay_ElementData data = Clay_GetElementData(id);
    if (!data.found) return false;
    Clay_PointerData pointer = Clay_GetPointerState();
    return pointer.position.x >= data.boundingBox.x && pointer.position.x <= data.boundingBox.x + data.boundingBox.width &&
           pointer.position.y >= data.boundingBox.y && pointer.position.y <= data.boundingBox.y + data.boundingBox.height;
}

static void SceneNodeTree(const SceneBundle* bundle, u32 bundleIdx, s32 nodeIdx, u32 depth)
{
    if (sceneTreeRowBudget == 0u) return;
    sceneTreeRowBudget--;

    const ANode* node = &bundle->nodes[nodeIdx];
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeNode"), bundleIdx * 0x10000u + (u32)nodeIdx);

    u32 flags = 0u;
    if (node->numChildren <= 0) flags |= UITreeNodeFlags_Leaf;
    if (bundleIdx == sceneSelectedBundle && nodeIdx == sceneSelectedNode) flags |= UITreeNodeFlags_Selected;

    bool open = SceneNodeIsOpen(bundleIdx, (u32)nodeIdx);
    bool selected = false;
    if (UITreeNode(id, SceneNodeLabel(node, nodeIdx), depth, flags, open, &selected))
    {
        SceneNodeFlipOpen(bundleIdx, (u32)nodeIdx);
        open = !open;
    }
    if (selected || SceneRowRightClicked(id))
    {
        sceneSelectedBundle = bundleIdx;
        sceneSelectedNode = nodeIdx;
    }
    if (!open) return;

    for (s32 c = 0; c < node->numChildren; c++)
    {
        s32 child = node->children[c];
        if (child >= 0 && child < bundle->numNodes)
            SceneNodeTree(bundle, bundleIdx, child, depth + 1u);
    }
}

static void SceneBundleTree(const Scene* scene, u32 bundleIdx)
{
    const SceneBundle* bundle = scene->bundleRefs[bundleIdx].bundle;
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeBundle"), bundleIdx);

    u32 flags = 0u;
    if (!bundle || bundle->numNodes <= 0) flags |= UITreeNodeFlags_Leaf;
    if (bundleIdx == sceneSelectedBundle && sceneSelectedNode < 0) flags |= UITreeNodeFlags_Selected;

    bool open = !sceneBundleClosed[bundleIdx];
    bool selected = false;
    if (UITreeNode(id, UIStr(GetFileName(scene->bundleRefs[bundleIdx].path)), 0u, flags, open, &selected))
    {
        sceneBundleClosed[bundleIdx] ^= 1u;
        open = !open;
    }
    if (selected || SceneRowRightClicked(id))
    {
        sceneSelectedBundle = bundleIdx;
        sceneSelectedNode = -1;
    }
    if (!open || !bundle) return;

    // normalized bundles have parent indices built, roots are the unparented nodes
    for (s32 i = 0; i < bundle->numNodes; i++)
        if (bundle->nodes[i].parent < 0)
            SceneNodeTree(bundle, bundleIdx, i, 1u);
}

// instanced triangle count at lod 0
static u32 SceneCountTriangles(const RenderSet* set)
{
    u64 triangles = 0u;
    for (u32 i = 0u; i < set->numGroups; i++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[i];
        if (!group->valid) continue;
        triangles += (u64)(group->numIndices / 3u) * group->numEntities;
    }
    return (u32)Minu64(triangles, 0xFFFFFFFFull);
}

//------------------------------------------------------------------------
// Scene tree context menu

static v128f SceneUnpackScaleXY11Z10(u32 packed)
{
    f32 x = (f32)(packed & 0x7FFu) / 2047.0f;
    f32 y = (f32)((packed >> 11u) & 0x7FFu) / 2047.0f;
    f32 z = (f32)((packed >> 22u) & 0x3FFu) / 1023.0f;
    return VecSetR(x, y, z, 0.0f);
}

// duplicates the selected bundle's instance using its first entity's transform
// (root node world, the spawn transform for typical assets), offset to the side
static void SceneEventDuplicateBundle(void* unused)
{
    (void)unused;
    Scene* scene = Scene_GetActive();
    if (!scene || sceneSelectedBundle >= scene->numBundles) return;

    const SceneBundleRef* ref = &scene->bundleRefs[sceneSelectedBundle];
    const RenderSet* set = ref->skinned ? &scene->skinnedSet : &scene->surfaceSet;

    v128f position = VecZero();
    v128f rotation = QIdentity();
    v128f scale    = VecSet1(1.0f);
    if (ref->renderIdx < set->numBundles)
    {
        Range range = set->bundleRange[ref->renderIdx];
        for (u32 g = range.start; g < range.start + range.count; g++)
        {
            const PrimitiveGroup* group = &set->primitiveGroups[g];
            if (!group->valid || group->numEntities == 0u) continue;
            const Entity* entity = &set->entities[group->entityOffset];
            position = entity->position;
            rotation = UnpackQuaternionS16Norm1(entity->rotation);
            scale    = SceneUnpackScaleXY11Z10(entity->scale);
            break;
        }
    }
    position = VecAdd(position, VecSetR(1.0f, 0.0f, 0.0f, 0.0f));
    EditorSpawnBundleAt(scene, sceneSelectedBundle, position, rotation, scale);
}

static void SceneEventDeleteBundle(void* unused)
{
    (void)unused;
    Scene* scene = Scene_GetActive();
    if (!scene || sceneSelectedBundle >= scene->numBundles) return;
    Scene_RemoveBundle(scene, sceneSelectedBundle);
    sceneSelectedBundle = INVALID_BUNDLE;
    sceneSelectedNode = -1;
}

// removes the selected node's mesh entities from the render set, every spawned
// instance of the bundle loses that node. the tree row stays, node hierarchies are
// shared bundle data
static void SceneEventDeleteNode(void* unused)
{
    (void)unused;
    Scene* scene = Scene_GetActive();
    if (!scene || sceneSelectedBundle >= scene->numBundles || sceneSelectedNode < 0) return;

    const SceneBundleRef* ref = &scene->bundleRefs[sceneSelectedBundle];
    const SceneBundle* bundle = ref->bundle;
    if (sceneSelectedNode >= bundle->numNodes) return;
    const ANode* node = &bundle->nodes[sceneSelectedNode];
    if (node->type != 0 || node->index < 0)
    {
        AX_LOG("node has no mesh to remove: %d", sceneSelectedNode);
        return;
    }

    RenderSet* set = ref->skinned ? &scene->skinnedSet : &scene->surfaceSet;
    if (ref->renderIdx >= set->numBundles) return;

    u32 removed = 0;
    Range range = set->bundleRange[ref->renderIdx];
    for (u32 g = range.start; g < range.start + range.count; g++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[g];
        if (!group->valid || group->meshIndex != (u32)node->index || group->numEntities == 0u) continue;
        removed += RenderSet_RemoveEntities(set, g, 0, group->numEntities);
    }
    scene->renderDataDirty = 1;
    AX_LOG("removed %d entities of node %d", removed, sceneSelectedNode);
}

//------------------------------------------------------------------------
// Lights

static void SceneAddLight(Scene* scene, u32 type)
{
    if (scene->numLights >= MAX_SCENE_LIGHTS)
    {
        AX_WARN("maximum scene lights reached: %d", MAX_SCENE_LIGHTS);
        return;
    }
    LightGPU* light = &scene->lights[scene->numLights];
    MemsetZero(light, sizeof(*light));
    light->positionRadius[1] = 4.0f;
    light->positionRadius[3] = 12.0f;
    light->directionCone[1] = -1.0f;
    light->directionCone[3] = type == LightType_Spot ? 0.72f : 0.0f;
    light->colorIntensity[0] = 1.0f;
    light->colorIntensity[1] = 1.0f;
    light->colorIntensity[2] = 1.0f;
    light->colorIntensity[3] = 25.0f;
    light->type = type;
    light->flags = LIGHT_FLAG_SHADOWED;
    light->shadowIndex = LIGHT_SHADOW_INDEX_INVALID;
    sceneSelectedLight = (s32)scene->numLights++;
}

static void SceneRemoveLight(Scene* scene, u32 lightIdx)
{
    if (lightIdx >= scene->numLights) return;
    for (u32 i = lightIdx + 1; i < scene->numLights; i++)
        scene->lights[i - 1] = scene->lights[i];
    scene->numLights--;
    if (sceneSelectedLight >= (s32)scene->numLights) sceneSelectedLight = (s32)scene->numLights - 1;
    if (scene->numLights == 0u)
        RendererSetLights(NULL, 0u); // clear the renderer's copy, submit skips empty scenes
}

static Clay_String SceneLightLabel(const LightGPU* light, u32 lightIdx)
{
    char* text = UIFrameStringAlloc(24u);
    if (!text) return CLAY_STRING("Light");
    const char* kind = light->type == LightType_Spot ? "Spot " : "Point ";
    u32 len = (u32)StringLength(kind);
    MemCopy(text, kind, len);
    len += (u32)IntToString(text + len, (int64_t)lightIdx, 0);
    text[len] = '\0';
    return (Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text };
}

static void SceneLightsUI(Scene* scene)
{
    sceneLightsOpen ^= UICollapsingHeader(CLAY_ID("SceneLightsHeader"), CLAY_STRING("Lights"), sceneLightsOpen);
    if (!sceneLightsOpen) return;

    CLAY(CLAY_ID("SceneLightButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        if (UIButton(CLAY_ID("SceneAddPoint"), CLAY_STRING("Add Point"), (Clay_Dimensions){ 100.0f, 26.0f }, false))
            SceneAddLight(scene, LightType_Point);
        if (UIButton(CLAY_ID("SceneAddSpot"), CLAY_STRING("Add Spot"), (Clay_Dimensions){ 100.0f, 26.0f }, false))
            SceneAddLight(scene, LightType_Spot);
        if (sceneSelectedLight >= 0 && sceneSelectedLight < (s32)scene->numLights &&
            UIButton(CLAY_ID("SceneDeleteLight"), CLAY_STRING("Delete"), (Clay_Dimensions){ 90.0f, 26.0f }, false))
            SceneRemoveLight(scene, (u32)sceneSelectedLight);
        UIPopFloat(UIFloat_TextScale);
    }

    for (u32 i = 0u; i < scene->numLights; i++)
    {
        Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneLightRow"), i);
        u32 flags = UITreeNodeFlags_Leaf;
        if ((s32)i == sceneSelectedLight) flags |= UITreeNodeFlags_Selected;
        bool selected = false;
        UITreeNode(id, SceneLightLabel(&scene->lights[i], i), 1u, flags, false, &selected);
        if (selected) sceneSelectedLight = (s32)i;
    }

    if (sceneSelectedLight < 0 || sceneSelectedLight >= (s32)scene->numLights) return;
    LightGPU* light = &scene->lights[sceneSelectedLight];

    UIEditFloatN(CLAY_ID("SceneLightPos"), CLAY_STRING("Position"), light->positionRadius, 3u, -10000.0f, 10000.0f, 2);
    UISliderFloatValue(CLAY_ID("SceneLightRadius"), CLAY_STRING("Radius"), &light->positionRadius[3], 0.1f, 200.0f, 1);
    if (light->type == LightType_Spot)
    {
        UIEditFloatN(CLAY_ID("SceneLightDir"), CLAY_STRING("Direction"), light->directionCone, 3u, -1.0f, 1.0f, 2);
        UISliderFloatValue(CLAY_ID("SceneLightCone"), CLAY_STRING("Cone"), &light->directionCone[3], 0.0f, 0.99f, 2);
    }
    UIColorEdit3(CLAY_ID("SceneLightColor"), CLAY_STRING("Color"), light->colorIntensity);
    UISliderFloatValue(CLAY_ID("SceneLightIntensity"), CLAY_STRING("Intensity"), &light->colorIntensity[3], 0.0f, 200.0f, 1);

    bool shadowed = (light->flags & LIGHT_FLAG_SHADOWED) != 0u;
    UICheckbox(CLAY_ID("SceneLightShadowed"), CLAY_STRING("Shadowed"), &shadowed);
    light->flags = shadowed ? LIGHT_FLAG_SHADOWED : 0u;
}

//------------------------------------------------------------------------
// Save popup and window

static void SceneSavePopup(void)
{
    if (!sceneSavePopupOpen) return;
    float2 center = { g_WindowState.prev_width * 0.5f - 190.0f, g_WindowState.prev_height * 0.5f - 90.0f };
    if (!UIBeginWindow("Save Scene", center, (float2){ 380.0f, 190.0f }, &sceneSavePopupOpen, UIWindowFlags_NoResize)) return;

    CLAY_TEXT(CLAY_STRING("Name:"), CLAY_TEXT_CONFIG({
        .fontSize = 14,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    static UITextAreaCustomData nameData;
    nameData.type = UICustomType_TextArea;
    nameData.buffer = sceneSaveName;
    nameData.capacity = sizeof(sceneSaveName);
    nameData.flags = UITextAreaFlags_CenterY;
    CLAY(CLAY_ID("SceneSaveNameBox"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26.0f) } },
        .custom = { .customData = &nameData }
    }) {}

    CLAY(CLAY_ID("SceneSaveButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton(CLAY_ID("SceneSaveOk"), CLAY_STRING("Save"), (Clay_Dimensions){ 96.0f, 30.0f }, false) || GetKeyPressed('\r'))
        {
            EditorSaveSceneAs(sceneSaveName);
            sceneSavePopupOpen = false;
        }
        if (UIButton(CLAY_ID("SceneSaveCancel"), CLAY_STRING("Cancel"), (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            sceneSavePopupOpen = false;
        }
    }
    UIEndWindow();
}

void DrawSceneWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("SceneWindow", 5381u) };
    if (UIBeginWindowId(windowID, "Scene", (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, open, UIWindowFlags_RightClickable))
    {
        Scene* scene = Scene_GetActive();

        if (scene && sceneSelectedBundle != INVALID_BUNDLE && sceneSelectedBundle < scene->numBundles)
        {
            if (sceneSelectedNode < 0)
            {
                UIRightClickAddEvent("Duplicate", SceneEventDuplicateBundle, NULL);
                UIRightClickAddEvent("Delete", SceneEventDeleteBundle, NULL);
            }
            else
                UIRightClickAddEvent("Delete", SceneEventDeleteNode, NULL);
        }

        CLAY(CLAY_ID("SceneWindowToolbar"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
                .childGap = 8,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            UIPushFloatAdd(UIFloat_TextScale, -0.15f);
            if (UIButton(CLAY_ID("SceneNewButton"), CLAY_STRING("New"), (Clay_Dimensions){ 80.0f, 26.0f }, false))
                EditorNewScene();
            if (scene && UIButton(CLAY_ID("SceneSaveButton"), CLAY_STRING("Save"), (Clay_Dimensions){ 80.0f, 26.0f }, false))
            {
                sceneSavePopupOpen = true;
                sceneSaveName[0] = '\0';
            }
            UIPopFloat(UIFloat_TextScale);
        }

        if (!scene)
        {
            CLAY_TEXT(CLAY_STRING("No active scene."), CLAY_TEXT_CONFIG({
                .fontSize = 14,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
        else
        {
            sceneInfoOpen ^= UICollapsingHeader(CLAY_ID("SceneInfoHeader"), CLAY_STRING("Active Scene"), sceneInfoOpen);
            if (sceneInfoOpen)
            {
                CLAY(CLAY_ID("SceneWindowInfo"), {
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                        .padding = { 6, 0, 0, 0 },
                        .childGap = 4,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    }
                }) {
                    UITextU32("Active scenes", g_NumActiveScenes);
                    UITextU32("Bundles", scene->numBundles);
                    UITextU32("Materials", scene->numMaterials);
                    UITextU32("Static entities", scene->surfaceSet.numEntities);
                    UITextU32("Skinned entities", scene->skinnedSet.numEntities);
                    UITextU32("Primitive groups", scene->surfaceSet.numGroups + scene->skinnedSet.numGroups);
                    UITextU32("Triangles", SceneCountTriangles(&scene->surfaceSet) + SceneCountTriangles(&scene->skinnedSet));
                }
            }
            UIDivider(CLAY_ID("SceneWindowDivider"));

            SceneLightsUI(scene);
            UIDivider(CLAY_ID("SceneWindowDivider2"));

            sceneTreeRowBudget = SCENE_TREE_MAX_NODES;
            CLAY(CLAY_ID("SceneWindowTree"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("SceneWindowTree"), 0.0f), 2u)) {
                for (u32 b = 0u; b < scene->numBundles; b++)
                    SceneBundleTree(scene, b);
                if (sceneTreeRowBudget == 0u)
                {
                    CLAY_TEXT(CLAY_STRING("... node limit reached, collapse some rows"), CLAY_TEXT_CONFIG({
                        .fontSize = 13,
                        .textColor = UIGetClayColor(UIColor_SubText)
                    }));
                }
            }
        }
        UIEndWindow();
    }

    SceneSavePopup();
    SceneImportDetailPopup();
}

//------------------------------------------------------------------------
// Textures Window

// texture page inspector, blits the selected 4k page layer into a small preview each frame
#define TEX_PREVIEW_SIZE 1024

static bool texInfoOpen = true;
static u32 texInspectClass = TextureClass_Albedo;
static u32 texInspectLayer;
static SDL_GPUTexture* texPagePreview;
static UIImageData texPagePreviewImage;

static u32 TexturePageMemoryMB(const TextureSystem* ts)
{
    u64 bytes = 0u;
    for (u32 i = 0u; i < TextureClass_Count; i++)
    {
        u64 texelBytes = ts->compressed ? 1u : (i == TextureClass_Albedo ? 4u : 2u);
        bytes += (u64)TEXTURE_PAGE_SIZE * TEXTURE_PAGE_SIZE * ts->classes[i].layerCount * texelBytes;
    }
    return (u32)((bytes * 4u / 3u) >> 20u); // mip chain adds ~1/3
}

static void TexturePageBlitPreview(const TexturePageClass* cls)
{
    if (!texPagePreview)
    {
        texPagePreview = CreateTexture2D(TEX_PREVIEW_SIZE, TEX_PREVIEW_SIZE, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                         SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                         SDL_GPU_SAMPLECOUNT_1, 1u, "TexturePagePreview");
        texPagePreviewImage = (UIImageData){ .texture = texPagePreview };
    }
    if (!texPagePreview) return;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    if (!cmd) return;

    SDL_GPUBlitInfo blit;
    SDL_zero(blit);
    blit.source.texture = cls->pages.handle;
    blit.source.layer_or_depth_plane = texInspectLayer;
    blit.source.w = (u32)cls->pages.width;
    blit.source.h = (u32)cls->pages.height;
    blit.destination.texture = texPagePreview;
    blit.destination.w = TEX_PREVIEW_SIZE;
    blit.destination.h = TEX_PREVIEW_SIZE;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd, &blit);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void DrawTexturesWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("TexturesWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Textures", (float2) { 540.0f, 18.0f }, (float2) { 520.0f, 760.0f }, open, 0u)) return;

    Scene* scene = Scene_GetActive();
    if (!scene)
    {
        CLAY_TEXT(CLAY_STRING("No active scene."), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        UIEndWindow();
        return;
    }

    TextureSystem* ts = &scene->textureSystem;

    texInfoOpen ^= UICollapsingHeader(CLAY_ID("TextureInfoHeader"), CLAY_STRING("Texture System"), texInfoOpen);
    if (texInfoOpen)
    {
        CLAY(CLAY_ID("TexturesWindowInfo"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .padding = { 6, 0, 0, 0 },
                .childGap = 4,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
        }) {
            UITextU32("Descriptors", ts->numDescriptors);
            UITextU32("Material watermark", ts->materialWatermark);
            UITextU32("Compressed pages", ts->compressed);
            UITextU32("Page size", TEXTURE_PAGE_SIZE);
            UITextU32("Albedo layers", ts->classes[TextureClass_Albedo].layerCount);
            UITextU32("Normal layers", ts->classes[TextureClass_Normal].layerCount);
            UITextU32("Metallic-roughness layers", ts->classes[TextureClass_MetallicRoughness].layerCount);
            UITextU32("Page memory (MB)", TexturePageMemoryMB(ts));
        }
    }
    UIDivider(CLAY_ID("TexturesWindowDivider"));

    CLAY(CLAY_ID("TextureClassButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        if (UIButton(CLAY_ID("TexClassAlbedo"), CLAY_STRING("Albedo")    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Albedo))            texInspectClass = TextureClass_Albedo;
        if (UIButton(CLAY_ID("TexClassNormal"), CLAY_STRING("Normal")    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Normal))            texInspectClass = TextureClass_Normal;
        if (UIButton(CLAY_ID("TexClassMR")    , CLAY_STRING("MetalRough"), (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_MetallicRoughness)) texInspectClass = TextureClass_MetallicRoughness;
        UIPopFloat(UIFloat_TextScale);
    }

    const TexturePageClass* cls = &ts->classes[texInspectClass];
    if (!cls->pages.handle || cls->layerCount == 0u)
    {
        CLAY_TEXT(CLAY_STRING("No pages for this class."), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        UIEndWindow();
        return;
    }
    if (texInspectLayer >= cls->layerCount) texInspectLayer = 0u;

    CLAY(CLAY_ID("TextureLayerButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 6,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        for (u32 i = 0u; i < cls->layerCount && i < TEXTURE_PAGE_LAYERS; i++)
        {
            Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("TexLayerButton"), i);
            char* buffer = UIFrameStringAlloc(16);
            IntToString(buffer, (s64)i, 0);
            if (UIButton(id, UIStr(buffer), (Clay_Dimensions){ 30.0f, 26.0f }, i == texInspectLayer)) texInspectLayer = i;
        }
    }

    TexturePageBlitPreview(cls);

    UIWindow* window = UIGetWindow(windowID);
    f32 side = window ? window->scale.x - 48.0f : 256.0f;
    side = Maxf32(Minf32(side, UIWindowRemainingHeight(windowID, CLAY_ID("TexturePagePreview"), 0.0f)), 64.0f);
    CLAY(CLAY_ID("TexturePagePreview"), {
        .layout = { .sizing = { CLAY_SIZING_FIXED(side), CLAY_SIZING_FIXED(side) } },
        .image = { .imageData = &texPagePreviewImage },
        .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(1) }
    }) {}

    UIEndWindow();
}
