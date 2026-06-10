#include "RenderingInternal.h"
#include "Include/TextureSystem.h"

float3 GetRenderSunDirection(void)
{
    f32 yaw = g_RenderSettings.sunYaw * MATH_DegToRad;
    f32 pitch = g_RenderSettings.sunPitch * MATH_DegToRad;
    float3 direction = {
        Cos(yaw) * Cos(pitch),
        Sin(pitch),
        Sin(yaw) * Cos(pitch)
    };
    return F3NormSafe(direction);
}

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx)
{
    SDL_GPUBufferBinding index_binding = { g_RenderState.indexBuffer, 0 };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTarget, ctx->colorTarget ? 1 : 0, ctx->depthTarget);
    if (ctx->viewport) SDL_SetGPUViewport(pass, ctx->viewport);
    if (ctx->scissor) SDL_SetGPUScissor(pass, ctx->scissor);

    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinned.vertexBuffer, 0 };
        SDL_GPUTextureSamplerBinding albedoSampler = { .texture = scene->textureSystem.classes[TextureClass_Albedo].pages.handle, .sampler = g_RenderState.sampler };
        SDL_GPUBuffer* fragmentBuffers[2] = {
            scene->textureSystem.materialBuffer,
            scene->textureSystem.descriptorBuffer
        };

        if (scene->skinnedSet.numGroups > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, ctx->skinnedPipeline);
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_GPUBuffer* buffers[5] = {
                scene->skinnedBuffers.entity,
                scene->skinnedBuffers.primitiveGroup,
                scene->skinnedBuffers.drawSparseIndices,
                g_RenderState.skinned.animatedVertices,
                ctx->usePointShadowSides ? g_RenderState.pointShadowMatrixBuffer : (ctx->useSpotShadowSides ? g_RenderState.spotShadowMatrixBuffer : g_RenderState.shadowCascadeBuffer)
            };
            SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, (ctx->useShadowCascades || ctx->usePointShadowSides || ctx->useSpotShadowSides) ? SDL_arraysize(buffers) : 4);
            if (ctx->alphaClip)
            {
                SDL_BindGPUFragmentSamplers(pass, 0, &albedoSampler, 1);
                SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
            }
            if (ctx->useShadowCascades || ctx->usePointShadowSides || ctx->useSpotShadowSides) SDL_PushGPUVertexUniformData(cmd, 0, &ctx->cascadeIndex, sizeof(u32));
            else SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
            SDL_DrawGPUIndexedPrimitivesIndirect(pass, scene->skinnedBuffers.drawArgs, 0, scene->skinnedSet.numGroups * MESH_LOD_COUNT);
        }

        if (scene->surfaceSet.numGroups > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, ctx->surfacePipeline);
            vertex_binding.buffer = g_RenderState.surface.vertexBuffer;
            vertex_binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_GPUBuffer* surfaceBuffers[4] = {
                scene->surfaceBuffers.entity,
                scene->surfaceBuffers.primitiveGroup,
                scene->surfaceBuffers.drawSparseIndices,
                ctx->usePointShadowSides ? g_RenderState.pointShadowMatrixBuffer : (ctx->useSpotShadowSides ? g_RenderState.spotShadowMatrixBuffer : g_RenderState.shadowCascadeBuffer)
            };
            SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, (ctx->useShadowCascades || ctx->usePointShadowSides || ctx->useSpotShadowSides) ? SDL_arraysize(surfaceBuffers) : 3);
            if (ctx->alphaClip)
            {
                SDL_BindGPUFragmentSamplers(pass, 0, &albedoSampler, 1);
                SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
            }
            if (ctx->useShadowCascades || ctx->usePointShadowSides || ctx->useSpotShadowSides) SDL_PushGPUVertexUniformData(cmd, 0, &ctx->cascadeIndex, sizeof(u32));
            else SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
            SDL_DrawGPUIndexedPrimitivesIndirect(pass, scene->surfaceBuffers.drawArgs, 0, scene->surfaceSet.numGroups * MESH_LOD_COUNT);
        }
    }

    SDL_EndGPURenderPass(pass);
}

void RenderScene(SDL_GPUCommandBuffer* cmd, const ScenePassContext* ctx)
{
    struct {
        mat4x4 viewProj;
        float cameraPosition[4];
        float cameraForward[4];
    } vertexParams;
    u32 totalGroups = 0;
    for (u32 s = 0; s < g_NumActiveScenes; s++)
        totalGroups += g_ActiveScenes[s]->skinnedSet.numGroups + g_ActiveScenes[s]->surfaceSet.numGroups;
    if (totalGroups == 0)
    {
        return;
    }
    vertexParams.viewProj = ctx->viewProj;
    vertexParams.cameraPosition[0] = g_Camera.position.x;
    vertexParams.cameraPosition[1] = g_Camera.position.y;
    vertexParams.cameraPosition[2] = g_Camera.position.z;
    vertexParams.cameraPosition[3] = 0.0f;
    vertexParams.cameraForward[0] = g_Camera.Front.x;
    vertexParams.cameraForward[1] = g_Camera.Front.y;
    vertexParams.cameraForward[2] = g_Camera.Front.z;
    vertexParams.cameraForward[3] = 0.0f;

    SDL_GPUBufferBinding index_binding = { g_RenderState.indexBuffer, 0 };
    float3 sunDirection = GetRenderSunDirection();
    struct { f32 viewportSize[4]; f32 sunDirection[4]; } fragmentParams = {
        { (f32)Maxs32(g_Camera.viewportSize.x, 1), (f32)Maxs32(g_Camera.viewportSize.y, 1), 0.0f, 0.0f },
        { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f }
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTargets, ctx->numColorTargets, ctx->depthTarget);

    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinned.vertexBuffer, 0 };
        SDL_GPUTextureSamplerBinding pageSamplers[4] = {
            { .texture = scene->textureSystem.classes[TextureClass_Albedo].pages.handle, .sampler = g_RenderState.sampler },
            { .texture = scene->textureSystem.classes[TextureClass_Normal].pages.handle, .sampler = g_RenderState.sampler },
            { .texture = scene->textureSystem.classes[TextureClass_MetallicRoughness].pages.handle, .sampler = g_RenderState.sampler },
            { .texture = g_WindowState.tex_shadow_color, .sampler = g_RenderState.shadowSampler }
        };
        SDL_GPUBuffer* fragmentBuffers[2] = {
            scene->textureSystem.materialBuffer,
            scene->textureSystem.descriptorBuffer
        };

        if (scene->skinnedSet.numGroups > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, g_RenderState.skinned.pipeline);
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_GPUBuffer* buffers[5] = {
                scene->skinnedBuffers.entity,
                scene->skinnedBuffers.primitiveGroup,
                scene->skinnedBuffers.drawSparseIndices,
                g_RenderState.skinned.animatedVertices,
                g_RenderState.shadowCascadeBuffer
            };
            SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));

            SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
            SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));

            SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
            SDL_PushGPUFragmentUniformData(cmd, 0, &fragmentParams, sizeof(fragmentParams));
            SDL_DrawGPUIndexedPrimitivesIndirect(pass, scene->skinnedBuffers.drawArgs, 0, scene->skinnedSet.numGroups * MESH_LOD_COUNT);
        }

        if (scene->surfaceSet.numGroups > 0)
        {
            SDL_BindGPUGraphicsPipeline(pass, g_RenderState.surface.pipeline);
            vertex_binding.buffer = g_RenderState.surface.vertexBuffer;
            vertex_binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_GPUBuffer* surfaceBuffers[4] = {
                scene->surfaceBuffers.entity,
                scene->surfaceBuffers.primitiveGroup,
                scene->surfaceBuffers.drawSparseIndices,
                g_RenderState.shadowCascadeBuffer
            };
            SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, SDL_arraysize(surfaceBuffers));
            SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
            SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
            SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
            SDL_PushGPUFragmentUniformData(cmd, 0, &fragmentParams, sizeof(fragmentParams));
            SDL_DrawGPUIndexedPrimitivesIndirect(pass, scene->surfaceBuffers.drawArgs, 0, scene->surfaceSet.numGroups * MESH_LOD_COUNT);
        }
    }

    SDL_EndGPURenderPass(pass);
}

void RenderDeferredLights(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, mat4x4 viewProj, u32 width, u32 height)
{
    WindowState* winstate = &g_WindowState;
    if (g_RenderState.numLights == 0u)
        return;
    if (!g_RenderState.deferredLightPipeline || !g_RenderState.lightBuffer || !g_RenderState.lightDrawInfoBuffer ||
        !g_RenderState.pointShadowMatrixBuffer ||
        !g_RenderState.spotShadowMatrixBuffer ||
        !g_RenderState.lightDrawArgsBuffer || !winstate->tex_gbuffer_tangent || !winstate->tex_gbuffer_albedo_metallic ||
        !winstate->tex_gbuffer_shadow_roughness || !winstate->tex_hiz_depth || !winstate->tex_hbao_blur ||
        !winstate->tex_point_shadow_color || !winstate->tex_spot_shadow_color)
    {
        AX_WARN("Deferred light rendering is not ready");
        return;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.deferredLightPipeline);

    SDL_GPUBuffer* vertexBuffers[1] = {
        g_RenderState.lightDrawInfoBuffer
    };
    SDL_GPUBuffer* fragmentBuffers[4] = {
        g_RenderState.lightBuffer,
        g_RenderState.lightDrawInfoBuffer,
        g_RenderState.pointShadowMatrixBuffer,
        g_RenderState.spotShadowMatrixBuffer
    };
    SDL_BindGPUVertexStorageBuffers(pass, 0, vertexBuffers, SDL_arraysize(vertexBuffers));
    SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));

    SDL_GPUTextureSamplerBinding inputs[7] = {
        { .texture = winstate->tex_gbuffer_tangent, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_gbuffer_albedo_metallic, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_gbuffer_shadow_roughness, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hiz_depth, .sampler = g_RenderState.hiZSampler },
        { .texture = winstate->tex_hbao_blur, .sampler = g_RenderState.sampler },
        { .texture = winstate->tex_point_shadow_color, .sampler = g_RenderState.shadowSampler },
        { .texture = winstate->tex_spot_shadow_color, .sampler = g_RenderState.shadowSampler }
    };
    SDL_BindGPUFragmentSamplers(pass, 0, inputs, SDL_arraysize(inputs));

    struct {
        u32 outputSize[2];
        f32 padding0[2];
        mat4x4 invViewProj;
        f32 cameraPosition[4];
    } params = {0};
    params.outputSize[0] = width;
    params.outputSize[1] = height;
    params.invViewProj = M44Inverse(viewProj);
    params.cameraPosition[0] = g_Camera.position.x;
    params.cameraPosition[1] = g_Camera.position.y;
    params.cameraPosition[2] = g_Camera.position.z;
    SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.lightDrawArgsBuffer, 0, 1);
    SDL_EndGPURenderPass(pass);
}

void RenderLines(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj)
{
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, depthTarget);
    SDL_GPUBufferBinding vertex_binding = { g_RenderState.lineBuffer, 0 };
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.linePipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.lineDrawArgsBuffer, sizeof(int) * 4, 1);
    SDL_EndGPURenderPass(pass);
}
