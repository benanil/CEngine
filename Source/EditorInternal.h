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

// implemented in AssetsEditor.c
void DrawAssetsWindow(bool* open);

#if defined(__cplusplus)
}
#endif

#endif // CP_EDITOR_INTERNAL
