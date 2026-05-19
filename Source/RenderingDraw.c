#include "RenderingInternal.h"

static float3 GetSunLightDir(void)
{
    return F3NormSafe((float3){ -0.5f, 0.5f, 0.0f });
}

mat4x4 GetShadowViewProj(void)
{
    float3 lightDir = GetSunLightDir();
    float3 center = g_Camera.position;
    float3 eye = F3Add(center, F3MulF(lightDir, SHADOW_CAMERA_DISTANCE));
    float3 viewDir = F3MulF(lightDir, -1.0f);
    mat4x4 view = M44LookAtRH(eye, viewDir, (float3){ 0.0f, 1.0f, 0.0f });
    mat4x4 proj = M44OrthoRH(-SHADOW_ORTHO_SIZE, SHADOW_ORTHO_SIZE,
                             -SHADOW_ORTHO_SIZE, SHADOW_ORTHO_SIZE,
                              SHADOW_NEAR_PLANE, SHADOW_FAR_PLANE);
    return M44Multiply(view, proj);
}

void RenderDepthPrepass(SDL_GPUCommandBuffer* cmd,
                        SDL_GPUColorTargetInfo* color_target,
                        SDL_GPUDepthStencilTargetInfo* depth_target,
                        mat4x4 viewProj,
                        SDL_GPUGraphicsPipeline* skinnedPipeline,
                        SDL_GPUGraphicsPipeline* surfacePipeline)
{
    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, color_target, color_target ? 1 : 0, depth_target);

    if (skinnedSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, skinnedPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUBuffer* buffers[4] = {
            g_RenderState.skinnedBuffers.entity,
            g_RenderState.skinnedBuffers.primitiveGroup,
            g_RenderState.skinnedBuffers.drawSparseIndices,
            g_RenderState.skinnedAnimatedVertices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
        SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);
    }

    if (surfaceSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, surfacePipeline);
        vertex_binding.buffer = g_RenderState.surfaceVertexBuffer;
        vertex_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUBuffer* surfaceBuffers[3] = {
            g_RenderState.surfaceBuffers.entity,
            g_RenderState.surfaceBuffers.primitiveGroup,
            g_RenderState.surfaceBuffers.drawSparseIndices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, SDL_arraysize(surfaceBuffers));
        SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }

    SDL_EndGPURenderPass(pass);
}

void RenderScene(SDL_GPUCommandBuffer* cmd,
                 SDL_GPUColorTargetInfo* color_target,
                 SDL_GPUDepthStencilTargetInfo* depth_target,
                 mat4x4 viewProj)
{
    struct {
        mat4x4 viewProj;
        mat4x4 lightViewProj;
        float cameraPosition[4];
    } shaderParams;
    if (skinnedSet.numGroups + surfaceSet.numGroups <= 0)
    {
        AX_WARN("nothing to render");
        return;
    }
    shaderParams.viewProj = viewProj;
    shaderParams.lightViewProj = GetShadowViewProj();
    shaderParams.cameraPosition[0] = g_Camera.position.x;
    shaderParams.cameraPosition[1] = g_Camera.position.y;
    shaderParams.cameraPosition[2] = g_Camera.position.z;
    shaderParams.cameraPosition[3] = 0.0f;

    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };
    SDL_GPUTextureSamplerBinding pageSamplers[4] = {
        { .texture = g_RenderState.albedoPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.normalPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.metallicRoughnessPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_WindowState.tex_shadow_depth, .sampler = g_RenderState.shadowSampler }
    };
    SDL_GPUBuffer* fragmentBuffers[2] = {
        g_RenderState.materialBuffer,
        g_RenderState.textureDescriptorBuffer
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, color_target, 1, depth_target);
    if (skinnedSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.skinnedPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUBuffer* buffers[4] = {
            g_RenderState.skinnedBuffers.entity,
            g_RenderState.skinnedBuffers.primitiveGroup,
            g_RenderState.skinnedBuffers.drawSparseIndices,
            g_RenderState.skinnedAnimatedVertices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));

        SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));

        SDL_PushGPUVertexUniformData(cmd, 0, &shaderParams, sizeof(shaderParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);
    }

    if (surfaceSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.surfacePipeline);
        vertex_binding.buffer = g_RenderState.surfaceVertexBuffer;
        vertex_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUBuffer* surfaceBuffers[3] = {
            g_RenderState.surfaceBuffers.entity,
            g_RenderState.surfaceBuffers.primitiveGroup,
            g_RenderState.surfaceBuffers.drawSparseIndices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, SDL_arraysize(surfaceBuffers));
        SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
        SDL_PushGPUVertexUniformData(cmd, 0, &shaderParams, sizeof(shaderParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }

    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.linePipeline);
    vertex_binding.buffer = g_RenderState.lineBuffer;
    vertex_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.lineDrawArgsBuffer, sizeof(int) * 4, 1);

    SDL_EndGPURenderPass(pass);
}
