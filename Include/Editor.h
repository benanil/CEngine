
#ifndef C_EDITOR_H
#define C_EDITOR_H

typedef struct Camera_ Camera;

bool  TerrainEditorUpdate(Camera* camera);
bool  EditorGizmoUpdate(Camera* camera);
bool  EditorLightGizmoUpdate(Camera* camera);
void  EditorPickingUpdate(Camera* camera);
void  EditorInit(void);
void  EditorConsoleInit(void);
void  EditorSceneStartup(void);
void  EditorSceneHotkeys(void);

#endif 