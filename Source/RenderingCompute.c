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
    params.hiZDepthBias = 0.005f;

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

        if (mip == 0)
        {
            CHECK_CREATE(g_HiZBuildComputePipeline, "Hi-Z Build Compute Pipeline")
            SDL_BindGPUComputePipeline(pass, g_HiZBuildComputePipeline);
            SDL_GPUTextureSamplerBinding depthBinding = { .texture = depthTexture, .sampler = g_RenderState.hiZSampler };
            SDL_BindGPUComputeSamplers(pass, 0, &depthBinding, 1);
            SDL_BindGPUComputeStorageTextures(pass, 0, &hiZTexture, 1);
        }
        else
        {
            CHECK_CREATE(g_HiZDownscaleComputePipeline, "Hi-Z Downscale Compute Pipeline")
            SDL_BindGPUComputePipeline(pass, g_HiZDownscaleComputePipeline);
            SDL_BindGPUComputeStorageTextures(pass, 0, &hiZTexture, 1);
        }
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (outputWidth + 7u) / 8u, (outputHeight + 7u) / 8u, 1);
        SDL_EndGPUComputePass(pass);
    }
}

void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* destination, u32 width, u32 height)
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
    SDL_BindGPUComputeSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
        .texture = source,
        .sampler = g_RenderState.sampler
    }, 1);
    struct { u32 outputSize[2]; f32 exposure; f32 gamma; } params = {
        { width, height }, 1.0f, 2.2f
    };
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
