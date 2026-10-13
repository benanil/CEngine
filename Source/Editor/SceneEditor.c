// scene hierarchy and texture system inspector windows
#include "EditorInternal.h"
#include "Include/UIWindow.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Include/Scene.h"
#include "Include/FileSystem.h"
#include "Include/AssetManager.h"
#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Camera.h"
#include "Include/BVH.h"
#include "Include/Terrain.h"

#define EDITOR_SCENE_FOLDER "Assets/Scenes"
#define EDITOR_SCENE_MAX_FILES 256
#define EDITOR_SCENE_DOUBLE_CLICK_SECONDS 0.4f

typedef struct EditorSceneFile_
{
    char path[512];
    u16 nameOffset;
} EditorSceneFile;

extern SDL_GPUDevice* g_GPUDevice;
extern WindowState g_WindowState;
extern Graphics gGFX;

extern s32  sceneSelectedLight;
// PopupWindows.c
extern bool sceneSavePopupOpen;
extern bool sceneSaveConfirmOpen;
extern bool sceneDeletePopupOpen;
extern char sceneSaveName[128];
extern char sceneDeletePath[512];

// hierarchy selection, bundle row when node is -1, nothing when bundle is INVALID_BUNDLE
static bool sceneInfoOpen       = true;

SceneObjectSelection sceneObjectSelection;
u32 sceneSelectedEntity = INVALID_ENTITY;
s32 sceneSelectedBundle = -1;

static EditorSceneFile sceneFiles[EDITOR_SCENE_MAX_FILES];
static f32  sceneFilesLastScan;
static f32  sceneLastClickTime = -10.0f;
static s32  sceneSelectedFile  = -1;
static bool sceneFilesDirty = true;
static u32  sceneNumFiles;
static u32  sceneLastClickHash;

static const char* const kEditorSceneAtlasSuffix[TextureClass_Count] = { "_albedo.ctex", "_normal.ctex", "_mr.ctex" };

extern void TerrainEditorSceneChanged();

static void EditorSceneResetState(void)
{
    EditorGizmoClear();
    sceneSelectedBundle  = INVALID_BUNDLE;
    sceneSelectedLight   = -1;
    sceneSelectedEntity  = INVALID_ENTITY;
    sceneObjectSelection.valid = false;
}

Scene* EditorNewScene(void)
{
    Scene* scene = Scene_NewActive();
    EditorSceneResetState();
    TerrainEditorSceneChanged();
    return scene;
}

static void OpenSceneFinish(SceneAsyncRequest* request)
{
    Scene* scene = Scene_OpenActive(request->path, request->sceneFileData);
    if (!scene) return;
    EditorSceneResetState();
    EditorSettingsSetLastScene(Scene_GetActivePath());
    TerrainEditorSceneChanged();
}

static void SceneSelectObject(EntityID entityHandle)
{
    Scene* scene = GetActiveScene();
    Entity* entity = Scene_GetEntity(scene, entityHandle);
    bool skinned = EntityIDIsSkinned(entityHandle);
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    sceneObjectSelection = (SceneObjectSelection){
        .valid        = true,
        .skinned      = skinned,
        .entityIdx    = entityHandle,
        .animIdx      = 0u,
        .groupIdx     = EntityGetPrimitiveID(entity),
        .bundleIdx    = EntityGetBundleId(set, entity),
        .animTime     = 0.0f
    };
}

void EditorOpenScene(const char* path)
{
    SceneAsyncBegin(SceneAsyncOp_OpenScene, path, "EditorOpenScene", OpenSceneFinish);
}

// click picking: ray casts the scene through the bundle blas data, logs the hit and
// outlines it. runs once per left click outside the ui
void EditorPickingUpdate(Camera* camera)
{
    if (!GetMousePressed(MouseButton_Left) || !EditorSceneInteractAllowed()) return;

    Scene* scene = GetActiveScene();
    if (!scene) return;

    RayV ray = ScreenPointToRay(camera, EditorSceneMouse());
    BVHHit hit;
    if (!BVH_RaycastScene(scene, ray.origin, ray.dir, &hit))
    {
        // clicking empty space clears the selection unless adding with ctrl
        if (!GetKeyDown(SDLK_LCTRL)) EditorGizmoClear();
        return;
    }
    
    scene = hit.scene;
    sceneSelectedEntity = hit.bundleIdx;
    SceneSelectObject(hit.entityID);
    // the gizmo submits the outlines of the whole selection every frame
    if (GetKeyDown(SDLK_LCTRL)) // ctrl click toggles the object in the multi selection
        EditorGizmoAddTarget(hit.entityID);
    else
        EditorGizmoSetTarget(hit.entityID);
}

void EditorSceneHotkeys(void)
{
    Scene* scene = GetActiveScene();
    if (!scene) return;

    bool ctrl = GetKeyDown(SDLK_LCTRL) || GetKeyDown(SDLK_RCTRL);
    if (ctrl && GetKeyPressed(SDLK_D))
    {
        if (EditorGizmoDuplicateSelected())
            sceneObjectSelection.valid = false;
    }

    if (GetKeyPressed(SDLK_DELETE) && (!UIAnyWindowHovered() || EditorSceneInteractAllowed()))
    {
        if (EditorGizmoDeleteSelected())
        {
            sceneObjectSelection.valid = false;
            sceneSelectedBundle = INVALID_BUNDLE;
        }
    }
}

void EditorSaveSceneAs(const char* name)
{
    Scene* scene = GetActiveScene();
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
    if (Scene_SaveActiveAs(path))
    {
        EditorSettingsSetLastScene(Scene_GetActivePath());
        sceneFilesDirty = true;
    }
}

void EditorSaveActiveScene(void)
{
    Scene* scene = GetActiveScene();
    if (!scene || Scene_GetActivePath()[0] == '\0') return;
    if (Scene_SaveActive())
    {
        EditorSettingsSetLastScene(Scene_GetActivePath());
        sceneFilesDirty = true;
    }
}

static bool ScenePathsEqual(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void SceneFilesCollectFn(const char* path, void* data)
{
    (void)data;
    if (sceneNumFiles >= EDITOR_SCENE_MAX_FILES || IsFolder(path)) return;
    if (!FileHasExtension(path, StringLength(path), ".scene")) return;

    EditorSceneFile* file = &sceneFiles[sceneNumFiles++];
    NormalizePath(path, file->path, sizeof(file->path));
    file->nameOffset = 0u;
    for (u32 i = 0u; file->path[i]; i++)
        if (file->path[i] == '/' || file->path[i] == '\\') file->nameOffset = (u16)(i + 1u);
}

static void SceneFilesRefresh(void)
{
    sceneNumFiles = 0u;
    CreateFolder(EDITOR_SCENE_FOLDER);
    VisitFolder(EDITOR_SCENE_FOLDER, SceneFilesCollectFn, NULL, false);
    sceneFilesDirty = false;
    sceneFilesLastScan = TimeSinceStartup();
}

void SceneDeleteWithDependencies(const char* path)
{
    for (u32 i = 0u; i < TextureClass_Count; i++)
    {
        char atlasPath[512];
        ChangeExtensionAndCopy(path, kEditorSceneAtlasSuffix[i], atlasPath, sizeof(atlasPath));
        if (atlasPath[0] && FileExist(atlasPath)) RemoveFile(atlasPath);
    }
    RemoveFile(path);
    if (ScenePathsEqual(path, Scene_GetActivePath())) EditorNewScene();
    sceneSelectedFile = -1;
    sceneFilesDirty = true;
}

static void SceneEventDeleteFile(void* unused)
{
    (void)unused;
    if (sceneSelectedFile < 0 || sceneSelectedFile >= (s32)sceneNumFiles) return;
    MemCopy(sceneDeletePath, sceneFiles[sceneSelectedFile].path, StringLength(sceneFiles[sceneSelectedFile].path) + 1);
    sceneDeletePopupOpen = true;
}

void EditorSceneStartup(void)
{
    if (!EditorSettingsOpenLastScene()) return;

    const char* path = EditorSettingsLastScene();
    if (path && path[0] && FileExist(path)) EditorOpenScene(path);
}

static void SceneFilesUI(void)
{
    if (sceneFilesDirty || TimeSinceStartup() - sceneFilesLastScan > 2.0f) SceneFilesRefresh();

    CLAY_TEXT(CLAY_STRING("Scenes"), CLAY_TEXT_CONFIG({
        .fontSize = 15,
        .textColor = UIGetClayColor(UIColor_Text)
    }));

    if (sceneNumFiles == 0u)
    {
        CLAY_TEXT(CLAY_STRING("No .scene files in Assets/Scenes."), CLAY_TEXT_CONFIG({
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        return;
    }

    for (u32 i = 0u; i < sceneNumFiles; i++)
    {
        EditorSceneFile* file = &sceneFiles[i];
        Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneFileRow"), i);
        u32 flags = UITreeNodeFlags_Leaf;
        if ((s32)i == sceneSelectedFile || ScenePathsEqual(file->path, Scene_GetActivePath())) flags |= UITreeNodeFlags_Selected;

        bool selected = false;
        UITreeNode(id, UIStr(file->path + file->nameOffset), 0u, flags, false, &selected);
        if (selected)
        {
            u32 clickHash = StringToHash(file->path, 5381u);
            f32 now = TimeSinceStartup();
            bool doubleClicked = clickHash == sceneLastClickHash && now - sceneLastClickTime < EDITOR_SCENE_DOUBLE_CLICK_SECONDS;
            sceneLastClickHash = clickHash;
            sceneLastClickTime = doubleClicked ? -10.0f : now;
            sceneSelectedFile = (s32)i;
            if (doubleClicked) EditorOpenScene(file->path);
        }
        if (SceneRowRightClicked(id)) sceneSelectedFile = (s32)i;
    }
}

void SceneAddPrimitive(void* data)
{
    Scene* scene = GetActiveScene();
    MeshType meshType = (MeshType)((u64)data);
    u32 bundle = Scene_AddBundleCached(scene, GetUnitPrimitive(meshType), GetPrimitiveName(meshType));
    Scene_Spawn(scene, bundle, VecZero(), QIdentity(), VecOne(), EntityFlags_ColliderEnabled);
}

void DrawSceneWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("SceneWindow", 5381u) };
    if (UIBeginWindowId(windowID, "Scene", (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, open, UIWindowFlags_RightClickable))
    {
        Scene* scene = GetActiveScene();
        if (!scene)
        {
            CLAY_TEXT(CLAY_STRING("No active scene."), CLAY_TEXT_CONFIG({
                .fontSize = 14,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
            UIEndWindow();
            goto scene_window_end;
        }

        if (GetKeyPressed(SDLK_DELETE) && (!UIAnyWindowHovered() || EditorSceneInteractAllowed()))
        {
            if (EditorGizmoDeleteSelected())
            {
                sceneObjectSelection.valid = false;
                sceneSelectedBundle = INVALID_BUNDLE;
            }
            else if (sceneObjectSelection.valid) SceneDeleteSelectedObject();
            else if (sceneSelectedBundle != INVALID_BUNDLE && sceneSelectedBundle < scene->numBundles)
            {
                 SceneEventDeleteBundle(NULL);
            }
        }

        UIRightClickAddEvent("Create Capsule" , SceneAddPrimitive, (void*)(u64)MeshType_Capsule);
        UIRightClickAddEvent("Create Cube"    , SceneAddPrimitive, (void*)(u64)MeshType_Cube);
        UIRightClickAddEvent("Create Grid"    , SceneAddPrimitive, (void*)(u64)MeshType_Grid);
        UIRightClickAddEvent("Create Cylinder", SceneAddPrimitive, (void*)(u64)MeshType_Cylinder);
        UIRightClickAddEvent("Create Cone"    , SceneAddPrimitive, (void*)(u64)MeshType_Cone);
        UIRightClickAddEvent("Create Sphere"  , SceneAddPrimitive, (void*)(u64)MeshType_Sphere);

        if (sceneSelectedBundle != INVALID_BUNDLE && sceneSelectedBundle < scene->numBundles)
        {
            UIRightClickAddEvent("Delete", SceneEventDeleteBundle, NULL);
            UIRightClickAddEvent("Duplicate", SceneEventDuplicateBundle, NULL);
        }
        if (sceneSelectedFile >= 0 && sceneSelectedFile < (s32)sceneNumFiles)
            UIRightClickAddEvent("Delete Scene File", SceneEventDeleteFile, NULL);

        CLAY(CLAY_ID("SceneWindowToolbar"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
                .childGap = 8,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            UIPushFloatAdd(UIFloat_TextScale, -0.15f);
            if (UIButton("New", (Clay_Dimensions){ 80.0f, 26.0f }, false))
                EditorNewScene();
            if (UIButton("Save", (Clay_Dimensions){ 80.0f, 26.0f }, false))
            {
                if (Scene_GetActivePath()[0])
                    sceneSaveConfirmOpen = true;
                else
                {
                    sceneSavePopupOpen = true;
                    sceneSaveName[0] = '\0';
                }
            }
            UIPopFloat(UIFloat_TextScale);
        }

        extern SceneAsyncRequest* sceneAsyncRequest;
        if (sceneAsyncRequest)
        {
            const char* asyncText = "Spawning bundle...";
            if (sceneAsyncRequest->op == SceneAsyncOp_OpenScene) asyncText = "Opening scene...";
            else if (sceneAsyncRequest->op == SceneAsyncOp_ImportMesh) asyncText = "Importing mesh...";
            CLAY_TEXT(UIStr(asyncText), CLAY_TEXT_CONFIG({
                .fontSize = 13,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }

        CLAY(CLAY_ID("SceneFilesPanel"), UIScrollPanelDeclaration(140.0f, 2u)) {
            SceneFilesUI();
        }
        UIDivider(CLAY_ID("SceneFilesDivider"));

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
                UIText(GetActiveScenePath());
                UITextU32("Bundles", scene->numBundles);
                UITextU32("Materials", scene->numMaterials);
                UITextU32("Static entities", scene->surfaceSet.numEntities);
                UITextU32("Skinned entities", scene->skinnedSet.numEntities);
                UITextU32("Primitive groups", scene->surfaceSet.numGroups + scene->skinnedSet.numGroups);
                UITextU32("Triangles", RenderSet_CountTriangles(&scene->surfaceSet) + RenderSet_CountTriangles(&scene->skinnedSet));
                
                if (tGetEnabled())
                {
                    Scene* foliageScene = GetFoliageScene();
                    UIText("Foliage Scene");
                    UITextU32("Bundles", foliageScene->numBundles);
                    UITextU32("Materials", foliageScene->numMaterials);
                    UITextU32("Static entities", foliageScene->surfaceSet.numEntities);
                    UITextU32("Skinned entities", foliageScene->skinnedSet.numEntities);
                    UITextU32("Primitive groups", foliageScene->surfaceSet.numGroups);
                    UITextU32("Triangles", RenderSet_CountTriangles(&foliageScene->surfaceSet));
                }
            }
        }
        UIDivider(CLAY_ID("SceneWindowDivider"));
        
        SceneLightsUI(scene);
        UIDivider(CLAY_ID("SceneWindowDivider2"));
        
        SceneInspectorUI(scene);
        UIDivider(CLAY_ID("SceneWindowDivider3"));
        
        SceneTreeRender(scene, windowID);
        
        UIEndWindow();
    }
    scene_window_end:{}
    SceneSavePopup();
    SceneSaveConfirmPopup();
    SceneDeletePopup();
    SceneImportDetailPopup();
}