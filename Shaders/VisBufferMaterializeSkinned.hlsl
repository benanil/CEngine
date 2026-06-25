// Visibility-buffer materialize for the SKINNED set (Phase A).
//
// Mirrors VisBufferMaterialize.hlsl but reconstructs skinned triangles: positions come from the
// animated vertex buffer and the tangent frame is re-skinned from the source vertex + bone
// matrices (mirrors Skinned.hlsl vert). Runs full-screen and early-outs on pixels that aren't
// skinned (sentinel or surface), which the surface materialize handles.
//
// NOTE: keep shading (material fetch -> gbuffer pack) in sync with Surface.hlsl frag() and the
// skinned transform in sync with Skinned.hlsl vert().
#include "../Include/RenderLimits.h"
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "PBR.hlsl"
#include "AnimatedTransform.hlsl"
#include "Shadow/Shadow.hlsl"

cbuffer params : register(b0, space2)
{
    uint2  outputSize;
    float2 padding0;
    float4x4 uViewProj;
    float4 cameraPosition;
    float4 cameraForward;
    float4 sunDirection;
};

// matches the CPU ASkinedVertex layout (24 bytes)
struct SkinnedVertex
{
    uint positionXY; // f16_2 (unused: position comes from the animated buffer)
    uint positionZW; // f16_2
    uint octTbn;
    uint texCoord;   // half2
    uint joints;     // 4x uint8
    uint weights;    // XY11Z10 unorm
};

// --- sampled textures (space0) + samplers ---
Texture2DArray<float4> AlbedoPages            : register(t0, space0);
Texture2DArray<float2> NormalPages            : register(t1, space0);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space0);
Texture2D<float>       ShadowMap              : register(t3, space0);
Texture2D<float>       HiZDepth               : register(t4, space0);
SamplerState           Sampler                : register(s0, space0);
SamplerState           ShadowSampler          : register(s3, space0);
// --- read-only storage textures ---
Texture2D<uint2>       VisBuffer              : register(t5, space0);
// --- read-only storage buffers ---
StructuredBuffer<Entity>             sEntities           : register(t6, space0);
StructuredBuffer<PrimitiveGroup>     sPrimitiveGroups    : register(t7, space0);
StructuredBuffer<uint>               sDrawSparseIndices  : register(t8, space0);
StructuredBuffer<uint>               sIndexBuffer        : register(t9, space0);
StructuredBuffer<SkinnedVertex>      sSkinnedVertex      : register(t10, space0);
StructuredBuffer<MaterialGPU>        sMaterials          : register(t11, space0);
StructuredBuffer<TextureDescriptor>  sTextureDescriptors : register(t12, space0);
StructuredBuffer<ShadowCascadeBuffer> sShadowCascades    : register(t13, space0);
// --- read-write storage textures = the gbuffer ---
[[vk::image_format("r32ui")]] RWTexture2D<uint>   OutTangentFrame    : register(u0, space1);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutAlbedoMetallic  : register(u1, space1);
[[vk::image_format("rg8")]]   RWTexture2D<float2> OutShadowRoughness : register(u2, space1);
// SDL caps compute readonly storage buffers at 8, so the two extra skinned inputs are bound as
// read-write storage buffers (read-only in the shader). animatedVertices/boneBuffer carry compute
// write usage, so this binding is valid.
RWStructuredBuffer<AnimatedVert> sAnimatedVert : register(u3, space1);
RWStructuredBuffer<uint>         sBoneMtx      : register(u4, space1);

f16_3x4 LoadBoneRW(uint idx)
{
    uint base = idx * MatrixNumInt32;
    f16_3x4 bone;
    bone[0] = f16_4(UnpackHalf2(sBoneMtx[base + 0]), UnpackHalf2(sBoneMtx[base + 1]));
    bone[1] = f16_4(UnpackHalf2(sBoneMtx[base + 2]), UnpackHalf2(sBoneMtx[base + 3]));
    bone[2] = f16_4(UnpackHalf2(sBoneMtx[base + 4]), UnpackHalf2(sBoneMtx[base + 5]));
    return bone;
}

// reconstruct one skinned vertex: world position (animated) + re-skinned tangent frame.
void SkinnedVertexData(uint vtxIndex, PrimitiveGroup group, uint lod, uint sparse,
                       f16_4 insRot, f16_3 insScale, float3 entityPos,
                       out float3 worldPos, out float3 N, out float3 T, out float3 B,
                       out float2 uv, out float handedness)
{
    SkinnedVertex sv = sSkinnedVertex[vtxIndex];
    uint localVertex = vtxIndex - group.lodVertexOffset[lod];
    uint animatedVertex = sparse * uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) + group.lodAnimatedVertexOffset[lod] + localVertex;
    AnimatedVert animated = sAnimatedVert[animatedVertex];
    float3 modelPos = UnpackAnimatedModelPos(uint2(animated.packed0, animated.packed1), group.aabbMin.xyz, group.aabbMax.xyz);
    worldPos = AnimatedWorldPos(modelPos, float4(insRot), float3(insScale), entityPos);

    // re-skin tangent frame from bones (mirrors Skinned.vert)
    uint boneStart = sparse * MAX_BONES;
    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(sv.weights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);
    uint4 joints = uint4(sv.joints & 0xFFu, (sv.joints >> 8) & 0xFFu, (sv.joints >> 16) & 0xFFu, (sv.joints >> 24) & 0xFFu);

    f16_3x4 animMat = (f16_3x4)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_3x4 bone = LoadBoneRW(boneStart + joints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
    }

    f16_3 restNormal, restTangent;
    UnpackNormalTangent(sv.octTbn, restNormal, restTangent);
    f16 hand = UnpackTangentHandedness(sv.octTbn);
    f16_3 nn = normalize(QMulVec3(insRot, mul(animMat, f16_4(restNormal,  f16(0.0)))));
    f16_3 tt = normalize(Orthonormalize(QMulVec3(insRot, mul(animMat, f16_4(restTangent, f16(0.0)))), nn));
    f16_3 bb = cross(nn, tt) * hand;
    N = normalize(float3(nn));
    T = float3(tt);
    B = float3(bb);
    uv = float2(UnpackHalf2(sv.texCoord));
    handedness = float(hand);
}

float3 PerspBary(float2 s0, float2 s1, float2 s2, float3 invW, float2 p)
{
    float2 e0 = s1 - s0;
    float2 e1 = s2 - s0;
    float2 ep = p  - s0;
    float det = e0.x * e1.y - e0.y * e1.x;
    float invDet = abs(det) > 1e-20f ? (1.0f / det) : 0.0f;
    float b1 = (ep.x * e1.y - ep.y * e1.x) * invDet;
    float b2 = (e0.x * ep.y - e0.y * ep.x) * invDet;
    float b0 = 1.0f - b1 - b2;
    float3 pc = float3(b0, b1, b2) * invW;
    float  sum = pc.x + pc.y + pc.z;
    return pc / (abs(sum) > 1e-20f ? sum : 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;
    uint2 pixel = tid.xy;

    float depth = HiZDepth.Load(int3(pixel, 0));
    if (depth >= 0.9999f) return;

    uint2 ids = VisBuffer.Load(int3(pixel, 0));
    if ((ids.x >> 16) == 0xFFFFu) return;     // sentinel (terrain/sky)
    if ((ids.y & 0x80u) == 0u) return;        // not skinned: surface materialize handles it

    uint primitiveIdx = ids.x >> 16;
    uint instanceID   = ids.x & 0xFFFFu;
    uint triangleID   = ids.y >> 8;
    uint lod          = ids.y & 0x03u;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    uint denseIdx = sDrawSparseIndices[lod * uint(MAX_ANIM_INSTANCES) + group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];
    uint sparse = entity.sparse;

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackRGBA16Unorm(entity.scale).xyz * f16(10.0);

    uint triBase = group.lodIndexOffset[lod] + triangleID * 3u;
    uint i0 = sIndexBuffer[triBase + 0u];
    uint i1 = sIndexBuffer[triBase + 1u];
    uint i2 = sIndexBuffer[triBase + 2u];

    float3 wp0, wp1, wp2, N0, T0, B0, N1, T1, B1, N2, T2, B2;
    float2 uv0, uv1, uv2;
    float  hnd0, hnd1, hnd2;
    SkinnedVertexData(i0, group, lod, sparse, insRot, insScale, entity.position.xyz, wp0, N0, T0, B0, uv0, hnd0);
    SkinnedVertexData(i1, group, lod, sparse, insRot, insScale, entity.position.xyz, wp1, N1, T1, B1, uv1, hnd1);
    SkinnedVertexData(i2, group, lod, sparse, insRot, insScale, entity.position.xyz, wp2, N2, T2, B2, uv2, hnd2);
    float handedness = hnd0; // nointerpolation = first/provoking vertex (i0)

    float4 c0 = mul(uViewProj, float4(wp0, 1.0));
    float4 c1 = mul(uViewProj, float4(wp1, 1.0));
    float4 c2 = mul(uViewProj, float4(wp2, 1.0));
    float3 invW = float3(1.0 / c0.w, 1.0 / c1.w, 1.0 / c2.w);
    float2 s0 = c0.xy * invW.x;
    float2 s1 = c1.xy * invW.y;
    float2 s2 = c2.xy * invW.z;

    float2 invSize  = 1.0 / float2(outputSize);
    float2 uvCenter = (float2(pixel) + 0.5) * invSize;
    float2 ndcC = float2(uvCenter.x * 2.0 - 1.0,               1.0 - uvCenter.y * 2.0);
    float2 ndcX = float2((uvCenter.x + invSize.x) * 2.0 - 1.0, 1.0 - uvCenter.y * 2.0);
    float2 ndcY = float2(uvCenter.x * 2.0 - 1.0,               1.0 - (uvCenter.y + invSize.y) * 2.0);

    float3 bC = PerspBary(s0, s1, s2, invW, ndcC);
    float3 bX = PerspBary(s0, s1, s2, invW, ndcX);
    float3 bY = PerspBary(s0, s1, s2, invW, ndcY);

    float2 uv    = bC.x * uv0 + bC.y * uv1 + bC.z * uv2;
    float2 duvdx = (bX.x * uv0 + bX.y * uv1 + bX.z * uv2) - uv;
    float2 duvdy = (bY.x * uv0 + bY.y * uv1 + bY.z * uv2) - uv;

    float3 normalI    = bC.x * N0 + bC.y * N1 + bC.z * N2;
    float3 tangentI   = bC.x * T0 + bC.y * T1 + bC.z * T2;
    float3 bitangentI = bC.x * B0 + bC.y * B1 + bC.z * B2;
    float3 worldPos   = bC.x * wp0 + bC.y * wp1 + bC.z * wp2;

    // ----- shading, mirroring Surface.hlsl frag() -----
    MaterialGPU material = sMaterials[group.materialIndex];
    TextureDescriptor albedoD = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalD = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrD     = sTextureDescriptors[material.metallicRoughnessDescriptor];

    f16_4 albedoSample = SampleTexturePageRGBAGrad(AlbedoPages, Sampler, albedoD, uv, duvdx, duvdy);
    f16_4 baseFactor   = UnpackColor4UintF16(material.baseColorFactor);
    f16_3 baseColor = SRGBToLinear(albedoSample.rgb) * f16_3(baseFactor.rgb);

    float3 tangentNormal = DecodeNormalRG(float2(SampleTexturePageRGGrad(NormalPages, Sampler, normalD, uv, duvdx, duvdy)));
    f16_2  mr = SampleTexturePageRGGrad(MetallicRoughnessPages, Sampler, mrD, uv, duvdx, duvdy);

    float3 N = normalize(tangentNormal.x * normalize(tangentI) +
                         tangentNormal.y * normalize(bitangentI) +
                         tangentNormal.z * normalize(normalI));

    float metallicFactor  = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic  = float(mr.x) * metallicFactor;
    float roughness = float(mr.y) * roughnessFactor;

    float viewDepth = dot(worldPos - cameraPosition.xyz, cameraForward.xyz);
    ShadowCascadeBuffer cascades = sShadowCascades[0];
    float3 cascadeSplits = cascades.splitDistances.xyz;
    uint cascadeIndex = viewDepth > cascadeSplits.x ? 1u : 0u;
    cascadeIndex = viewDepth > cascadeSplits.y ? 2u : cascadeIndex;
    float4 shadowPos = MulShadowCascade(cascades, cascadeIndex, float4(worldPos, 1.0));
    float  shadow = SampleShadow(ShadowMap, ShadowSampler, shadowPos, cascadeIndex, N, sunDirection.xyz);

    roughness = SpecularAntiAliasing(roughness, float3(0, 0, 0), float3(0, 0, 0));

    float3 T = normalize(tangentI - dot(tangentI, N) * N);

    OutTangentFrame[pixel]    = PackNormalTangent(f16_3(N), f16_4(f16_3(T), f16(handedness)));
    OutAlbedoMetallic[pixel]  = float4(float3(baseColor), saturate(metallic));
    OutShadowRoughness[pixel] = float2(saturate(shadow), saturate(roughness));
}
