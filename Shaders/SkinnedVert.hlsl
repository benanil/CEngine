
// shadercross SkinnedVert.hlsl -s HLSL -d SPIRV -t vertex -o SkinnedVert.spv
// bin2c -o SkinnedVert.spv.h SkinnedVert.spv

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

StructuredBuffer<uint>   sBoneMtx          : register(t0);
StructuredBuffer<float4> sInstancePosition : register(t1);
StructuredBuffer<uint>   sInstanceRotation : register(t2);

struct VSInput
{
    float3 aPos        : POSITION0;
    uint   aQTangentXY : TANGENT0;
    uint   aQTangentZW : TANGENT1;
    float2 aTexCoords  : TEXCOORD0;
    uint4  aJoints     : BLENDINDICES0;
    uint   aWeights    : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 normal    : NORMAL;
};

float4 QuaternionMul(float4 Q1, float4 Q2)
{
    return float4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                  (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                  (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                  (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

float3 QuaternionRotateVector(float4 quat, float3 vec)
{
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0f);
}

float3x3 Matrix3FromQuaternion(float4 q)
{
    float3x3 mat;
    const float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0f - (2.0f * (num8 + num7));
    mat[0][1] = 2.0f * (num6 + num5);
    mat[0][2] = 2.0f * (num4 - num3);
    mat[1][0] = 2.0f * (num6 - num5);
    mat[1][1] = 1.0f - (2.0f * (num7 + num9));
    mat[1][2] = 2.0f * (num2 + num);
    mat[2][0] = 2.0f * (num4 + num3);
    mat[2][1] = 2.0f * (num2 - num);
    mat[2][2] = 1.0f - (2.0f * (num8 + num9));
    return mat;
}

// HLSL equivalent to GLSL's unpackHalf2x16
float2 unpackHalf2x16(uint packed)
{
    return float2(
        f16tof32(packed & 0xFFFF),
        f16tof32((packed >> 16) & 0xFFFF)
    );
}

float4 UnpackRGBA16Snorm(uint xy, uint zw) 
{
    int4 raw;
    raw.x = (int(xy << 16u) >> 16);
    raw.y = int(xy) >> 16;
    raw.z = (int(zw << 16u) >> 16);
    raw.w = int(zw) >> 16;
    return float4(raw) / float4(32767.0, 32767.0, 32767.0, 32767.0);
}


float3 UnpackVec3XY11Z10Snorm(uint packed)
{
    // extract fields
    uint ux =  packed        & 0x7FFu;
    uint uy = (packed >> 11) & 0x7FFu;
    uint uz = (packed >> 22) & 0x3FFu;

    // sign-extend: (v << (32 - bits)) >> (32 - bits)
    int sx = (int)(ux << 21) >> 21; // 32 - 11
    int sy = (int)(uy << 21) >> 21;
    int sz = (int)(uz << 22) >> 22; // 32 - 10

    // normalize back to [-1,1]
    return float3(
        sx * (1.0 / 1023.0),
        sy * (1.0 / 1023.0),
        sz * (1.0 / 511.0)
    );
}

float3 UnpackVec3XY11Z10Unorm(uint packed)
{
    uint ux =  packed        & 0x7FFu;
    uint uy = (packed >> 11) & 0x7FFu;
    uint uz = (packed >> 22) & 0x3FFu;

    return float3(
        ux / 2047.0,
        uy / 2047.0,
        uz / 1023.0
    );
}

float4 GetInstanceRotation(uint instanceID)
{
    instanceID <<= 1;
    return normalize(UnpackRGBA16Snorm(sInstanceRotation[instanceID], sInstanceRotation[instanceID+1]));
}

static const uint MaxBonePoses = 128;
static const uint MatrixNumInt32 = 6;

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput o;
    
    // Initialize animation matrix (3 rows, 4 columns)
    float3x4 animMat = float3x4(0.0, 0.0, 0.0, 0.0,
                                0.0, 0.0, 0.0, 0.0,
                                0.0, 0.0, 0.0, 0.0);
    
    uint boneStart = instanceID * MaxBonePoses * MatrixNumInt32;
    float4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w = 1.0 - (weights.x + weights.y + weights.z);
    // Accumulate bone transforms
    for (int i = 0; i < 4; i++)
    {
        uint matIdx = input.aJoints[i] * MatrixNumInt32 + boneStart;
        
        float4 row0 = float4(unpackHalf2x16(sBoneMtx[matIdx + 0]), 
                             unpackHalf2x16(sBoneMtx[matIdx + 1]));
        float4 row1 = float4(unpackHalf2x16(sBoneMtx[matIdx + 2]), 
                             unpackHalf2x16(sBoneMtx[matIdx + 3]));
        float4 row2 = float4(unpackHalf2x16(sBoneMtx[matIdx + 4]), 
                             unpackHalf2x16(sBoneMtx[matIdx + 5]));
        
        animMat[0] += row0 * weights[i];
        animMat[1] += row1 * weights[i];
        animMat[2] += row2 * weights[i];
    }
    
    float4 qtangent = UnpackRGBA16Snorm(input.aQTangentXY, input.aQTangentZW);
    float3x3 tbn; // = Matrix3FromQuaternion(qtangent);
    tbn[2] = UnpackVec3XY11Z10Snorm(input.aQTangentXY);
    tbn[1] = UnpackVec3XY11Z10Snorm(input.aQTangentZW);
    tbn[0] = cross(tbn[2], tbn[1]);

    // Transform position: multiply [4x1] * [3x4] = [3x1]
    float4x3 animTransposed = transpose(animMat);
    float3 worldPos = mul(float4(input.aPos, 1.0), animTransposed);

    // Transform TBN by animation matrix (apply to each basis vector)
    tbn[0] = mul(float4(tbn[0], 0.0), animTransposed);
    tbn[1] = mul(float4(tbn[1], 0.0), animTransposed);
    tbn[2] = mul(float4(tbn[2], 0.0), animTransposed);

    // Apply instance rotation
    float4 instanceRotation = GetInstanceRotation(instanceID);
    worldPos = QuaternionRotateVector(instanceRotation, worldPos);
    worldPos *= 0.1610;
    worldPos += sInstancePosition[instanceID].xyz;

    // Rotate TBN basis vectors by instance rotation
    float3x3 instanceRotMat = Matrix3FromQuaternion(instanceRotation);
    tbn = mul(tbn, instanceRotMat);

    o.texCoords = input.aTexCoords;
    o.position = mul(uViewProj, float4(worldPos, 1.0));
    o.normal = normalize(tbn[2]); // Normalize after transformations
    return o;
}