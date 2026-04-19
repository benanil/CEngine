// #define half min16float
// #define half2 min16float2
// #define half3 min16float3
// #define half4 min16float4
// #define half3x3 min16float3x3
// #define half3x4 min16float3x4
// #define half4x3 min16float4x3
#pragma dxc enable_16bit_types

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
    half4  aPos          : POSITION0;
    uint   aTangentSpace : TANGENT0;
    half2  aTexCoords    : TEXCOORD0;
    uint4  aJoints       : BLENDINDICES0;
    uint   aWeights      : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position  : SV_Position;
    half2  texCoords : TEXCOORD0;
    half3  normal    : NORMAL;
};

half4 QuaternionMul(half4 Q1, half4 Q2) {
    return half4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                 (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                 (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                 (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

half3 UnpackVec3XY11Z10Snorm(uint packed) {
    int16_t sx = int16_t((int)( packed << 21) >> 21); // 32 - 11
    int16_t sy = int16_t((int)((packed >> 11) << 21) >> 21);
    int16_t sz = int16_t((int)((packed >> 22) << 22) >> 22); // 32 - 10
    return half3(sx * (1.0 / 1023.0), sy * (1.0 / 1023.0), sz * (1.0 / 511.0));
}

half3 OctDecode(half2 f)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    half  t = saturate(-n.z);
    n.xy   += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

half2 decode_diamond(half p)
{
    half2 v;
    half p_sign = half(sign(p - 0.5));
    v.x = -p_sign * 4.0 * p + 1.0 + p_sign * 2.0;
    v.y = p_sign * (1.0 - abs(v.x));
    return normalize(v);
}

// https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/
half3 decode_tangent(half3 normal, half diamond_tangent)
{
    half3 t1;
    if (abs(normal.y) > abs(normal.z))
    {
        t1 = half3(normal.y, -normal.x, 0.f);
    }
    else
    {
        t1 = half3(normal.z, 0.f, -normal.x);
    }
    t1 = normalize(t1);

    half3 t2 = cross(t1, normal);
    half2 packed_tangent = decode_diamond(diamond_tangent);
    return packed_tangent.x * t1 + packed_tangent.y * t2;
}

void UnpackNormalTangent(uint packed, out half3 normal, out half3 tangent, out half3 binormal)
{
    half3 oct     = UnpackVec3XY11Z10Snorm(packed);
    normal        = OctDecode(oct.xy);
    half diamond  = oct.z * 0.5 + 0.5;
    tangent       = decode_tangent(normal, diamond);
    binormal      = cross(normal, tangent);
}

half3 QuaternionRotateVector(half4 quat, half3 vec) {
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0);
}

half3x3 Matrix3FromQuaternion(half4 q) 
{
    half3x3 mat;
    const half num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0 - (2.0 * (num8 + num7));
    mat[0][1] = 2.0 * (num6 + num5);
    mat[0][2] = 2.0 * (num4 - num3);
    mat[1][0] = 2.0 * (num6 - num5);
    mat[1][1] = 1.0 - (2.0 * (num7 + num9));
    mat[1][2] = 2.0 * (num2 + num);
    mat[2][0] = 2.0 * (num4 + num3);
    mat[2][1] = 2.0 * (num2 - num);
    mat[2][2] = 1.0 - (2.0 * (num8 + num9));
    return mat;
}

half2 unpackHalf2x16(uint packed) {
    return asfloat16(uint16_t2(uint16_t(packed), uint16_t(packed >> 16)));
}

half4 UnpackRGBA16Snorm(uint xy, uint zw) {
    int16_t4 raw;
    raw.x = int16_t(int(xy << 16u) >> 16);
    raw.y = int16_t(int(xy) >> 16);
    raw.z = int16_t(int(zw << 16u) >> 16);
    raw.w = int16_t(int(zw) >> 16);
    return half4(half4(raw) / half4(32767.0, 32767.0, 32767.0, 32767.0));
}

half3 UnpackVec3XY11Z10Unorm(uint packed) {
    uint16_t ux = uint16_t( packed        & 0x7FFu);
    uint16_t uy = uint16_t((packed >> 11) & 0x7FFu);
    uint16_t uz = uint16_t((packed >> 22)         );
    return half3(ux / 2047.0, uy / 2047.0, uz / 1023.0);
}

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    half3x4 animMat = half3x4(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    uint boneStart = instanceID * MaxBonePoses * MatrixNumInt32;
    half4 weights; // = half4(1.0, 0.0, 0.0, 0.0);
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w = max(1.0 - (weights.x + weights.y + weights.z), 0.0);
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        uint matIdx = input.aJoints[i] * MatrixNumInt32 + boneStart;
        half4 row0 = half4(unpackHalf2x16(sBoneMtx[matIdx + 0]), unpackHalf2x16(sBoneMtx[matIdx + 1]));
        half4 row1 = half4(unpackHalf2x16(sBoneMtx[matIdx + 2]), unpackHalf2x16(sBoneMtx[matIdx + 3]));
        half4 row2 = half4(unpackHalf2x16(sBoneMtx[matIdx + 4]), unpackHalf2x16(sBoneMtx[matIdx + 5]));
        animMat[0] += row0 * weights[i];
        animMat[1] += row1 * weights[i];
        animMat[2] += row2 * weights[i];
    }
    
    Entity  entity    = sEntities[instanceID];
    half4   insRot    = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    half3x3 insRotMat = Matrix3FromQuaternion(insRot);
    float3  insPos    = entity.position.xyz;
    half3   insScale  = UnpackVec3XY11Z10Unorm(entity.scale[0]) * 10.0; // half3(unpackHalf2x16(entity.scale[0]), asfloat16(uint16_t(entity.scale[1]))); 
    VSOutput o;
    o.texCoords = input.aTexCoords;

    half3x3 tbn; 
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1], tbn[0]);

    half4x3 animTransposed = transpose(animMat);
    tbn[0] = mul(half4(tbn[0], 0.0), animTransposed);
    tbn[1] = mul(half4(tbn[1], 0.0), animTransposed);
    tbn[2] = mul(half4(tbn[2], 0.0), animTransposed);

    // instance rotation (missing in your code)
    tbn[0] = mul(insRotMat, tbn[0]);
    tbn[2] = mul(insRotMat, tbn[2]);
    // re-orthonormalization
    tbn[1] = normalize(cross(tbn[2], tbn[0]));

    half3 worldPos = mul(half4(input.aPos.xyz, 1.0), animTransposed);
    worldPos = mul(insRotMat, worldPos); // use matrix, not quaternion, to match TBN path

    o.position = mul(uViewProj, float4(insScale * float3(worldPos) + insPos, 1.0));
    o.normal   = normalize(tbn[2]);
    return o;
}

