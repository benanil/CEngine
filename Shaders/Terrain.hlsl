// terrain g-buffer pass: decodes the compact 16 byte chunk relative vertex, shades with
// triplanar mapping from three material layers (grass/leaves, rocky slope, high rock)
// blended by slope and altitude. outputs match Surface.hlsl's g-buffer layout
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "PBR.hlsl"
#include "Shadow/Shadow.hlsl"

#define TERRAIN_UV_SCALE     (1.0 / 6.0)  // one texture tile every 6 meters
#define TERRAIN_NORMAL_DX    1            // nor_dx textures, green points down

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4   uChunkOriginSize; // xyz chunk world origin, w chunk world size
    float4   uCameraPosition;
    float4   uCameraForward;
};

cbuffer ps_params : register(b0, space3)
{
    float4 uSunDirection;
};

StructuredBuffer<ShadowCascadeBuffer> sShadowCascades : register(t0);

Texture2DArray<float4> AlbedoLayers : register(t0, space2); // 0 grass, 1 rocky slope, 2 high rock
Texture2DArray<float4> NormalLayers : register(t1, space2);
Texture2DArray<float4> ArmLayers    : register(t2, space2); // ao, roughness, metallic
Texture2D<float>       ShadowMap    : register(t3, space2);
SamplerState           Sampler      : register(s0, space2);
SamplerState           ShadowSampler : register(s3, space2);

struct VSInput
{
    uint4 data : TEXCOORD0; // posXY, posZ, octNormal, spare
};

struct VSOutput
{
    float4 position   : SV_Position;
    float3 worldPos   : TEXCOORD0;
    float3 normal     : NORMAL;
    float4 shadowPos0 : TEXCOORD1;
    float4 shadowPos1 : TEXCOORD2;
    float4 shadowPos2 : TEXCOORD3;
    float  viewDepth  : TEXCOORD4;
    nointerpolation float3 cascadeSplits : TEXCOORD5;
};

struct GBufferOutput
{
    uint     tangentFrame    : SV_Target0;
    f16_4_io albedoMetallic  : SV_Target1;
    f16_2_io shadowRoughness : SV_Target2;
};

// 3 x 21 bit fixed point, 32768 steps per cell over a 16 cell chunk -> 524288 max
float3 TerrainDecodePosition(uint4 data)
{
    uint qx = data.x & 0x1FFFFFu;
    uint qy = (data.x >> 21) | ((data.y & 0x3FFu) << 11);
    uint qz = (data.y >> 10) & 0x1FFFFFu;
    return float3(qx, qy, qz) * (uChunkOriginSize.w / 524288.0) + uChunkOriginSize.xyz;
}

float3 TerrainDecodeNormal(uint packed)
{
    float2 oct = float2(float(packed & 0xFFFFu), float(packed >> 16)) * (2.0 / 65535.0) - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

VSOutput vert(VSInput input)
{
    float3 worldPos = TerrainDecodePosition(input.data);

    VSOutput o;
    o.position = mul(uViewProj, float4(worldPos, 1.0));
    o.worldPos = worldPos;
    o.normal   = TerrainDecodeNormal(input.data.z);

    ShadowCascadeBuffer cascades = sShadowCascades[0];
    o.shadowPos0 = MulShadowCascade(cascades, 0u, float4(worldPos, 1.0));
    o.shadowPos1 = MulShadowCascade(cascades, 1u, float4(worldPos, 1.0));
    o.shadowPos2 = MulShadowCascade(cascades, 2u, float4(worldPos, 1.0));
    o.viewDepth  = dot(worldPos - uCameraPosition.xyz, uCameraForward.xyz);
    o.cascadeSplits = cascades.splitDistances.xyz;
    return o;
}

// samples one map type for two layers across the three planar projections
float4 SampleTriplanarLayer(Texture2DArray<float4> tex, float layer, float3 wpos, float3 blend)
{
    float4 sx = tex.Sample(Sampler, float3(wpos.zy * TERRAIN_UV_SCALE, layer));
    float4 sy = tex.Sample(Sampler, float3(wpos.xz * TERRAIN_UV_SCALE, layer));
    float4 sz = tex.Sample(Sampler, float3(wpos.xy * TERRAIN_UV_SCALE, layer));
    return sx * blend.x + sy * blend.y + sz * blend.z;
}

// whiteout blend triplanar normal mapping
float3 SampleTriplanarNormal(float layer, float3 wpos, float3 blend, float3 N)
{
    float3 tx = NormalLayers.Sample(Sampler, float3(wpos.zy * TERRAIN_UV_SCALE, layer)).xyz * 2.0 - 1.0;
    float3 ty = NormalLayers.Sample(Sampler, float3(wpos.xz * TERRAIN_UV_SCALE, layer)).xyz * 2.0 - 1.0;
    float3 tz = NormalLayers.Sample(Sampler, float3(wpos.xy * TERRAIN_UV_SCALE, layer)).xyz * 2.0 - 1.0;
#if TERRAIN_NORMAL_DX
    tx.y = -tx.y; ty.y = -ty.y; tz.y = -tz.y;
#endif
    // whiteout: keep the projection plane derivatives, take z from the geometric normal
    float3 nx = float3(tx.xy + N.zy, abs(N.x) * tx.z);
    float3 ny = float3(ty.xy + N.xz, abs(N.y) * ty.z);
    float3 nz = float3(tz.xy + N.xy, abs(N.z) * tz.z);
    float3 n = nx.zyx * blend.x + ny.xzy * blend.y + nz.xyz * blend.z;
    return normalize(lerp(N, normalize(n), 0.8));
}

GBufferOutput frag(VSOutput input)
{
    float3 N = normalize(input.normal);
    float3 blend = pow(abs(N), 4.0);
    blend /= (blend.x + blend.y + blend.z);

    // layer selection: grass on flat ground, rocky slope on steep, high rock above the snowline-ish altitude
    float slope     = N.y;
    float rockBlend = 1.0 - saturate((slope - 0.55) * 4.0);      // 0 flat .. 1 steep
    float highBlend = saturate((input.worldPos.y - 34.0) * 0.08);

    float baseLayer  = rockBlend > 0.5 ? 1.0 : 0.0;
    float otherLayer = rockBlend > 0.5 ? 0.0 : 1.0;
    float baseT      = rockBlend > 0.5 ? (1.0 - rockBlend) * 2.0 : rockBlend * 2.0;

    float4 albedoA = SampleTriplanarLayer(AlbedoLayers, baseLayer, input.worldPos, blend);
    float4 albedoB = SampleTriplanarLayer(AlbedoLayers, otherLayer, input.worldPos, blend);
    float4 armA    = SampleTriplanarLayer(ArmLayers, baseLayer, input.worldPos, blend);
    float4 armB    = SampleTriplanarLayer(ArmLayers, otherLayer, input.worldPos, blend);
    float4 albedoSample = lerp(albedoA, albedoB, baseT);
    float4 arm          = lerp(armA, armB, baseT);

    if (highBlend > 0.01)
    {
        float4 albedoC = SampleTriplanarLayer(AlbedoLayers, 2.0, input.worldPos, blend);
        float4 armC    = SampleTriplanarLayer(ArmLayers, 2.0, input.worldPos, blend);
        albedoSample = lerp(albedoSample, albedoC, highBlend);
        arm          = lerp(arm, armC, highBlend);
    }

    float3 shadingN = SampleTriplanarNormal(baseT > 0.5 ? otherLayer : baseLayer, input.worldPos, blend, N);

    f16_3 baseColor = SRGBToLinear(f16_3(albedoSample.rgb)) * f16(arm.r); // bake ao into albedo
    float metallic  = saturate(arm.b);
    float roughness = saturate(arm.g);

    uint cascadeIndex = input.viewDepth > input.cascadeSplits.x ? 1u : 0u;
    cascadeIndex = input.viewDepth > input.cascadeSplits.y ? 2u : cascadeIndex;
    float4 shadowPos = cascadeIndex == 0u ? input.shadowPos0 : (cascadeIndex == 1u ? input.shadowPos1 : input.shadowPos2);
    float shadow = SampleShadow(ShadowMap, ShadowSampler, shadowPos, cascadeIndex, shadingN, uSunDirection.xyz);

    roughness = SpecularAntiAliasing(roughness, ddx(shadingN), ddy(shadingN));

    // arbitrary stable tangent, terrain has no authored uv direction
    float3 T = normalize(abs(shadingN.y) < 0.9 ? cross(float3(0.0, 1.0, 0.0), shadingN)
                                               : cross(float3(1.0, 0.0, 0.0), shadingN));

    GBufferOutput output;
    output.tangentFrame    = PackNormalTangent(f16_3(shadingN), f16_4(f16_3(T), f16(1.0)));
    output.albedoMetallic  = f16_4_io(float4(float3(baseColor), metallic));
    output.shadowRoughness = f16_2_io(float2(saturate(shadow), roughness));
    return output;
}
