//------------------------------------------------------------------------
#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/UIWindow.h"

bool sceneSavePopupOpen;
bool sceneSaveConfirmOpen;
bool sceneDeletePopupOpen;

extern WindowState g_WindowState;
char sceneSaveName[128];
char sceneDeletePath[512];
// sceneeditor.c
extern void EditorSaveSceneAs(const char* name);
extern void EditorSaveSceneAs(const char* name);
extern void SceneDeleteWithDependencies(const char* path);
extern void EditorSaveActiveScene(void);

// Save popup and window
void SceneSavePopup(void)
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
        if (UIButton("Save", (Clay_Dimensions){ 96.0f, 30.0f }, false) || GetKeyPressed('\r'))
        {
            EditorSaveSceneAs(sceneSaveName);
            sceneSavePopupOpen = false;
        }
        if (UIButton("Cancel", (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            sceneSavePopupOpen = false;
        }
    }
    UIEndWindow();
}

void SceneSaveConfirmPopup(void)
{
    if (!sceneSaveConfirmOpen) return;
    if (Scene_GetActivePath()[0] == '\0')
    {
        sceneSaveConfirmOpen = false;
        return;
    }

    float2 center = { g_WindowState.prev_width * 0.5f - 230.0f, g_WindowState.prev_height * 0.5f - 85.0f };
    if (!UIBeginWindow("Save Scene?", center, (float2){ 460.0f, 180.0f }, &sceneSaveConfirmOpen, UIWindowFlags_NoResize)) return;

    CLAY_TEXT(CLAY_STRING("Overwrite the active scene?"), CLAY_TEXT_CONFIG({
        .fontSize = 15,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    CLAY_TEXT(UIStr(Scene_GetActivePath()), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));

    CLAY(CLAY_ID("SceneSaveConfirmButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton("Save", (Clay_Dimensions){ 96.0f, 30.0f }, false) || GetKeyPressed('\r'))
        {
            EditorSaveActiveScene();
            sceneSaveConfirmOpen = false;
        }
        if (UIButton("Save As New", (Clay_Dimensions){ 128.0f, 30.0f }, false))
        {
            sceneSaveConfirmOpen = false;
            sceneSavePopupOpen = true;
            sceneSaveName[0] = '\0';
        }
        if (UIButton("Cancel", (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            sceneSaveConfirmOpen = false;
        }
    }
    UIEndWindow();
}

void SceneDeletePopup(void)
{
    if (!sceneDeletePopupOpen) return;
    if (sceneDeletePath[0] == '\0')
    {
        sceneDeletePopupOpen = false;
        return;
    }

    float2 center = { g_WindowState.prev_width * 0.5f - 240.0f, g_WindowState.prev_height * 0.5f - 95.0f };
    if (!UIBeginWindow("Delete Scene?", center, (float2){ 480.0f, 200.0f }, &sceneDeletePopupOpen, UIWindowFlags_NoResize)) return;

    CLAY_TEXT(CLAY_STRING("Delete this scene and baked texture sidecars?"), CLAY_TEXT_CONFIG({
        .fontSize = 15,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    CLAY_TEXT(UIStr(sceneDeletePath), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));
    CLAY_TEXT(CLAY_STRING("Bundle .abm/.basis sources are kept."), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));

    CLAY(CLAY_ID("SceneDeleteButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton("Delete", (Clay_Dimensions){ 96.0f, 30.0f }, false) || GetKeyPressed('\r'))
        {
            SceneDeleteWithDependencies(sceneDeletePath);
            sceneDeletePath[0] = '\0';
            sceneDeletePopupOpen = false;
        }
        if (UIButton("Cancel", (Clay_Dimensions){ 96.0f, 30.0f }, false))
        {
            sceneDeletePopupOpen = false;
        }
    }
    UIEndWindow();
}