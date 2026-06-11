#ifndef CP_RENDERING
#define CP_RENDERING

#include "Graphics.h"

typedef struct RenderSettings_
{
    bool enableOcclusion;
    bool enableHBAO;
    bool enableMLAA;
    bool showMLAAEdges;
    bool enableLocalLights;
    bool enableLightFrustumCulling;
    bool enableLightOcclusionCulling;
    bool showLightRects;
    f32 hbaoRadius;
    f32 hbaoBias;
    f32 hbaoIntensity;
    f32 hbaoPower;
    f32 mlaaThreshold;
    f32 exposure;
    f32 gamma;
    f32 godRayIntensity;
    f32 godRaySamples;    // ray march steps, 0 disables
    f32 hbaoDirections;   // horizon sample directions
    f32 lodDistanceModifier;
    f32 sunYaw;
    f32 sunPitch;
    f32 shadowMaxDistance;
    f32 shadowCameraDistance;
    f32 shadowCasterDepthMargin;
    f32 shadowCascadeOverlap;
    f32 shadowSplitNearDistance;
    f32 shadowPSSMLambda;
    f32 maxVisiblePointShadows;
    f32 maxVisibleSpotShadows;
} RenderSettings;

typedef struct RenderLightDebugInfo_
{
    u32 totalLights;
    u32 submittedLights;
    u32 maxLights;
} RenderLightDebugInfo;

extern RenderSettings g_RenderSettings;

void DestroyPipeline();

void Quit(int rc);

void Render();

void InitBuffers();

void RendererInit();

void RendererSetLights(const LightGPU* lights, u32 count);

#define MAX_OUTLINE_TARGETS 256u

// one outlined render set entity of the active scene
typedef struct OutlineTarget_
{
    u32 skinned;
    u32 groupIdx;
    u32 entityIdx;
} OutlineTarget;

// editor selection outlines, the gizmo submits every entity of the selection each frame
void RendererSetOutlineTargets(const OutlineTarget* targets, u32 count);

void RendererClearOutlineTarget(void);

#define MAX_GIZMO_VERTICES 1024u

// world space line overlay drawn on top of everything (no depth), the editor gizmo
// submits its lines every frame, count 0 hides it
void RendererSetGizmoLines(const ALineVertex* vertices, u32 count);

u32 RendererGetLightCount(void);

RenderLightDebugInfo RendererGetLightDebugInfo(void);

#endif
