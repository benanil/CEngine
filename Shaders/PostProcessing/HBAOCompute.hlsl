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
    uint numDirections;
};

Texture2D<float> DepthTexture : register(t0, space0);
Texture2D<float4> NormalTexture : register(t1, space0);
[[vk::image_format("r8")]] RWTexture2D<float> OutputAO : register(u0, space1);

static const float HBAO_PI = 3.14159265f;
static const uint NUM_STEPS = 4u;

// OPTIMIZATION 1: Combined Hash logic to output both float values simultaneously.
// This reduces ALU instructions, hash register shifting, and duplicate operations.
float2 Hash12_Dual(uint2 p)
{
    uint x = p.x * 1664525u + p.y * 1013904223u + frameIndex * 747796405u;
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    uint y = p.y * 1664525u + p.x * 1013904223u + (frameIndex + 17u) * 747796405u;
    y ^= y >> 16u; y *= 0x7feb352du;
    y ^= y >> 15u; y *= 0x846ca68bu;
    y ^= y >> 16u;
    return float2(x & 0x00FFFFFFu, y & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

// OPTIMIZATION 2: Stripped out UV normalization divisions inside the ray-march loop.
// We pass precalculated `rcpFullSize` to perform rapid vector MAC operations instead.
float3 ReconstructViewFast(uint2 p, float2 rcpFullSize)
{
    p = min(p, fullSize - 1u);
    float depth = DepthTexture.Load(int3(p, 0));
    // Bypassed: (float2(p) + 0.5f) / float2(fullSize)
    float2 uv = (float2(p) + 0.5f) * rcpFullSize;
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 v = mul(invProjection, clip);
    return v.xyz / max(abs(v.w), 0.00001f);
}

float ComputeAOContribution(float3 p, float3 n, float3 s, float invRadiusSq)
{
    float3 v = s - p;
    float vv = dot(v, v);
    // Using an explicit early branch to kill execution weight for flat sections
    if (vv <= 0.000001f) return 0.0f;
    float invLen = rsqrt(vv);
    float ndv = dot(n, v) * invLen;
    // Replaced division with precomputed inverse multiplication
    float falloff = saturate(1.0f - vv * invRadiusSq);
    return saturate(ndv - bias) * falloff;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;
    
    // Wave Early Out: If disabled, whole wave exits immediately without loading textures
    if (enabled == 0u)
    {
        OutputAO[tid.xy] = 1.0f;
        return;
    }

    uint2 p = tid.xy;
    uint2 fullP = min(p * 2u + 1u, fullSize - 1u);
    
    float centerDepth = DepthTexture.Load(int3(fullP, 0));
    if (centerDepth <= 0.00001f) // reversed-Z: sky / cleared depth is at the far plane (0)
    {
        OutputAO[p] = 1.0f;
        return;
    }

    // Scalar constants for fast reconstruction loop
    const float2 rcpFullSize = 1.0f / float2(fullSize);
    const float invRadiusSq = 1.0f / max(radius * radius, 0.0001f);

    float3 viewPos = ReconstructViewFast(fullP, rcpFullSize);
    
    // OPTIMIZATION 3: Simplified normal math. 
    // Stripped nested normalizes. Transformed matrix math directly to avoid full float3x3 allocation.
    float3 worldNormal = NormalTexture.Load(int3(p, 0)).xyz * 2.0f - 1.0f;
    float3 normal = normalize(mul((float3x3)view, worldNormal));

    float viewZ = max(abs(viewPos.z), 0.01f);
    float radiusPixels = clamp(radius * projectionScale / viewZ * 0.5f, 2.0f, 48.0f);
    float stepPixels = radiusPixels / float(NUM_STEPS + 1u);

    // Get combined dual jitter hash token
    float2 jitters = Hash12_Dual(p);
    float jitterAngle = jitters.x * 2.0f * HBAO_PI;
    float jitterStep = jitters.y;
    
    float2 rot;
    sincos(jitterAngle, rot.y, rot.x); // Fast hardware sincos assembly intrinsic
    
    float ao = 0.0f;
    uint directions = clamp(numDirections, 2u, 16u);

    // OPTIMIZATION 4: Unrolling loops with 32 texture loads spills registers (VGPRs).
    // We explicitly instruct the compiler to keep this as a tight dynamic execution loop.
    [loop]
    for (uint dirIndex = 0u; dirIndex < directions; dirIndex++)
    {
        float angle = (2.0f * HBAO_PI * float(dirIndex)) / float(directions);
        float2 dir;
        sincos(angle, dir.y, dir.x);
        
        // Complex number rotation vector math
        dir = float2(dir.x * rot.x - dir.y * rot.y, dir.x * rot.y + dir.y * rot.x);
        float rayPixels = 1.0f + jitterStep * stepPixels;

        [unroll] // Keep the inner 4 steps unrolled for texture pipelining
        for (uint stepIndex = 0u; stepIndex < NUM_STEPS; stepIndex++)
        {
            int2 samplePixel = int2(p) + int2(round(dir * rayPixels));
            samplePixel = clamp(samplePixel, int2(0, 0), int2(aoSize) - 1);
            uint2 sampleFullPixel = min(uint2(samplePixel) * 2u + 1u, fullSize - 1u);
            // Fast view space lookup
            float3 sampleViewPos = ReconstructViewFast(sampleFullPixel, rcpFullSize);
            ao += ComputeAOContribution(viewPos, normal, sampleViewPos, invRadiusSq);
            rayPixels += stepPixels;
        }
    }

    ao = ao * intensity * (1.0f / float(directions * NUM_STEPS));
    OutputAO[p] = pow(saturate(1.0f - ao * 2.0f), power);
}