#ifndef CP_RENDERING
#define CP_RENDERING

#include "Graphics.h"

typedef struct RenderSettings_
{
    bool enableOcclusion;
    bool enableHBAO;
    bool enableMLAA;
    bool showMLAAEdges;
    f32 hbaoRadius;
    f32 hbaoBias;
    f32 hbaoIntensity;
    f32 hbaoPower;
    f32 mlaaThreshold;
    f32 exposure;
    f32 gamma;
    f32 godRayIntensity;
    f32 sunYaw;
    f32 sunPitch;
    f32 shadowMaxDistance;
    f32 shadowCameraDistance;
    f32 shadowCasterDepthMargin;
    f32 shadowCascadeOverlap;
    f32 shadowSplitNearDistance;
    f32 shadowPSSMLambda;
} RenderSettings;

extern RenderSettings g_RenderSettings;

void DestroyPipeline();

void Quit(int rc);

void Render();

int InitScene();

void InitBuffers();

void RendererInit();

#endif
