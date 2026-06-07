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

int InitScene();

void InitBuffers();

void RendererInit();

void RendererSetLights(const LightGPU* lights, u32 count);
u32 RendererGetLightCount(void);
RenderLightDebugInfo RendererGetLightDebugInfo(void);

#endif
