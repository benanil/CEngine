#include "RenderingInternal.h"

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

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx)
{
    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };
    SDL_GPUTextureSamplerBinding albedoSampler = { .texture = g_RenderState.albedoPages.handle, .sampler = g_RenderState.sampler };
    SDL_GPUBuffer* fragmentBuffers[2] = {
        g_RenderState.materialBuffer,
        g_RenderState.textureDescriptorBuffer
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTarget, ctx->colorTarget ? 1 : 0, ctx->depthTarget);
    if (ctx->viewport) SDL_SetGPUViewport(pass, ctx->viewport);
    if (ctx->scissor) SDL_SetGPUScissor(pass, ctx->scissor);

    if (skinnedSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, ctx->skinnedPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUBuffer* buffers[5] = {
            g_RenderState.skinnedBuffers.entity,
            g_RenderState.skinnedBuffers.primitiveGroup,
            g_RenderState.skinnedBuffers.drawSparseIndices,
            g_RenderState.skinnedAnimatedVertices,
            g_RenderState.shadowCascadeBuffer
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, ctx->useShadowCascades ? SDL_arraysize(buffers) : 4);
        if (ctx->alphaClip)
        {
            SDL_BindGPUFragmentSamplers(pass, 0, &albedoSampler, 1);
            SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
        }
        if (ctx->useShadowCascades) SDL_PushGPUVertexUniformData(cmd, 0, &ctx->cascadeIndex, sizeof(u32));
        else SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);
    }

    if (surfaceSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, ctx->surfacePipeline);
        vertex_binding.buffer = g_RenderState.surfaceVertexBuffer;
        vertex_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUBuffer* surfaceBuffers[4] = {
            g_RenderState.surfaceBuffers.entity,
            g_RenderState.surfaceBuffers.primitiveGroup,
            g_RenderState.surfaceBuffers.drawSparseIndices,
            g_RenderState.shadowCascadeBuffer
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, ctx->useShadowCascades ? SDL_arraysize(surfaceBuffers) : 3);
        if (ctx->alphaClip)
        {
            SDL_BindGPUFragmentSamplers(pass, 0, &albedoSampler, 1);
            SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
        }
        if (ctx->useShadowCascades) SDL_PushGPUVertexUniformData(cmd, 0, &ctx->cascadeIndex, sizeof(u32));
        else SDL_PushGPUVertexUniformData(cmd, 0, &ctx->viewProj, sizeof(mat4x4));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
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
    if (skinnedSet.numGroups + surfaceSet.numGroups <= 0)
    {
        AX_WARN("nothing to render");
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

    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer, 0 };
    float3 sunDirection = GetRenderSunDirection();
    SDL_GPUTextureSamplerBinding pageSamplers[4] = {
        { .texture = g_RenderState.albedoPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.normalPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_RenderState.metallicRoughnessPages.handle, .sampler = g_RenderState.sampler },
        { .texture = g_WindowState.tex_shadow_color, .sampler = g_RenderState.shadowSampler }
    };
    struct { f32 viewportSize[4]; f32 sunDirection[4]; } fragmentParams = {
        { (f32)Maxs32(g_Camera.viewportSize.x, 1), (f32)Maxs32(g_Camera.viewportSize.y, 1), 0.0f, 0.0f },
        { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f }
    };
    SDL_GPUBuffer* fragmentBuffers[2] = {
        g_RenderState.materialBuffer,
        g_RenderState.textureDescriptorBuffer
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, ctx->colorTargets, ctx->numColorTargets, ctx->depthTarget);
    if (skinnedSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.skinnedPipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUBuffer* buffers[5] = {
            g_RenderState.skinnedBuffers.entity,
            g_RenderState.skinnedBuffers.primitiveGroup,
            g_RenderState.skinnedBuffers.drawSparseIndices,
            g_RenderState.skinnedAnimatedVertices,
            g_RenderState.shadowCascadeBuffer
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));

        SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));

        SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
        SDL_PushGPUFragmentUniformData(cmd, 0, &fragmentParams, sizeof(fragmentParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);
    }

    if (surfaceSet.numGroups > 0)
    {
        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.surfacePipeline);
        vertex_binding.buffer = g_RenderState.surfaceVertexBuffer;
        vertex_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUBuffer* surfaceBuffers[4] = {
            g_RenderState.surfaceBuffers.entity,
            g_RenderState.surfaceBuffers.primitiveGroup,
            g_RenderState.surfaceBuffers.drawSparseIndices,
            g_RenderState.shadowCascadeBuffer
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, SDL_arraysize(surfaceBuffers));
        SDL_BindGPUFragmentSamplers(pass, 0, pageSamplers, SDL_arraysize(pageSamplers));
        SDL_BindGPUFragmentStorageBuffers(pass, 0, fragmentBuffers, SDL_arraysize(fragmentBuffers));
        SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
        SDL_PushGPUFragmentUniformData(cmd, 0, &fragmentParams, sizeof(fragmentParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }

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
