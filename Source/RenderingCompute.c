#include "RenderingInternal.h"

void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet,
                                 RenderSetBuffers* buffers,
                                 FrustumPlanes frustumPlanes,
                                 mat4x4 viewProj,
                                 bool enableHiZ,
                                 bool enableVisibilityOutput)
{
    if (renderSet->numGroups == 0) return;
    CHECK_CREATE(g_CullDrawArgsComputePipeline, "Cull Draw Args Compute Pipeline");
    struct {
        FrustumPlanes planes;
        u32 numEntities;
        u32 numPrimitiveGroups;
        u32 mode;
        u32 enableVisibilityOutput;
        mat4x4 viewProjection;
        u32 hiZSize[2];
        u32 hiZMipCount;
        u32 enableHiZ;
        f32 hiZDepthBias;
        f32 hiZPadding[3];
    } params;

    WindowState* winstate = &g_WindowState;
    SDL_GPUTexture* hiZTexture = winstate->tex_hiz;
    u32 hiZWidth = winstate->hiz_width;
    u32 hiZHeight = winstate->hiz_height;
    u32 hiZMipCount = winstate->hiz_mip_count;
    MemCopy(&params.planes, frustumPlanes.planes, sizeof(FrustumPlanes));
    params.numEntities = renderSet->numEntities;
    params.numPrimitiveGroups = renderSet->numGroups;
    params.mode = 0;
    params.enableVisibilityOutput = enableVisibilityOutput ? 1u : 0u;
    params.viewProjection = viewProj;
    params.hiZSize[0] = hiZWidth;
    params.hiZSize[1] = hiZHeight;
    params.hiZMipCount = hiZMipCount;
    params.enableHiZ = enableHiZ ? 1u : 0u;
    params.hiZDepthBias = 0.02f;

    SDL_GPUBuffer* ro_buffers[3] = {
        buffers->entity,
        buffers->primitiveGroup,
        buffers->denseToPrimitive
    };
    SDL_GPUStorageBufferReadWriteBinding rw_bindings[8] = {
        { buffers->drawSparseIndices },
        { buffers->drawArgs },
        { g_RenderState.lineBuffer },
        { g_RenderState.lineDrawArgsBuffer },
        { buffers->visibleSparseIndices },
        { buffers->visibilityMask },
        { buffers->visibleCount },
        { buffers->dispatchArgs }
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_CullDrawArgsComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    if (hiZTexture)
        SDL_BindGPUComputeStorageTextures(pass, 0, &hiZTexture, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

    u32 resetCount = renderSet->numGroups;
    if (enableVisibilityOutput && renderSet->numEntities > resetCount) resetCount = renderSet->numEntities;
    SDL_DispatchGPUCompute(pass, (resetCount + 63) / 64, 1, 1);

    if (renderSet->numEntities > 0)
    {
        params.mode = 1;
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (renderSet->numEntities + 63) / 64, 1, 1);
    }
    SDL_EndGPUComputePass(pass);
}

void DispatchHiZBuildCompute(SDL_GPUCommandBuffer* cmd)
{
    WindowState* winstate = &g_WindowState;
    SDL_GPUTexture* depthTexture = winstate->tex_hiz_depth; 
    SDL_GPUTexture* hiZTexture = winstate->tex_hiz;
    u32 width    = winstate->hiz_width;
    u32 height   = winstate->hiz_height;
    u32 mipCount = winstate->hiz_mip_count;
    if (!depthTexture || !hiZTexture || mipCount == 0) return;
    for (u32 mip = 0; mip < mipCount; mip++)
    {
        u32 outputWidth  = Maxu32(width >> mip, 1);
        u32 outputHeight = Maxu32(height >> mip, 1);
        u32 sourceWidth  = Maxu32((mip == 0) ? width  : (width >> (mip - 1)), 1);
        u32 sourceHeight = Maxu32((mip == 0) ? height : (height >> (mip - 1)), 1);

        SDL_GPUStorageTextureReadWriteBinding rwTexture = {
            .texture = hiZTexture,
            .mip_level = mip,
            .layer = 0,
            .cycle = false
        };
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rwTexture, 1, NULL, 0);
        struct { u32 sourceSize[2]; u32 outputSize[2]; u32 sourceMip; u32 isBaseLevel; u32 padding[2]; } params = {
            { sourceWidth, sourceHeight },
            { outputWidth, outputHeight },
            mip == 0 ? 0u : mip - 1u,
            mip == 0 ? 1u : 0u,
            { 0u, 0u }
        };

        SDL_GPUTextureSamplerBinding depthBinding = { .texture = depthTexture, .sampler = g_RenderState.hiZSampler };
        SDL_BindGPUComputeSamplers(pass, 0, &depthBinding, 1);
        if (mip == 0)
        {
            CHECK_CREATE(g_HiZBuildComputePipeline, "Hi-Z Build Compute Pipeline")
            SDL_BindGPUComputePipeline(pass, g_HiZBuildComputePipeline);
        }
        else
        {
            CHECK_CREATE(g_HiZDownscaleComputePipeline, "Hi-Z Downscale Compute Pipeline");
            SDL_BindGPUComputePipeline(pass, g_HiZDownscaleComputePipeline);
            SDL_BindGPUComputeStorageTextures(pass, 0, &hiZTexture, 1);
        }
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (outputWidth + 7u) / 8u, (outputHeight + 7u) / 8u, 1);
        SDL_EndGPUComputePass(pass);
    }
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

void DispatchHBAOCompute(SDL_GPUCommandBuffer* cmd, bool enabled, u32 width, u32 height)
{
    WindowState* winstate = &g_WindowState;
    if (!winstate->tex_hiz_depth || !winstate->tex_gbuffer_tangent || !winstate->tex_hbao ||
        !winstate->tex_hbao_blur || !winstate->tex_hbao_normal) return;
    if (enabled) CHECK_CREATE(g_ExtractNormalComputePipeline, "Extract Normal Compute Pipeline");
    CHECK_CREATE(g_HBAOComputePipeline, "HBAO Compute Pipeline");
    CHECK_CREATE(g_HBAOBlurComputePipeline, "HBAO Blur Compute Pipeline");

    u32 aoWidth = Maxu32(width / 2u, 1u);
    u32 aoHeight = Maxu32(height / 2u, 1u);

    struct {
        mat4x4 invProjection;
        mat4x4 view;
        u32 fullSize[2];
        u32 aoSize[2];
        f32 radius;
        f32 projectionScale;
        f32 bias;
        f32 intensity;
        f32 power;
        u32 enabled;
        u32 frameIndex;
        u32 padding;
    } params;

    params.invProjection = g_Camera.inverseProjection;
    params.view = g_Camera.view;
    params.fullSize[0] = width;
    params.fullSize[1] = height;
    params.aoSize[0] = aoWidth;
    params.aoSize[1] = aoHeight;
    params.radius = Maxf32(g_RenderSettings.hbaoRadius, 0.001f);
    params.projectionScale = g_Camera.projection.m[1][1] * (f32)height * 0.5f;
    params.bias = g_RenderSettings.hbaoBias;
    params.intensity = g_RenderSettings.hbaoIntensity;
    params.power = Maxf32(g_RenderSettings.hbaoPower, 0.001f);
    params.enabled = enabled ? 1u : 0u;
    params.frameIndex = (u32)SDL_GetTicks();
    params.padding = 0u;

    SDL_GPUComputePass* pass;
    if (enabled)
    {
        SDL_GPUStorageTextureReadWriteBinding normalOutput = {
            .texture = winstate->tex_hbao_normal,
            .mip_level = 0,
            .layer = 0,
            .cycle = true
        };
        pass = SDL_BeginGPUComputePass(cmd, &normalOutput, 1, NULL, 0);
        SDL_BindGPUComputePipeline(pass, g_ExtractNormalComputePipeline);
        SDL_BindGPUComputeSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
            .texture = winstate->tex_gbuffer_tangent,
            .sampler = g_RenderState.hiZSampler
        }, 1);
        SDL_DispatchGPUCompute(pass, (aoWidth + 7u) / 8u, (aoHeight + 7u) / 8u, 1);
        SDL_EndGPUComputePass(pass);
    }

    SDL_GPUStorageTextureReadWriteBinding hbaoOutput = {
        .texture = winstate->tex_hbao,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    pass = SDL_BeginGPUComputePass(cmd, &hbaoOutput, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_HBAOComputePipeline);
    SDL_GPUTextureSamplerBinding hbaoInputs[2] = {
        { .texture = winstate->tex_hiz_depth, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hbao_normal, .sampler = g_RenderState.hiZSampler }
    };
    SDL_BindGPUComputeSamplers(pass, 0, hbaoInputs, SDL_arraysize(hbaoInputs));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (aoWidth + 7u) / 8u, (aoHeight + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUStorageTextureReadWriteBinding blurOutput = {
        .texture = winstate->tex_hbao_blur,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    pass = SDL_BeginGPUComputePass(cmd, &blurOutput, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_HBAOBlurComputePipeline);
    SDL_GPUTextureSamplerBinding blurInputs[2] = {
        { .texture = winstate->tex_hbao, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hiz_depth, .sampler = g_RenderState.hiZSampler }
    };
    SDL_BindGPUComputeSamplers(pass, 0, blurInputs, SDL_arraysize(blurInputs));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (aoWidth + 7u) / 8u, (aoHeight + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);
}

void DispatchDeferredLightingCompute(SDL_GPUCommandBuffer* cmd, u32 width, u32 height, mat4x4 viewProj)
{
    WindowState* winstate = &g_WindowState;
    if (!winstate->tex_gbuffer_tangent || !winstate->tex_gbuffer_albedo_metallic ||
        !winstate->tex_gbuffer_shadow_roughness || !winstate->tex_hiz_depth ||
        !winstate->tex_hbao_blur || !winstate->tex_color)
    {
        AX_LOG("Deferred lighting is not ready yet");
        return;
    }
    CHECK_CREATE(g_DeferredLightingComputePipeline, "Deferred Lighting Compute Pipeline");

    SDL_GPUStorageTextureReadWriteBinding output = {
        .texture = winstate->tex_color,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &output, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_DeferredLightingComputePipeline);

    SDL_GPUTextureSamplerBinding inputs[5] = {
        { .texture = winstate->tex_gbuffer_tangent, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_gbuffer_albedo_metallic, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_gbuffer_shadow_roughness, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hiz_depth, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hbao_blur, .sampler = g_RenderState.sampler }
    };
    SDL_BindGPUComputeSamplers(pass, 0, inputs, SDL_arraysize(inputs));

    float3 sunDirection = GetRenderSunDirection();
    struct {
        u32 outputSize[2];
        f32 padding0[2];
        mat4x4 invViewProj;
        f32 cameraPosition[4];
        f32 sunDirection[4];
    } params = {0};
    params.outputSize[0] = width;
    params.outputSize[1] = height;
    params.invViewProj = M44Inverse(viewProj);
    params.cameraPosition[0] = g_Camera.position.x;
    params.cameraPosition[1] = g_Camera.position.y;
    params.cameraPosition[2] = g_Camera.position.z;
    params.sunDirection[0] = sunDirection.x;
    params.sunDirection[1] = sunDirection.y;
    params.sunDirection[2] = sunDirection.z;
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (width + 7u) / 8u, (height + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);
}

static float2 GetGodRaySunPos(mat4x4 viewProj, float* intensity)
{
    float3 dir = GetRenderSunDirection();
    float3 sunWorld = F3Add(g_Camera.position, F3MulF(dir, 100.0f));
    v128f clip = Vec3Transform(Vec3Load(&sunWorld.x), viewProj.r);
    float w = VecGetW(clip);
    float facing = F3Dot(g_Camera.Front, dir);
    if (facing <= -0.2f)
    {
        *intensity = 0.0f;
        return (float2){ -10.0f, -10.0f };
    }

    if (w <= 0.0001f) w = 0.0001f;
    v128f ndcV = VecDiv(clip, VecSet1(w));
    float ndcX = VecGetX(ndcV);
    float ndcY = VecGetY(ndcV);
    *intensity *= Saturatef32((facing + 0.2f) / 0.7f);
    return (float2){ Clampf32(ndcX * 0.5f + 0.5f, -0.5f, 1.5f), Clampf32(0.5f - ndcY * 0.5f, -0.5f, 1.5f) };
}

static f32 GetSkyPhaseTime(void)
{
    static double startupPhase = 0.0;
    if (startupPhase == 0.0)
    {
        SDL_Time wallTime = 0;
        if (SDL_GetCurrentTime(&wallTime))
        {
            const SDL_Time dayNS = (SDL_Time)86400 * 1000000000;
            startupPhase = (double)(wallTime % dayNS) / 1000000000.0;
        }
    }
    return (f32)(startupPhase * 0.01);
}

void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* depth, SDL_GPUTexture* destination, u32 width, u32 height, mat4x4 viewProj)
{
    CHECK_CREATE(g_TonemapComputePipeline, "Tonemap Compute Pipeline");
    SDL_GPUStorageTextureReadWriteBinding rwTexture = {
        .texture = destination,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rwTexture, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_TonemapComputePipeline);
    SDL_GPUTextureSamplerBinding inputs[3] = {
        { .texture = source, .sampler = g_RenderState.sampler },
        { .texture = depth, .sampler = g_RenderState.hiZSampler },
        { .texture = g_RenderState.skyNoise3D, .sampler = g_RenderState.sampler }
    };
    SDL_BindGPUComputeSamplers(pass, 0, inputs, SDL_arraysize(inputs));

    float godRayIntensity = g_RenderSettings.godRayIntensity;
    float2 sunPos = GetGodRaySunPos(viewProj, &godRayIntensity);
    struct {
        u32 outputSize[2];
        f32 exposure;
        f32 gamma;
        f32 sunPos[2];
        f32 godRayIntensity;
        f32 time;
        f32 cloudTime;
        f32 padding[3];
        mat4x4 invViewProj;
        f32 cameraPosition[4];
        f32 sunDirection[4];
    } params = {0};
    float3 sunDir = GetRenderSunDirection();
    params.outputSize[0] = width;
    params.outputSize[1] = height;
    params.exposure = g_RenderSettings.exposure;
    params.gamma = g_RenderSettings.gamma;
    params.sunPos[0] = sunPos.x;
    params.sunPos[1] = sunPos.y;
    params.godRayIntensity = godRayIntensity;
    params.cloudTime = TimeSinceStartup();
    params.time = GetSkyPhaseTime() + params.cloudTime;
    params.invViewProj = M44Inverse(viewProj);
    params.cameraPosition[0] = g_Camera.position.x;
    params.cameraPosition[1] = g_Camera.position.y;
    params.cameraPosition[2] = g_Camera.position.z;
    params.sunDirection[0] = sunDir.x;
    params.sunDirection[1] = sunDir.y;
    params.sunDirection[2] = sunDir.z;
    params.sunDirection[2] = FModf(params.time, MATH_TwoPI);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (width + 7u) / 8u, (height + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);
}

void DispatchMLAACompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* destination, u32 width, u32 height, f32 threshold, bool showEdges)
{
    WindowState* winstate = &g_WindowState;
    if (!source || !destination || !winstate->tex_mlaa_edge_mask || !winstate->tex_mlaa_edge_count)
    {
        AX_WARN("MLAA resources are not ready");
        return;
    }
    CHECK_CREATE(g_MLAAEdgeMaskComputePipeline, "MLAA Edge Mask Compute Pipeline");
    CHECK_CREATE(g_MLAALineLengthComputePipeline, "MLAA Line Length Compute Pipeline");
    CHECK_CREATE(g_MLAABlendComputePipeline, "MLAA Blend Compute Pipeline");

    struct {
        u32 outputSize[2];
        f32 threshold;
        u32 showEdges;
    } params = {
        { width, height },
        threshold,
        showEdges ? 1u : 0u
    };

    SDL_GPUStorageTextureReadWriteBinding edgeMaskOutput = {
        .texture = winstate->tex_mlaa_edge_mask,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &edgeMaskOutput, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_MLAAEdgeMaskComputePipeline);
    SDL_GPUTextureSamplerBinding sourceBinding = { .texture = source, .sampler = g_RenderState.hiZSampler };
    SDL_BindGPUComputeSamplers(pass, 0, &sourceBinding, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (width + 7u) / 8u, (height + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUStorageTextureReadWriteBinding edgeCountOutput = {
        .texture = winstate->tex_mlaa_edge_count,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    pass = SDL_BeginGPUComputePass(cmd, &edgeCountOutput, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_MLAALineLengthComputePipeline);
    SDL_BindGPUComputeStorageTextures(pass, 0, &winstate->tex_mlaa_edge_mask, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (width + 7u) / 8u, (height + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUStorageTextureReadWriteBinding blendOutput = {
        .texture = destination,
        .mip_level = 0,
        .layer = 0,
        .cycle = true
    };
    pass = SDL_BeginGPUComputePass(cmd, &blendOutput, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, g_MLAABlendComputePipeline);
    SDL_BindGPUComputeSamplers(pass, 0, &sourceBinding, 1);
    SDL_GPUTexture* readonlyTextures[2] = { winstate->tex_mlaa_edge_mask, winstate->tex_mlaa_edge_count };
    SDL_BindGPUComputeStorageTextures(pass, 0, readonlyTextures, SDL_arraysize(readonlyTextures));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (width + 7u) / 8u, (height + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);
}

void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet)
{
    if (renderSet->numEntities == 0) return;
    CHECK_CREATE(g_AnimComputePipeline, "Animation Compute Pipeline")

    struct {
        float timeSinceStartup;
        int   numInstances;
    } params;
    params.timeSinceStartup = (float)TimeSinceStartup();
    params.numInstances     = (int)renderSet->numEntities;

    SDL_GPUStorageBufferReadWriteBinding rw_bindings[1] = {
        { g_RenderState.boneBuffer }
    };

    SDL_GPUBuffer* ro_buffers[7] = {
        g_RenderState.animPoseBuffer,
        g_RenderState.animHierarchyBuffer,
        g_RenderState.animDataBuffer,
        g_RenderState.jointsBuffer,
        g_RenderState.invBindBuffer,
        g_RenderState.animInstanceBuffer,
        g_RenderState.skinnedBuffers.visibleSparseIndices
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_AnimComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUComputeIndirect(pass, g_RenderState.skinnedBuffers.dispatchArgs, 0);

    SDL_EndGPUComputePass(pass);
}

void DispatchAnimateVerticesCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet)
{
    if (renderSet->numGroups == 0) return;
    CHECK_CREATE(g_AnimVerticesPipeline, "Animation vertices Pipeline")

    struct {
        u32 numPrimitiveGroups;
        u32 maxAnimatedVertices;
        u32 padding[2];
    } params;
    params.numPrimitiveGroups = renderSet->numGroups;
    params.maxAnimatedVertices = MAX_ANIMATED_VERTEX;
    params.padding[0] = params.padding[1] = 0;

    SDL_GPUStorageBufferReadWriteBinding rw_bindings[1] = {
        { g_RenderState.skinnedAnimatedVertices }
    };

    SDL_GPUBuffer* ro_buffers[6] = {
        g_RenderState.boneBuffer,
        g_RenderState.skinnedBuffers.entity,
        g_RenderState.skinnedBuffers.primitiveGroup,
        g_RenderState.skinnedBuffers.drawSparseIndices,
        g_RenderState.skinnedVertexBuffer,
        g_RenderState.skinnedBuffers.drawArgs
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_AnimVerticesPipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUComputeIndirect(pass, g_RenderState.skinnedBuffers.dispatchArgs, sizeof(u32) * 3);
    SDL_EndGPUComputePass(pass);
}
