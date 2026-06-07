#include "RenderingInternal.h"

void InitShadows()
{

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

void DestroyShadows()
{

}
