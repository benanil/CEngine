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

Texture2D<float> AOSource : register(t0, space0);
Texture2D<float> DepthTexture : register(t1, space0);
[[vk::image_format("r8")]] RWTexture2D<float> OutputAO : register(u0, space1);

// Shared memory for 8x8 thread group with a 2-pixel apron on all sides (8 + 2*2 = 12)
groupshared float s_Depth[12][12];
groupshared float s_AO[12][12];

// Helper to safely load data from texture with clamping
void LoadDataToLDS(int2 globalPos, uint2 ldsPos)
{
    globalPos = clamp(globalPos, int2(0, 0), int2(aoSize) - 1);
    s_AO[ldsPos.y][ldsPos.x] = AOSource.Load(int3(globalPos, 0));
    // Depth Load (with original downsampling logic intact)
    uint2 fullP = min(uint2(globalPos) * 2u + 1u, fullSize - 1u);
    s_Depth[ldsPos.y][ldsPos.x] = DepthTexture.Load(int3(fullP, 0));
}

[numthreads(8, 8, 1)]
void main(
    uint3 tid : SV_DispatchThreadID, 
    uint3 gtid : SV_GroupThreadID, 
    uint groupIndex : SV_GroupIndex)
{
    // 1. COLLABORATIVE LOAD INTO GROUPSHARED MEMORY
    // We need to fill a 12x12 area (144 elements) using 64 threads.
    // Each thread loads at least 2 elements, some load 3.
    int2 groupStartPos = int2(tid.xy) - int2(gtid.xy) - 2; // Top-left of apron

    [unroll]
    for (uint i = groupIndex; i < 144; i += 64)
    {
        uint ldsY = i / 12;
        uint ldsX = i % 12;
        int2 loadPos = groupStartPos + int2(ldsX, ldsY);
        LoadDataToLDS(loadPos, uint2(ldsX, ldsY));
    }

    // Synchronize to ensure all threads finished writing to LDS
    GroupMemoryBarrierWithGroupSync();
    // Early out for out-of-bounds threads *after* sync so they still participate in loading
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;
    // 2. BILATERAL BLUR FROM LDS
    uint2 ldsCenter = gtid.xy + 2; // Current thread's position inside LDS
    float centerDepth = s_Depth[ldsCenter.y][ldsCenter.x];
    
    float total = s_AO[ldsCenter.y][ldsCenter.x];
    float weightTotal = 1.0f;
    // Unroll the spatial blur loops
    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            if (x == 0 && y == 0) continue;

            float dist2 = float(x * x + y * y);
            // Read from LDS instead of Texture
            float sampleDepth = s_Depth[ldsCenter.y + y][ldsCenter.x + x];
            float sampleAO    = s_AO[ldsCenter.y + y][ldsCenter.x + x];

            float dd = (sampleDepth - centerDepth) * 80.0f;
            float w = exp2(-dist2 * 0.35f - dd * dd);

            total += sampleAO * w;
            weightTotal += w;
        }
    }

    OutputAO[tid.xy] = total / weightTotal;
}