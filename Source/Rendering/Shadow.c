#include "RenderingInternal.h"

#include "Shaders/spv/SDSMDepthBoundsInitial.spv.h"
#include "Shaders/spv/SDSMDepthBoundsReduce.spv.h"
#include "Shaders/spv/SDSMSetupShadows.spv.h"

SDL_GPUComputePipeline* g_SDSMDepthBoundsInitialPipeline = NULL;
SDL_GPUComputePipeline* g_SDSMDepthBoundsReducePipeline = NULL;
SDL_GPUComputePipeline* g_SDSMSetupShadowsPipeline = NULL;

void InitSDSM()
{
    g_SDSMDepthBoundsInitialPipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_SDSMDepthBoundsInitial_spv,
        .code_size                      = sizeof(Shaders_SDSMDepthBoundsInitial_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_SDSMDepthBoundsInitialPipeline, "SDSM Depth Bounds Initial Pipeline");

    g_SDSMDepthBoundsReducePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_SDSMDepthBoundsReduce_spv,
        .code_size                      = sizeof(Shaders_SDSMDepthBoundsReduce_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_readonly_storage_textures  = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_SDSMDepthBoundsReducePipeline, "SDSM Depth Bounds Reduce Pipeline");

    g_SDSMSetupShadowsPipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_SDSMSetupShadows_spv,
        .code_size                      = sizeof(Shaders_SDSMSetupShadows_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_readonly_storage_textures  = 1,
        .num_readwrite_storage_buffers  = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 1,
        .threadcount_y                  = 1,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_SDSMSetupShadowsPipeline, "SDSM Setup Shadows Pipeline");
}

static inline v128f VCALL TransformPoint(mat4x4 m, v128f p)
{
    return Vec3Transform(p, m.r);
}

static inline v128f VCALL AddScaled(v128f a, v128f b, float scale)
{
    return VecFmadd(b, VecSet1(scale), a);
}

static float CascadeSplitDistance(float shadowNear, float shadowFar, u32 cascade)
{
    float splitNear = Maxf32(shadowNear, Minf32(g_RenderSettings.shadowSplitNearDistance, shadowFar * 0.5f));
    float p = (float)(cascade + 1u) / (float)SHADOW_CASCADE_COUNT;
    float logSplit = splitNear * Powf(shadowFar / splitNear, p);
    float uniformSplit = Lerpf(splitNear, shadowFar, p);
    return Maxf32(shadowNear, Minf32(shadowFar, Lerpf(uniformSplit, logSplit, Saturatef32(g_RenderSettings.shadowPSSMLambda))));
}

ShadowCascadeData GetShadowCascades(void)
{
    ShadowCascadeData result;
    float shadowNear = Maxf32(g_Camera.nearClip, 0.01f);
    float shadowFar  = Minf32(g_Camera.farClip, Maxf32(g_RenderSettings.shadowMaxDistance, g_Camera.nearClip + 1.0f));
    float aspect     = (float)Maxs32(g_Camera.viewportSize.x, 1) / (float)Maxs32(g_Camera.viewportSize.y, 1);
    float tanHalfFov = Tan(g_Camera.verticalFOV * MATH_DegToRad * 0.5f);

    float3 sunLightDir   = GetRenderSunDirection();
    v128f lightDir       = Vec3Load(&sunLightDir.x);
    v128f lightViewDir   = VecMul(lightDir, VecSet1(-1.0f));
    v128f cameraPosition = Vec3Load(&g_Camera.position.x);
    v128f cameraFront    = Vec3Load(&g_Camera.Front.x);
    v128f cameraRight    = Vec3Load(&g_Camera.Right.x);
    v128f cameraUp       = Vec3Load(&g_Camera.Up.x);
    float previousSplit = shadowNear;

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        float split = CascadeSplitDistance(shadowNear, shadowFar, cascade);
        result.splitDistances[cascade] = split;
        float overlap = Maxf32(g_RenderSettings.shadowCascadeOverlap, 0.0f);
        float nearDist = cascade > 0 ? Maxf32(shadowNear, previousSplit - overlap) : previousSplit;
        float farDist  = cascade + 1u < SHADOW_CASCADE_COUNT ? Minf32(shadowFar, split + overlap) : split;
        previousSplit  = split;

        float nearH = tanHalfFov * nearDist;
        float nearW = nearH * aspect;
        float farH  = tanHalfFov * farDist;
        float farW  = farH * aspect;
        v128f nearCenter = AddScaled(cameraPosition, cameraFront, nearDist);
        v128f farCenter  = AddScaled(cameraPosition, cameraFront, farDist);
        v128f corners[8] = {
            AddScaled(AddScaled(nearCenter, cameraUp,  nearH), cameraRight, -nearW),
            AddScaled(AddScaled(nearCenter, cameraUp,  nearH), cameraRight,  nearW),
            AddScaled(AddScaled(nearCenter, cameraUp, -nearH), cameraRight, -nearW),
            AddScaled(AddScaled(nearCenter, cameraUp, -nearH), cameraRight,  nearW),
            AddScaled(AddScaled(farCenter,  cameraUp,  farH),  cameraRight, -farW),
            AddScaled(AddScaled(farCenter,  cameraUp,  farH),  cameraRight,  farW),
            AddScaled(AddScaled(farCenter,  cameraUp, -farH),  cameraRight, -farW),
            AddScaled(AddScaled(farCenter,  cameraUp, -farH),  cameraRight,  farW)
        };

        v128f center = VecAdd(VecAdd(corners[0], corners[1]), VecAdd(corners[2], corners[3]));
        center = VecAdd(center, VecAdd(VecAdd(corners[4], corners[5]), VecAdd(corners[6], corners[7])));
        center = VecMul(center, VecSet1(1.0f / 8.0f));

        v128f radiusSq = VecZero();
        for (u32 i = 0; i < 8u; i++)
        {
            v128f toCorner = VecSub(corners[i], center);
            radiusSq = VecMax(radiusSq, Vec3DotV(toCorner, toCorner));
        }
        float radius = Maxf32(Sqrtf(VecGetX(radiusSq)), 1.0f);
        float casterMargin = Maxf32(g_RenderSettings.shadowCasterDepthMargin, 1.0f);
        float eyeDistance = radius + Maxf32(g_RenderSettings.shadowCameraDistance, 1.0f) + casterMargin;
        v128f eye = AddScaled(center, lightDir, eyeDistance);
        
        // prevent LookAt NaN failures if the sun points straight down (e.g. at noon)
        v128f upVec = VecSetR(0.0f, 1.0f, 0.0f, 0.0f);
        if (Absf32(sunLightDir.y) > 0.999f) {
            upVec = VecSetR(0.0f, 0.0f, 1.0f, 0.0f);
        }
        mat4x4 view = M44LookAtRHVec(eye, lightViewDir, upVec);
        v128f minLight = VecSet1( FLT_MAX);
        v128f maxLight = VecSet1(-FLT_MAX);
        for (u32 i = 0; i < 8u; i++)
        {
            v128f cornerLight = TransformPoint(view, corners[i]);
            minLight = VecMin(minLight, cornerLight);
            maxLight = VecMax(maxLight, cornerLight);
        }

        v128f extentXY = VecSub(maxLight, minLight);
        extentXY = VecMax(extentXY, VecSwapPairs(extentXY));
        float extent = Ceilf(VecGetX(VecAddf(extentXY, 2.0f)));
        float halfExtent = extent * 0.5f;
        float texelSize = extent / (float)Maxu32(SHADOW_MAP_SIZE >> cascade, 1u);
        v128f centerLight = VecMul(VecAdd(minLight, maxLight), VecSet1(0.5f));
        centerLight = VecMul(VecFloor(VecAddf(VecDiv(centerLight, VecSet1(texelSize)), 0.5f)), VecSet1(texelSize));

        mat4x4 proj = M44OrthoRH(VecGetX(centerLight) - halfExtent, VecGetX(centerLight) + halfExtent,
                                 VecGetY(centerLight) - halfExtent, VecGetY(centerLight) + halfExtent,
                                 SHADOW_NEAR_PLANE, eyeDistance + radius + casterMargin);
        result.lightViewProj[cascade] = M44Multiply(view, proj);
    }

    return result;
}

void DispatchSDSMSetupShadowsCompute(SDL_GPUCommandBuffer* cmd, mat4x4 viewProj)
{
    WindowState* winstate = &g_WindowState;
    if (!winstate->tex_sdsm_bounds || !g_RenderState.shadowCascadeBuffer) return;
    CHECK_CREATE(g_SDSMSetupShadowsPipeline, "SDSM Setup Shadows Pipeline");

    float3 sunDirection = GetRenderSunDirection();
    struct {
        mat4x4 invViewProj;
        f32 cameraPosition[4];
        f32 cameraForward[4];
        f32 cameraRight[4];
        f32 cameraUp[4];
        f32 sunDirection[4];
        f32 setupParams[4];
        f32 fovParams[4];
    } params;
    params.invViewProj = M44Inverse(viewProj);
    params.cameraPosition[0] = g_Camera.position.x;
    params.cameraPosition[1] = g_Camera.position.y;
    params.cameraPosition[2] = g_Camera.position.z;
    params.cameraPosition[3] = 0.0f;
    params.cameraForward[0] = g_Camera.Front.x;
    params.cameraForward[1] = g_Camera.Front.y;
    params.cameraForward[2] = g_Camera.Front.z;
    params.cameraForward[3] = 0.0f;
    params.cameraRight[0] = g_Camera.Right.x;
    params.cameraRight[1] = g_Camera.Right.y;
    params.cameraRight[2] = g_Camera.Right.z;
    params.cameraRight[3] = 0.0f;
    params.cameraUp[0] = g_Camera.Up.x;
    params.cameraUp[1] = g_Camera.Up.y;
    params.cameraUp[2] = g_Camera.Up.z;
    params.cameraUp[3] = 0.0f;
    params.sunDirection[0] = sunDirection.x;
    params.sunDirection[1] = sunDirection.y;
    params.sunDirection[2] = sunDirection.z;
    params.sunDirection[3] = 0.0f;
    params.setupParams[0] = Maxf32(g_Camera.nearClip, 0.01f);
    params.setupParams[1] = Minf32(g_Camera.farClip, Maxf32(g_RenderSettings.shadowMaxDistance, g_Camera.nearClip + 1.0f));
    params.setupParams[2] = Maxf32(g_RenderSettings.shadowMaxDistance, g_Camera.nearClip + 1.0f);
    params.setupParams[3] = winstate->sdsm_valid ? 1.0f : 0.0f;
    params.fovParams[0] = Tan(g_Camera.verticalFOV * MATH_DegToRad * 0.5f);
    params.fovParams[1] = (float)Maxs32(g_Camera.viewportSize.x, 1) / (float)Maxs32(g_Camera.viewportSize.y, 1);
    params.fovParams[2] = Maxf32(g_RenderSettings.shadowCascadeOverlap, 0.0f);
    params.fovParams[3] = Saturatef32(g_RenderSettings.shadowPSSMLambda);

    SDL_GPUStorageBufferReadWriteBinding rw = { g_RenderState.shadowCascadeBuffer };
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, g_SDSMSetupShadowsPipeline);
    SDL_BindGPUComputeStorageTextures(pass, 0, &winstate->tex_sdsm_bounds, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, 1, 1, 1);
    SDL_EndGPUComputePass(pass);
}

void DispatchSDSMDepthBoundsCompute(SDL_GPUCommandBuffer* cmd)
{
    WindowState* winstate = &g_WindowState;
    SDL_GPUTexture* depthTexture = winstate->tex_hiz_depth;
    SDL_GPUTexture* boundsTexture = winstate->tex_sdsm_bounds;
    u32 width = Maxu32((winstate->hiz_width + 1u) / 2u, 1u);
    u32 height = Maxu32((winstate->hiz_height + 1u) / 2u, 1u);
    u32 mipCount = winstate->sdsm_mip_count;
    if (!depthTexture || !boundsTexture || mipCount == 0) return;

    for (u32 mip = 0; mip < mipCount; mip += 3u)
    {
        u32 outputWidth = Maxu32(width >> mip, 1u);
        u32 outputHeight = Maxu32(height >> mip, 1u);
        u32 sourceWidth = (mip == 0) ? winstate->hiz_width : Maxu32(width >> (mip - 3u), 1u);
        u32 sourceHeight = (mip == 0) ? winstate->hiz_height : Maxu32(height >> (mip - 3u), 1u);
        SDL_GPUStorageTextureReadWriteBinding rwTexture = { .texture = boundsTexture, .mip_level = mip, .layer = 0, .cycle = false };
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rwTexture, 1, NULL, 0);
        struct { u32 outputSize[2]; u32 sourceSize[2]; u32 sourceMip; u32 padding; } params = {
            { outputWidth, outputHeight },
            { sourceWidth, sourceHeight },
            mip == 0 ? 0u : mip - 3u,
            0u
        };
        if (mip == 0)
        {
            CHECK_CREATE(g_SDSMDepthBoundsInitialPipeline, "SDSM Depth Bounds Initial Pipeline");
            SDL_BindGPUComputePipeline(pass, g_SDSMDepthBoundsInitialPipeline);
            SDL_GPUTextureSamplerBinding depthBinding = { .texture = depthTexture, .sampler = g_RenderState.hiZSampler };
            SDL_BindGPUComputeSamplers(pass, 0, &depthBinding, 1);
        }
        else
        {
            CHECK_CREATE(g_SDSMDepthBoundsReducePipeline, "SDSM Depth Bounds Reduce Pipeline");
            SDL_BindGPUComputePipeline(pass, g_SDSMDepthBoundsReducePipeline);
            SDL_BindGPUComputeStorageTextures(pass, 0, &boundsTexture, 1);
        }
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        if (mip == 0)
        {
            SDL_DispatchGPUCompute(pass, (outputWidth + 7u) / 8u, (outputHeight + 7u) / 8u, 1);
        }
        else
        {
            SDL_DispatchGPUCompute(pass, outputWidth, outputHeight, 1);
        }
        SDL_EndGPUComputePass(pass);
    }
    winstate->sdsm_valid = true;
}


void DestroySDSM()
{
    if (g_SDSMDepthBoundsInitialPipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_SDSMDepthBoundsInitialPipeline);
    if (g_SDSMDepthBoundsReducePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_SDSMDepthBoundsReducePipeline);
    if (g_SDSMSetupShadowsPipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_SDSMSetupShadowsPipeline);
}