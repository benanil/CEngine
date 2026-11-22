
#ifndef C_EDITOR_H
#define C_EDITOR_H
#include "Include/SIMD.h"
typedef struct Camera_ Camera;

bool  TerrainEditorUpdate(Camera* camera);
bool  EditorGizmoUpdate(Camera* camera);
bool  EditorLightGizmoUpdate(Camera* camera);
void  EditorPickingUpdate(Camera* camera);
bool  EditorSceneInteractAllowed(void); // cursor is over the 3d scene, not an editor panel
bool  EditorInit();
void  EditorConsoleInit(void);
void  EditorSceneStartup(void);
void  EditorSceneHotkeys(void);

void GraphicsEditorUI(void);
void GraphicsEditorInit();

void SetGizmoLines();
void GizmoSetLineWidth(f32 width);
void GizmoLine(v128f a, v128f b, u32 color);
void GizmoQuad(v128f a, v128f b, v128f c, v128f d, u32 color);
void GizmoRing(v128f center, v128f axis, f32 radius, u32 color);
void GizmoArrow(v128f center, v128f axisDir, f32 size, u32 color);
void GizmoPlane(v128f center, v128f spanA, v128f spanB, f32 start, f32 end, u32 color);
void GizmoCapsule(v128f a, v128f b, f32 radius, u32 color);
void GizmoCapsuleAt(v128f position, f32 height, f32 radius, u32 color);
void GizmoAABB(v128f worldMin, v128f worldMax, u32 color);
void GizmoCube(v128f position, v128f halfExtend, u32 color);

#endif 