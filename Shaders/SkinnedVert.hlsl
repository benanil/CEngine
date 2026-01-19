
// shadercross SkinnedVert.hlsl -s HLSL -d SPIRV -t vertex -o SkinnedVert.spv
// bin2c -o SkinnedVert.spv.h SkinnedVert.spv

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

RWStructuredBuffer<uint> sBoneMtx : register(u0);

struct VSInput
{
    float3 aPos       : POSITION0;
    float3 aNormal    : NORMAL0;
    float4 aTangent   : TANGENT0;
    float2 aTexCoords : TEXCOORD0;
    uint4  aJoints    : BLENDINDICES0;
    float4 aWeights   : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 normal    : NORMAL;
};

// Helper function to unpack half floats (HLSL equivalent to GLSL's unpackHalf2x16)
float2 unpackHalf2x16(uint packed)
{
    return float2(
        f16tof32(packed & 0xFFFF),
        f16tof32((packed >> 16) & 0xFFFF)
    );
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
        
        animMat[0] += row0 * input.aWeights[i];
        animMat[1] += row1 * input.aWeights[i];
        animMat[2] += row2 * input.aWeights[i];
    }
    
    // Transform position: multiply [4x1] * [3x4] = [3x1]
    float3 worldPos = mul(float4(input.aPos, 1.0), transpose(animMat));
    
    // Transform normal (w=0 for direction vectors)
    float3 worldNormal = mul(float4(input.aNormal, 0.0), transpose(animMat));
    
    o.texCoords = input.aTexCoords;
    o.position = mul(uViewProj, float4(worldPos, 1.0));
    o.normal = normalize(worldNormal);
    
    return o;
}