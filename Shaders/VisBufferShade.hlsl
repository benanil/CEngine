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

RWStructuredBuffer<LightGPU>          lights              : register(u1, space1);
RWStructuredBuffer<PointShadowMatrix> PointShadowMatrices : register(u2, space1);
RWStructuredBuffer<PointShadowMatrix> SpotShadowMatrices  : register(u3, space1);
RWStructuredBuffer<uint>              tileCounts          : register(u4, space1);
RWStructuredBuffer<uint>              tileLightIndices    : register(u5, space1);

#include "LocalLights.hlsl"

// OPTIMIZATION: Takes pre-calculated AABB extents to avoid recalculating 3 times.
float3 SurfaceWorldPos(SurfaceVertex v, float3 aabbMin, float3 aabbExtent, f16_4 insRot, f16_3 insScale, float3 entityPos)
{
    float3 localPos = aabbMin + UnpackUnorm16x4(v.position).xyz * aabbExtent;
    f16_3  worldPos = QMulVec3(insRot, f16_3(localPos) * insScale);
    return float3(worldPos) + entityPos;
}

// OPTIMIZATION: Cache invariant triangle data to skip redundant determinant math in derivatives.
struct BarySetup {
    float2 s0, e0, e1;
    float invDet;
    float3 invW;
};

BarySetup CalcBarySetup(float2 s0, float2 s1, float2 s2, float3 invW)
{
    BarySetup b;
    b.s0 = s0;
    b.e0 = s1 - s0;
    b.e1 = s2 - s0;
    b.invW = invW;
    float det = b.e0.x * b.e1.y - b.e0.y * b.e1.x;
    b.invDet = abs(det) > 1e-20f ? (1.0f / det) : 0.0f;
    return b;
}

float3 EvalBary(BarySetup b, float2 p)
{
    float2 ep = p - b.s0;
    float b1 = (ep.x * b.e1.y - ep.y * b.e1.x) * b.invDet;
    float b2 = (b.e0.x * ep.y - b.e0.y * ep.x) * b.invDet;
    float b0 = 1.0f - b1 - b2;
    float3 pc = float3(b0, b1, b2) * b.invW;
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

    // Combine bitwise checks and failure states
    uint2 ids = VisBuffer.Load(int3(pixel, 0));
    if (ids.x == 0xFFFFFFFFu || (ids.y & 0x80u) != 0u) return;

    uint denseIdx   = ids.x;
    uint triangleID = ids.y >> 8;
    uint lod        = ids.y & 0x03u;
    
    Entity entity = sEntities[denseIdx];
    PrimitiveGroup group = sPrimitiveGroups[entity.primitiveIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackRGBA16Unorm(entity.scale).xyz * f16(10.0);

    uint triBase = group.lodIndexOffset[lod] + triangleID * 3u;
    SurfaceVertex v0 = sVertexBuffer[sIndexBuffer[triBase + 0u]];
    SurfaceVertex v1 = sVertexBuffer[sIndexBuffer[triBase + 1u]];
    SurfaceVertex v2 = sVertexBuffer[sIndexBuffer[triBase + 2u]];

    // OPTIMIZATION: Calculate AABB boundaries once.
    float3 aabbMin = group.aabbMin.xyz;
    float3 aabbExtent = group.aabbMax.xyz - aabbMin;
    float3 wp0 = SurfaceWorldPos(v0, aabbMin, aabbExtent, insRot, insScale, entity.position.xyz);
    float3 wp1 = SurfaceWorldPos(v1, aabbMin, aabbExtent, insRot, insScale, entity.position.xyz);
    float3 wp2 = SurfaceWorldPos(v2, aabbMin, aabbExtent, insRot, insScale, entity.position.xyz);

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

    // OPTIMIZATION: Compute constant determinant logic once for all 3 barycentric queries.
    BarySetup barySetup = CalcBarySetup(s0, s1, s2, invW);
    float3 bC = EvalBary(barySetup, ndcC);
    float3 bX = EvalBary(barySetup, ndcX);
    float3 bY = EvalBary(barySetup, ndcY);

    float2 t0 = float2(UnpackHalf2(v0.texCoord));
    float2 t1 = float2(UnpackHalf2(v1.texCoord));
    float2 t2 = float2(UnpackHalf2(v2.texCoord));
    float2 uv = bC.x * t0 + bC.y * t1 + bC.z * t2;
    float2 duvdx = (bX.x * t0 + bX.y * t1 + bX.z * t2) - uv;
    float2 duvdy = (bY.x * t0 + bY.y * t1 + bY.z * t2) - uv;

    // OPTIMIZATION: Interpolate local TBN BEFORE heavy quaternion transformations.
    f16_3 rn0, rt0; UnpackNormalTangent(v0.octTbn, rn0, rt0);
    f16_3 rn1, rt1; UnpackNormalTangent(v1.octTbn, rn1, rt1);
    f16_3 rn2, rt2; UnpackNormalTangent(v2.octTbn, rn2, rt2);

    float3 localN = bC.x * float3(rn0) + bC.y * float3(rn1) + bC.z * float3(rn2);
    float3 localT = bC.x * float3(rt0) + bC.y * float3(rt1) + bC.z * float3(rt2);

    // Transform linearly interpolated frame once.
    float3 normalI = normalize(float3(QMulVec3(insRot, f16_3(localN))));
    float3 tangentI = float3(QMulVec3(insRot, f16_3(localT)));
    
    f16 h = UnpackTangentHandedness(v0.octTbn);
    tangentI = normalize(Orthonormalize(tangentI, normalI));
    float3 bitangentI = normalize(cross(normalI, tangentI)) * float(h);

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

    float3 N = normalize(tangentNormal.x * tangentI +
                         tangentNormal.y * bitangentI +
                         tangentNormal.z * normalI);

    float metallicFactor = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic = float(mr.x) * metallicFactor;
    float roughness = SpecularAntiAliasing(float(mr.y) * roughnessFactor, float3(0, 0, 0), float3(0, 0, 0));

    float viewDepth = dot(worldPos - cameraPosition.xyz, cameraForward.xyz);
    ShadowCascadeBuffer cascades = sShadowCascades[0];
    
    // OPTIMIZATION: Branchless cascade index calculation.
    uint cascadeIndex = (viewDepth > cascades.splitDistances.x ? 1u : 0u) + 
		(viewDepth > cascades.splitDistances.y ? 1u : 0u);
                        
    float4 shadowPos = MulShadowCascade(cascades, cascadeIndex, float4(worldPos, 1.0));
    float shadow = SampleShadow(ShadowMap, ShadowSampler, shadowPos, cascadeIndex, N, sunDirection.xyz);

    float2 screenUV = (float2(pixel) + 0.5f) * invSize;
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