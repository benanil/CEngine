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
struct Scene_* EditorNewScene(void);
struct Camera_;
void EditorPickingUpdate(struct Camera_* camera);

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
