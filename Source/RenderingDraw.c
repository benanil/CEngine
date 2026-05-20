#include "RenderingInternal.h"

static float3 GetSunLightDir(void)
{
    return F3Norm((float3){ -0.33f, 0.66f, 0.0f });
}

static inline v128f VCALL TransformPoint(mat4x4 m, v128f p)
{
    return Vec3Transform(p, m.r);
}

static inline v128f VCALL AddScaled(v128f a, v128f b, float scale)
{
    return VecFmadd(b, VecSet1(scale), a);
}

static float CascadeSplitDistance(float nearClip, float farClip, float t)
{
    float logSplit = nearClip * Powf(farClip / nearClip, t);
    float uniformSplit = nearClip + (farClip - nearClip) * t;
    return logSplit * SHADOW_CASCADE_LAMBDA + uniformSplit * (1.0f - SHADOW_CASCADE_LAMBDA);
}

static u32 GetShadowCascadeMapSize(u32 cascade)
{
    (void)cascade;
    return SHADOW_MAP_SIZE;
}

ShadowCascadeData GetShadowCascades(void)
{
    ShadowCascadeData result;
    float shadowNear = Maxf32(g_Camera.nearClip, 0.01f);
    float shadowFar  = Minf32(g_Camera.farClip, SHADOW_MAX_DISTANCE);
    float aspect     = (float)Maxi32(g_Camera.viewportSize.x, 1) / (float)Maxi32(g_Camera.viewportSize.y, 1);
    float tanHalfFov = Tan(g_Camera.verticalFOV * MATH_DegToRad * 0.5f);

    float3 sunLightDir = GetSunLightDir();
    v128f lightDir       = Vec3Load(&sunLightDir.x);
    v128f lightViewDir   = VecMul(lightDir, VecSet1(-1.0f));
    v128f cameraPosition = Vec3Load(&g_Camera.position.x);
    v128f cameraFront    = Vec3Load(&g_Camera.Front.x);
    v128f cameraRight    = Vec3Load(&g_Camera.Right.x);
    v128f cameraUp       = Vec3Load(&g_Camera.Up.x);
    float previousSplit = shadowNear;

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        float t = (float)(cascade + 1u) / (float)SHADOW_CASCADE_COUNT;
        float split = CascadeSplitDistance(shadowNear, shadowFar, t);
        result.splitDistances[cascade] = split;

        float sliceOverlap = Maxf32((split - previousSplit) * SHADOW_CASCADE_OVERLAP, 0.05f);
        float nearDist = Maxf32(shadowNear, previousSplit - sliceOverlap);
        float farDist  = Minf32(shadowFar, split + sliceOverlap);
        previousSplit = split;

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

        v128f center = VecZero();
        for (u32 i = 0; i < 8u; i++) center = VecAdd(center, corners[i]);
        center = VecMul(center, VecSet1(1.0f / 8.0f));

        float radius = 0.0f;
        for (u32 i = 0; i < 8u; i++)
        {
            float dist = Vec3LenfV(VecSub(corners[i], center));
            radius = Maxf32(radius, dist);
        }
        radius = Maxf32(radius, 1.0f);

        float eyeDistance = radius + SHADOW_CAMERA_DISTANCE;
        v128f eye = AddScaled(center, lightDir, eyeDistance);
        mat4x4 view = M44LookAtRHVec(eye, lightViewDir, VecSetR(0.0f, 1.0f, 0.0f, 0.0f));

        v128f minLight = VecSet1(FLT_MAX);
        v128f maxLight = VecSet1(-FLT_MAX);
        for (u32 i = 0; i < 8u; i++)
        {
            v128f lightPos = TransformPoint(view, corners[i]);
            minLight = VecMin(minLight, lightPos);
            maxLight = VecMax(maxLight, lightPos);
        }

        float extent = Ceilf(radius * 2.0f);
        float halfExtent = extent * 0.5f;
        v128f centerV = VecMulf(VecAdd(minLight, maxLight), 0.5f);
        v128f texelSize = VecSet1(extent / (float)GetShadowCascadeMapSize(cascade));
        centerV = VecMul(VecFloor(VecDiv(centerV, texelSize)), texelSize);
        float centerX = VecGetX(centerV);
        float centerY = VecGetY(centerV);

        mat4x4 proj = M44OrthoRH(centerX - halfExtent, centerX + halfExtent,
                                 centerY - halfExtent, centerY + halfExtent,
                                 SHADOW_NEAR_PLANE, eyeDistance + radius);
        result.lightViewProj[cascade] = M44Multiply(view, proj);
    }

    return result;
}

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx)
{
    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTarget, ctx->colorTarget ? 1 : 0, ctx->depthTarget);
    if (ctx->viewport) SDL_SetGPUViewport(pass, ctx->viewport);
    if (ctx->scissor) SDL_SetGPUScissor(pass, ctx->scissor);

    if (skinnedSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, ctx->skinnedPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUBuffer* buffers[4] = {
            g_RenderState.skinnedBuffers.entity,
            g_RenderState.skinnedBuffers.primitiveGroup,
            g_RenderState.skinnedBuffers.drawSparseIndices,
            g_RenderState.skinnedAnimatedVertices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
        SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);
    }

    if (surfaceSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, ctx->surfacePipeline);
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
        SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }

    SDL_EndGPURenderPass(pass);
}

void RenderScene(SDL_GPUCommandBuffer* cmd, const ScenePassContext* ctx)
{
    struct {
        mat4x4 viewProj;
        mat4x4 lightViewProj[SHADOW_CASCADE_COUNT];
        float cameraPosition[4];
        float cameraForward[4];
        float cascadeSplits[4];
    } vertexParams;
    if (skinnedSet.numGroups + surfaceSet.numGroups <= 0)
    {
        AX_WARN("nothing to render");
        return;
    }
    vertexParams.viewProj = ctx->viewProj;
    for (u32 i = 0; i < SHADOW_CASCADE_COUNT; i++)
    {
        vertexParams.lightViewProj[i] = ctx->shadowCascades.lightViewProj[i];
        vertexParams.cascadeSplits[i] = ctx->shadowCascades.splitDistances[i];
    }
    vertexParams.cameraPosition[0] = g_Camera.position.x;
    vertexParams.cameraPosition[1] = g_Camera.position.y;
    vertexParams.cameraPosition[2] = g_Camera.position.z;
    vertexParams.cameraPosition[3] = 0.0f;
    vertexParams.cameraForward[0] = g_Camera.Front.x;
    vertexParams.cameraForward[1] = g_Camera.Front.y;
    vertexParams.cameraForward[2] = g_Camera.Front.z;
    vertexParams.cameraForward[3] = 0.0f;
    vertexParams.cascadeSplits[3] = ctx->shadowCascades.splitDistances[SHADOW_CASCADE_COUNT - 1u];

    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };
    SDL_GPUTextureSamplerBinding pageSamplers[4] = {
        { .texture = g_RenderState.albedoPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.normalPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.metallicRoughnessPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_WindowState.tex_shadow_color, .sampler = g_RenderState.shadowSampler }
    };
    SDL_GPUBuffer* fragmentBuffers[2] = {
        g_RenderState.materialBuffer,
        g_RenderState.textureDescriptorBuffer
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTarget, 1, ctx->depthTarget);
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

        SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
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
        SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }

    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.linePipeline);
    vertex_binding.buffer = g_RenderState.lineBuffer;
    vertex_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(ctx->viewProj));
    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.lineDrawArgsBuffer, sizeof(int) * 4, 1);

    SDL_EndGPURenderPass(pass);
}
