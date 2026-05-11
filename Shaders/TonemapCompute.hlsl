cbuffer TonemapParams : register(b0, space2)
{
    uint2 outputSize;
    float exposure;
    float gamma;
};

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler      : register(s0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

float3 MulMat3(float3 row0, float3 row1, float3 row2, float3 v)
{
    return float3(dot(row0, v), dot(row1, v), dot(row2, v));
}

float3 AgXDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2
         - 40.14f * x4 * x
         + 31.96f * x4
         - 6.868f * x2 * x
         + 0.4298f * x2
         + 0.1191f * x
         - 0.00232f;
}

float3 TonemapAgX(float3 color)
{
    color = MulMat3(
        float3(0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f),
        float3(0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f),
        float3(0.0423756549057051f, 0.0784336f,          0.879142973793104f),
        color);

    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    color = clamp(log2(max(color, 1e-10f)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = AgXDefaultContrastApprox(color);

    color = MulMat3(
        float3(1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f),
        float3(-0.0528968517574562f, 1.15190312990417f,   -0.0989611768448433f),
        float3(-0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f),
        color);

    return saturate(color);
}


float3 TonemapACES(float3 color)
{
    float3 v = MulMat3(
        float3(0.59719f, 0.35458f, 0.04823f),
        float3(0.07600f, 0.90834f, 0.01566f),
        float3(0.02840f, 0.13383f, 0.83777f),
        color);

    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    v = a / b;

    v = MulMat3(
        float3(1.60475f, -0.53108f, -0.07367f),
        float3(-0.10208f, 1.10813f, -0.00605f),
        float3(-0.00327f, -0.07276f, 1.07602f),
        v);

    return saturate(v);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 color = SourceTexture.SampleLevel(SourceSampler, uv, 0.0f).rgb;
    color = TonemapACES(color * exposure);
    color = pow(color, 1.0f / max(gamma, 0.001f));
    OutputTexture[tid.xy] = float4(color, 1.0f);
}
