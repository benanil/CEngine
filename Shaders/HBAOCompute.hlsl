cbuffer HBAOParams : register(b0, space2)
{
    float4x4 invProjection;
    float4x4 view;
    uint2 fullSize;
    uint2 aoSize;
    float radius;
    float projectionScale;
    float bias;
    float intensity;
    float power;
    uint enabled;
    uint frameIndex;
    uint padding;
};

Texture2D<float> DepthTexture : register(t0, space0);
Texture2D<float4> NormalTexture : register(t1, space0);
[[vk::image_format("r8")]] RWTexture2D<float> OutputAO : register(u0, space1);

static const float HBAO_PI = 3.14159265f;
static const uint NUM_DIRECTIONS = 8u;
static const uint NUM_STEPS = 4u;

float Hash12(uint2 p)
{
    uint x = p.x * 1664525u + p.y * 1013904223u + frameIndex * 747796405u;
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return float(x & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

float3 ReconstructView(uint2 p)
{
    p = min(p, fullSize - 1u);
    float depth = DepthTexture.Load(int3(p, 0));
    float2 uv = (float2(p) + 0.5f) / float2(fullSize);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 view = mul(invProjection, clip);
    return view.xyz / max(abs(view.w), 0.00001f);
}

float ComputeAOContribution(float3 p, float3 n, float3 s)
{
    float3 v = s - p;
    float vv = dot(v, v);
    if (vv <= 0.000001f) return 0.0f;
    float invLen = rsqrt(vv);
    float ndv = dot(n, v) * invLen;
    float falloff = saturate(1.0f - vv / max(radius * radius, 0.0001f));
    return saturate(ndv - bias) * falloff;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;
    if (enabled == 0u)
    {
        OutputAO[tid.xy] = 1.0f;
        return;
    }

    uint2 p = tid.xy;
    uint2 fullP = min(p * 2u + 1u, fullSize - 1u);
    float centerDepth = DepthTexture.Load(int3(fullP, 0));
    if (centerDepth >= 0.99999f)
    {
        OutputAO[p] = 1.0f;
        return;
    }

    float3 viewPos = ReconstructView(fullP);
    float3 worldNormal = normalize(NormalTexture.Load(int3(p, 0)).xyz * 2.0f - 1.0f);
    float3 normal = normalize(mul((float3x3)view, worldNormal));
    float viewZ = max(abs(viewPos.z), 0.01f);
    float radiusPixels = clamp(radius * projectionScale / viewZ * 0.5f, 2.0f, 48.0f);
    float stepPixels = radiusPixels / float(NUM_STEPS + 1u);

    float jitterAngle = Hash12(p) * 2.0f * HBAO_PI;
    float jitterStep = Hash12(p.yx + 17u);
    float2 rot = float2(cos(jitterAngle), sin(jitterAngle));
    float ao = 0.0f;

    [unroll]
    for (uint dirIndex = 0u; dirIndex < NUM_DIRECTIONS; dirIndex++)
    {
        float angle = (2.0f * HBAO_PI * float(dirIndex)) / float(NUM_DIRECTIONS);
        float2 dir = float2(cos(angle), sin(angle));
        dir = float2(dir.x * rot.x - dir.y * rot.y, dir.x * rot.y + dir.y * rot.x);
        float rayPixels = 1.0f + jitterStep * stepPixels;

        [unroll]
        for (uint stepIndex = 0u; stepIndex < NUM_STEPS; stepIndex++)
        {
            int2 samplePixel = int2(p) + int2(round(dir * rayPixels));
            samplePixel = clamp(samplePixel, int2(0, 0), int2(aoSize) - 1);
            uint2 sampleFullPixel = min(uint2(samplePixel) * 2u + 1u, fullSize - 1u);
            ao += ComputeAOContribution(viewPos, normal, ReconstructView(sampleFullPixel));
            rayPixels += stepPixels;
        }
    }

    ao = ao * intensity / float(NUM_DIRECTIONS * NUM_STEPS);
    OutputAO[p] = pow(saturate(1.0f - ao * 2.0f), power);
}
