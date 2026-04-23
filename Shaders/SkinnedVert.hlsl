#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

struct Entity
{
    float4   position;
    uint32_t rotation[2];
    uint32_t scale[2];
};

StructuredBuffer<uint>   sBoneMtx  : register(t0);
StructuredBuffer<Entity> sEntities : register(t1);

static const uint MaxBonePoses = 128;
static const uint MatrixNumInt32 = 6;

struct VSInput
{
    fp16_4_io  aPos          : POSITION0;
    uint       aTangentSpace : TANGENT0;
    fp16_2_io  aTexCoords    : TEXCOORD0;
    uint4      aJoints       : BLENDINDICES0;
    uint       aWeights      : BLENDWEIGHT0;
};

struct VSOutput
{
    float4    position  : SV_Position;
    fp16_2_io texCoords : TEXCOORD0;
    fp16_3_io normal    : NORMAL;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    fp16_3x4 animMat = fp16_3x4(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    uint boneStart = instanceID * MaxBonePoses * MatrixNumInt32;
    fp16_4 weights; // = fp16_4(1.0, 0.0, 0.0, 0.0);
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w = max(1.0 - (weights.x + weights.y + weights.z), 0.0);
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        uint matIdx = input.aJoints[i] * MatrixNumInt32 + boneStart;
        fp16_4 row0 = fp16_4(unpackHalf2x16(sBoneMtx[matIdx + 0]), unpackHalf2x16(sBoneMtx[matIdx + 1]));
        fp16_4 row1 = fp16_4(unpackHalf2x16(sBoneMtx[matIdx + 2]), unpackHalf2x16(sBoneMtx[matIdx + 3]));
        fp16_4 row2 = fp16_4(unpackHalf2x16(sBoneMtx[matIdx + 4]), unpackHalf2x16(sBoneMtx[matIdx + 5]));
        animMat[0] += row0 * weights[i];
        animMat[1] += row1 * weights[i];
        animMat[2] += row2 * weights[i];
    }
    
    Entity   entity    = sEntities[instanceID];
    fp16_4   insRot    = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    fp16_3x3 insRotMat = Matrix3FromQuaternion(insRot);
    float3   insPos    = entity.position.xyz;
    fp16_3   insScale  = UnpackVec3XY11Z10Unorm(entity.scale[0]) * 10.0; // fp16_3(unpackHalf2x16(entity.scale[0]), asfloat16(uint16_t(entity.scale[1]))); 
    VSOutput o;
    o.texCoords = input.aTexCoords;

    fp16_3x3 tbn; 
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);

    fp16_4x3 animTransposed = transpose(animMat);
    tbn[1] = mul(fp16_4(tbn[1], 0.0), animTransposed);
    tbn[2] = mul(fp16_4(tbn[2], 0.0), animTransposed);

    // instance rotation
    tbn[1] = mul(insRotMat, tbn[1]);
    tbn[2] = mul(insRotMat, tbn[2]);
    // re-orthonormalization
    tbn[0] = Orthonormalize(tbn[2], tbn[1]);
   
    fp16_3 worldPos = mul(fp16_4(input.aPos.xyz, 1.0), animTransposed);
    worldPos = mul(insRotMat, worldPos); // use matrix, not quaternion, to match TBN path

    o.position = mul(uViewProj, float4(insScale * float3(worldPos) + insPos, 1.0));
    o.normal   = normalize(tbn[2]);
    return o;
}

