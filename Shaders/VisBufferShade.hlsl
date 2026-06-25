// Surface visibility-buffer shade pass (Phase B transition).
// Reconstructs surface attributes from the visibility buffer and writes final lit color directly.
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "PBR.hlsl"
#include "CommonStructs.hlsl"
#include "Shadow/Shadow.hlsl"

cbuffer params : register(b0, space2)
{
    uint2  outputSize;
    float2 padding0;
    float4x4 uViewProj;
    float4 cameraPosition;
    float4 cameraForward;
    float4 sunDirection;
    uint2 tileCount;
    uint enableLocalLights;
    uint padding1;
};

struct SurfaceVertex
{
    uint2 position;
    uint  octTbn;
    uint  texCoord;
};

Texture2DArray<float4> AlbedoPages            : register(t0, space0);
Texture2DArray<float2> NormalPages            : register(t1, space0);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space0);
Texture2D<float>       ShadowMap              : register(t3, space0);
Texture2D<float>       HiZDepth               : register(t4, space0);
Texture2D<float>       AmbientOcclusion       : register(t5, space0);
Texture2DArray<float>  PointShadowTexture     : register(t6, space0);
Texture2DArray<float>  SpotShadowTexture      : register(t7, space0);
SamplerState           Sampler                : register(s0, space0);
SamplerState           ShadowSampler          : register(s3, space0);
SamplerState           PointShadowSampler     : register(s6, space0);
SamplerState           SpotShadowSampler      : register(s7, space0);

Texture2D<uint2>       VisBuffer              : register(t8, space0);
StructuredBuffer<Entity>              sEntities           : register(t9, space0);
StructuredBuffer<PrimitiveGroup>      sPrimitiveGroups    : register(t10, space0);
StructuredBuffer<SurfaceVertex>       sVertexBuffer       : register(t11, space0);
StructuredBuffer<uint>                sIndexBuffer        : register(t12, space0);
StructuredBuffer<MaterialGPU>         sMaterials          : register(t13, space0);
StructuredBuffer<TextureDescriptor>   sTextureDescriptors : register(t14, space0);
StructuredBuffer<ShadowCascadeBuffer> sShadowCascades     : register(t15, space0);

[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutputTexture : register(u0, space1);
// SDL caps compute readonly storage buffers at 8, so local-light inputs are bound as
// read-write storage buffers. They are read-only in this shader.
RWStructuredBuffer<LightGPU>          lights              : register(u1, space1);
RWStructuredBuffer<PointShadowMatrix> PointShadowMatrices : register(u2, space1);
RWStructuredBuffer<PointShadowMatrix> SpotShadowMatrices  : register(u3, space1);
RWStructuredBuffer<uint>              tileCounts          : register(u4, space1);
RWStructuredBuffer<uint>              tileLightIndices    : register(u5, space1);

#include "LocalLights.hlsl"

float3 SurfaceWorldPos(SurfaceVertex v, PrimitiveGroup group, f16_4 insRot, f16_3 insScale, float3 entityPos)
{
    float3 localPos = group.aabbMin.xyz + UnpackUnorm16x4(v.position).xyz * (group.aabbMax.xyz - group.aabbMin.xyz);
    f16_3  worldPos = QMulVec3(insRot, f16_3(localPos) * insScale);
    return float3(worldPos) + entityPos;
}

void SurfaceTBN(uint octTbn, f16_4 insRot, out float3 N, out float3 T, out float3 B)
{
    f16_3 rn, rt;
    UnpackNormalTangent(octTbn, rn, rt);
    f16   h  = UnpackTangentHandedness(octTbn);
    f16_3 nn = QMulVec3(insRot, rn);
    f16_3 tt = QMulVec3(insRot, rt);
    tt = normalize(Orthonormalize(tt, nn));
    f16_3 bb = normalize(cross(nn, tt)) * h;
    N = normalize(float3(nn));
    T = float3(tt);
    B = float3(bb);
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
    float sum = pc.x + pc.y + pc.z;
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
    if (ids.x == 0xFFFFFFFFu) return;
    if ((ids.y & 0x80u) != 0u) return;

    uint denseIdx     = ids.x;
    uint triangleID   = ids.y >> 8;
    uint lod          = ids.y & 0x03u;
    Entity entity = sEntities[denseIdx];
    uint primitiveIdx = entity.primitiveIdx;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackRGBA16Unorm(entity.scale).xyz * f16(10.0);

    uint triBase = group.lodIndexOffset[lod] + triangleID * 3u;
    SurfaceVertex v0 = sVertexBuffer[sIndexBuffer[triBase + 0u]];
    SurfaceVertex v1 = sVertexBuffer[sIndexBuffer[triBase + 1u]];
    SurfaceVertex v2 = sVertexBuffer[sIndexBuffer[triBase + 2u]];

    float3 wp0 = SurfaceWorldPos(v0, group, insRot, insScale, entity.position.xyz);
    float3 wp1 = SurfaceWorldPos(v1, group, insRot, insScale, entity.position.xyz);
    float3 wp2 = SurfaceWorldPos(v2, group, insRot, insScale, entity.position.xyz);

    float4 c0 = mul(uViewProj, float4(wp0, 1.0));
    float4 c1 = mul(uViewProj, float4(wp1, 1.0));
    float4 c2 = mul(uViewProj, float4(wp2, 1.0));
    float3 invW = float3(1.0 / c0.w, 1.0 / c1.w, 1.0 / c2.w);
    float2 s0 = c0.xy * invW.x;
    float2 s1 = c1.xy * invW.y;
    float2 s2 = c2.xy * invW.z;

    float2 invSize = 1.0 / float2(outputSize);
    float2 uvCenter = (float2(pixel) + 0.5) * invSize;
    float2 ndcC = float2(uvCenter.x * 2.0 - 1.0, 1.0 - uvCenter.y * 2.0);
    float2 ndcX = float2((uvCenter.x + invSize.x) * 2.0 - 1.0, 1.0 - uvCenter.y * 2.0);
    float2 ndcY = float2(uvCenter.x * 2.0 - 1.0, 1.0 - (uvCenter.y + invSize.y) * 2.0);

    float3 bC = PerspBary(s0, s1, s2, invW, ndcC);
    float3 bX = PerspBary(s0, s1, s2, invW, ndcX);
    float3 bY = PerspBary(s0, s1, s2, invW, ndcY);

    float2 t0 = float2(UnpackHalf2(v0.texCoord));
    float2 t1 = float2(UnpackHalf2(v1.texCoord));
    float2 t2 = float2(UnpackHalf2(v2.texCoord));
    float2 uv = bC.x * t0 + bC.y * t1 + bC.z * t2;
    float2 duvdx = (bX.x * t0 + bX.y * t1 + bX.z * t2) - uv;
    float2 duvdy = (bY.x * t0 + bY.y * t1 + bY.z * t2) - uv;

    float3 N0, T0, B0, N1, T1, B1, N2, T2, B2;
    SurfaceTBN(v0.octTbn, insRot, N0, T0, B0);
    SurfaceTBN(v1.octTbn, insRot, N1, T1, B1);
    SurfaceTBN(v2.octTbn, insRot, N2, T2, B2);
    float3 normalI = bC.x * N0 + bC.y * N1 + bC.z * N2;
    float3 tangentI = bC.x * T0 + bC.y * T1 + bC.z * T2;
    float3 bitangentI = bC.x * B0 + bC.y * B1 + bC.z * B2;
    float3 worldPos = bC.x * wp0 + bC.y * wp1 + bC.z * wp2;

    MaterialGPU material = sMaterials[group.materialIndex];
    TextureDescriptor albedoD = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalD = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrD = sTextureDescriptors[material.metallicRoughnessDescriptor];

    f16_4 albedoSample = SampleTexturePageRGBAGrad(AlbedoPages, Sampler, albedoD, uv, duvdx, duvdy);
    f16_4 baseFactor = UnpackColor4UintF16(material.baseColorFactor);
    float3 baseColor = float3(SRGBToLinear(albedoSample.rgb) * f16_3(baseFactor.rgb));
    float3 tangentNormal = DecodeNormalRG(float2(SampleTexturePageRGGrad(NormalPages, Sampler, normalD, uv, duvdx, duvdy)));
    f16_2 mr = SampleTexturePageRGGrad(MetallicRoughnessPages, Sampler, mrD, uv, duvdx, duvdy);

    float3 N = normalize(tangentNormal.x * normalize(tangentI) +
                         tangentNormal.y * normalize(bitangentI) +
                         tangentNormal.z * normalize(normalI));

    float metallicFactor = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic = float(mr.x) * metallicFactor;
    float roughness = float(mr.y) * roughnessFactor;
    roughness = SpecularAntiAliasing(roughness, float3(0, 0, 0), float3(0, 0, 0));

    float viewDepth = dot(worldPos - cameraPosition.xyz, cameraForward.xyz);
    ShadowCascadeBuffer cascades = sShadowCascades[0];
    float3 cascadeSplits = cascades.splitDistances.xyz;
    uint cascadeIndex = viewDepth > cascadeSplits.x ? 1u : 0u;
    cascadeIndex = viewDepth > cascadeSplits.y ? 2u : cascadeIndex;
    float4 shadowPos = MulShadowCascade(cascades, cascadeIndex, float4(worldPos, 1.0));
    float shadow = SampleShadow(ShadowMap, ShadowSampler, shadowPos, cascadeIndex, N, sunDirection.xyz);

    float2 screenUV = (float2(pixel) + 0.5f) / float2(outputSize);
    float ao = AmbientOcclusion.SampleLevel(Sampler, screenUV, 0.0f);
    float3 viewDir = cameraPosition.xyz - worldPos;
    float3 color = ApplyPBR(baseColor, N, viewDir, saturate(metallic), saturate(roughness), shadow, ao, sunDirection.xyz);

    if (enableLocalLights != 0u && tileCount.x != 0u && tileCount.y != 0u)
    {
        uint2 tile = min(pixel / LIGHT_TILE_SIZE, tileCount - 1u);
        uint tileIdx = tile.y * tileCount.x + tile.x;
        uint count = min(tileCounts[tileIdx], MAX_LIGHTS_PER_TILE);
        [loop]
        for (uint i = 0u; i < count; i++)
        {
            uint lightIndex = tileLightIndices[tileIdx * MAX_LIGHTS_PER_TILE + i];
            color += ApplyLocalLight(baseColor, N, viewDir, saturate(metallic), saturate(roughness), lights[lightIndex], worldPos, ao);
        }
    }

    OutputTexture[pixel] = float4(color, 1.0f);
}
