// Visibility-buffer materialize pass (Phase A, surface set only).
//
// Reads the visibility buffer (drawID, instanceID, triangleID), re-fetches the triangle's
// three vertices from the global vertex/index buffers, reconstructs per-pixel attributes via
// analytic perspective-correct barycentrics + analytic uv gradients, samples the material, and
// writes the SAME three G-buffer textures the old direct gbuffer pass produced. Everything
// downstream (HBAO, deferred lighting, light volumes) is unchanged.
//
// Skinned + terrain geometry still go through the direct gbuffer pass; their pixels carry the
// 0xFFFFFFFF sentinel in the visbuffer and are skipped here so we don't clobber them.
//
// NOTE: keep the shading (material fetch -> gbuffer pack) in sync with Surface.hlsl frag().
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "PBR.hlsl"
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

// matches the CPU AVertex layout (16 bytes): u64 position + u32 octTbn + u32 texCoord
struct SurfaceVertex
{
    uint2 position; // unorm16 xyz against the primitive AABB
    uint  octTbn;
    uint  texCoord; // half2
};

// --- sampled textures (space0) + samplers ---
Texture2DArray<float4> AlbedoPages            : register(t0, space0);
Texture2DArray<float2> NormalPages            : register(t1, space0);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space0);
Texture2D<float>       ShadowMap              : register(t3, space0);
Texture2D<float>       HiZDepth               : register(t4, space0);
SamplerState           Sampler                : register(s0, space0);
SamplerState           ShadowSampler          : register(s3, space0);
// --- read-only storage textures (space0, after sampled) ---
Texture2D<uint2>       VisBuffer              : register(t5, space0);
// --- read-only storage buffers (space0, after storage textures) ---
StructuredBuffer<Entity>             sEntities           : register(t6, space0);
StructuredBuffer<PrimitiveGroup>     sPrimitiveGroups    : register(t7, space0);
StructuredBuffer<uint>               sDrawSparseIndices  : register(t8, space0);
StructuredBuffer<SurfaceVertex>      sVertexBuffer       : register(t9, space0);
StructuredBuffer<uint>               sIndexBuffer        : register(t10, space0);
StructuredBuffer<MaterialGPU>        sMaterials          : register(t11, space0);
StructuredBuffer<TextureDescriptor>  sTextureDescriptors : register(t12, space0);
StructuredBuffer<ShadowCascadeBuffer> sShadowCascades    : register(t13, space0);
// --- read-write storage textures (space1) = the gbuffer ---
[[vk::image_format("r32ui")]] RWTexture2D<uint>   OutTangentFrame    : register(u0, space1);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutAlbedoMetallic  : register(u1, space1);
[[vk::image_format("rg8")]]   RWTexture2D<float2> OutShadowRoughness : register(u2, space1);

float3 SurfaceWorldPos(SurfaceVertex v, PrimitiveGroup group, f16_4 insRot, f16_3 insScale, float3 entityPos)
{
    float3 localPos = group.aabbMin.xyz + UnpackUnorm16x4(v.position).xyz * (group.aabbMax.xyz - group.aabbMin.xyz);
    f16_3  worldPos = QMulVec3(insRot, f16_3(localPos) * insScale);
    return float3(worldPos) + entityPos;
}

// per-vertex tangent frame in world space, matching Surface.vert (post entity-rotation, orthonormalized)
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

// perspective-correct barycentrics for a screen point, given the triangle's NDC screen positions
// (clip.xy/clip.w) and inverse-w per vertex. Returns weights for (v0, v1, v2).
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
    if (depth >= 0.9999f) return; // sky: deferred lighting handles it via depth

    uint2 ids = VisBuffer.Load(int3(pixel, 0));
    // sentinel: cleared visbuffer has primitiveIdx 0xFFFF, which is never valid (MAX_GROUP=65535,
    // so valid indices are 0..65534). Those pixels belong to skinned/terrain (direct gbuffer).
    if ((ids.x >> 16) == 0xFFFFu) return;
    if ((ids.y & 0x80u) != 0u) return; // skinned pixel: handled by the skinned materialize pass

    uint primitiveIdx = ids.x >> 16;
    uint instanceID   = ids.x & 0xFFFFu;
    uint triangleID   = ids.y >> 8;
    uint lod          = ids.y & 0x03u;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    uint denseIdx = sDrawSparseIndices[lod * MAX_ENTITY + group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackRGBA16Unorm(entity.scale).xyz * f16(10.0);

    // fetch the triangle. vertexOffset is 0 in the draw args, so index values are absolute into
    // the global surface vertex buffer (see CullDrawArgsCompute Initialize()).
    uint triBase = group.lodIndexOffset[lod] + triangleID * 3u;
    uint i0 = sIndexBuffer[triBase + 0u];
    uint i1 = sIndexBuffer[triBase + 1u];
    uint i2 = sIndexBuffer[triBase + 2u];
    SurfaceVertex v0 = sVertexBuffer[i0];
    SurfaceVertex v1 = sVertexBuffer[i1];
    SurfaceVertex v2 = sVertexBuffer[i2];

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

    float2 invSize  = 1.0 / float2(outputSize);
    float2 uvCenter = (float2(pixel) + 0.5) * invSize;
    float2 ndcC = float2(uvCenter.x * 2.0 - 1.0,                   1.0 - uvCenter.y * 2.0);
    float2 ndcX = float2((uvCenter.x + invSize.x) * 2.0 - 1.0,     1.0 - uvCenter.y * 2.0);
    float2 ndcY = float2(uvCenter.x * 2.0 - 1.0,                   1.0 - (uvCenter.y + invSize.y) * 2.0);

    float3 bC = PerspBary(s0, s1, s2, invW, ndcC);
    float3 bX = PerspBary(s0, s1, s2, invW, ndcX);
    float3 bY = PerspBary(s0, s1, s2, invW, ndcY);

    // texcoords + analytic gradients
    float2 t0 = float2(UnpackHalf2(v0.texCoord));
    float2 t1 = float2(UnpackHalf2(v1.texCoord));
    float2 t2 = float2(UnpackHalf2(v2.texCoord));
    float2 uv    = bC.x * t0 + bC.y * t1 + bC.z * t2;
    float2 duvdx = (bX.x * t0 + bX.y * t1 + bX.z * t2) - uv;
    float2 duvdy = (bY.x * t0 + bY.y * t1 + bY.z * t2) - uv;

    // interpolated tangent frame (rasterizer-equivalent: interpolate then normalize in shading)
    float3 N0, T0, B0, N1, T1, B1, N2, T2, B2;
    SurfaceTBN(v0.octTbn, insRot, N0, T0, B0);
    SurfaceTBN(v1.octTbn, insRot, N1, T1, B1);
    SurfaceTBN(v2.octTbn, insRot, N2, T2, B2);
    float3 normalI    = bC.x * N0 + bC.y * N1 + bC.z * N2;
    float3 tangentI   = bC.x * T0 + bC.y * T1 + bC.z * T2;
    float3 bitangentI = bC.x * B0 + bC.y * B1 + bC.z * B2;
    float  handedness = float(UnpackTangentHandedness(v0.octTbn)); // provoking vertex (flat in raster)

    float3 worldPos = bC.x * wp0 + bC.y * wp1 + bC.z * wp2;

    // ----- shading, mirroring Surface.hlsl frag() -----
    MaterialGPU material = sMaterials[group.materialIndex];
    TextureDescriptor albedoD = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalD = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrD     = sTextureDescriptors[material.metallicRoughnessDescriptor];

    f16_4 albedoSample = SampleTexturePageRGBAGrad(AlbedoPages, Sampler, albedoD, uv, duvdx, duvdy);
    f16_4 baseFactor   = UnpackColor4UintF16(material.baseColorFactor);
    // no alpha-clip here: the vis raster already discarded clipped fragments, and `discard` is
    // illegal in a compute shader.
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

    // Phase A: no screen-space dN in compute, so specular AA is a no-op (passing zero derivatives
    // leaves roughness unchanged). Geometric specular AA can be reconstructed later if needed.
    roughness = SpecularAntiAliasing(roughness, float3(0, 0, 0), float3(0, 0, 0));

    float3 T = normalize(tangentI - dot(tangentI, N) * N);

    OutTangentFrame[pixel]    = PackNormalTangent(f16_3(N), f16_4(f16_3(T), f16(handedness)));
    OutAlbedoMetallic[pixel]  = float4(float3(baseColor), saturate(metallic));
    OutShadowRoughness[pixel] = float2(saturate(shadow), saturate(roughness));
}
