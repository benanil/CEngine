#ifndef CP_EDITOR_INTERNAL
#define CP_EDITOR_INTERNAL

#include "Include/UIRenderer.h"
#include "Include/UIWindow.h"

#if defined(__cplusplus)
extern "C" {
#endif

// implemented in SceneEditor.c
void DrawSceneWindow(bool* open);
void DrawTexturesWindow(bool* open);
void EditorImportMeshToScene(const char* path);
void EditorOpenImportDetail(const char* path);
void EditorOpenScene(const char* path);
void EditorSceneStartup(void);
const char* EditorSceneActivePath(void);
struct Scene_* EditorNewScene(void);
struct Camera_;
void EditorSceneHotkeys(void);
void EditorPickingUpdate(struct Camera_* camera);
bool EditorLightGizmoUpdate(struct Camera_* camera);
void DrawSceneLightGizmos(struct Camera_* camera);

// implemented in Editor.c
bool EditorSettingsOpenLastScene(void);
const char* EditorSettingsLastScene(void);
void EditorSettingsSetLastScene(const char* path);

// scene view window (Editor.c). while it is open the 3d scene renders at the window
// content size and picking, gizmos and light icons work in scene view local coordinates
bool   EditorSceneViewActive(void);
float2 EditorSceneViewOrigin(void);            // content rect top left, zero when inactive
float2 EditorSceneMouse(void);                 // mouse in scene coordinates
bool   EditorSceneInteractAllowed(void);       // mouse may pick or drag in the 3d scene
bool   EditorSceneViewPointVisible(float2 point); // screen point shows the scene, not covered by other ui

// implemented in EditorGizmo.c, update returns true while the gizmo owns the mouse.
// AddTarget toggles the object in the multi selection (ctrl click)
void EditorGizmoSetTarget(u32 skinned, u32 groupIdx, u32 entityIdx);
void EditorGizmoAddTarget(u32 skinned, u32 groupIdx, u32 entityIdx);
void EditorGizmoClear(void);
bool EditorGizmoDuplicateSelected(void);
bool EditorGizmoDeleteSelected(void);
bool EditorGizmoUpdate(struct Camera_* camera);

// implemented in AssetsEditor.c
void DrawAssetsWindow(bool* open);

// implemented in ConsoleEditor.c, init installs the sdl log hook so logs are
// recorded from startup even while the window is closed
void EditorConsoleInit(void);
void DrawConsoleWindow(bool* open);

#if defined(__cplusplus)
}
#endif

#endif // CP_EDITOR_INTERNAL
