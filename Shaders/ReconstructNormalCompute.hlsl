// Forward+ AO helper: the deferred path feeds HBAO with normals unpacked from the
// G-buffer tangent target, which the forward path never writes. This reconstructs a
// world-space normal from the prepass depth (cross product of neighbouring reconstructed
// positions) into the same rgba8 target HBAO already samples.
Texture2D<float> DepthTexture : register(t0, space0);
SamplerState     DepthSampler : register(s0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputNormal : register(u0, space1);

cbuffer Params : register(b0, space2)
{
    float4x4 invViewProj;
    float4   cameraPosition;
    uint2    fullSize;
    uint2    aoSize;
};

// A 9x9 grid holds exactly enough data for an 8x8 thread group to 
// compute forward-differencing normals without re-fetching neighbors.
groupshared float3 sharedPos[81]; 

float3 ReconstructWorld(uint2 fp)
{
    fp = min(fp, fullSize - 1u);
    float depth = DepthTexture.Load(int3(fp, 0));
    float2 uv = (float2(fp) + 0.5f) / float2(fullSize);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 w = mul(invViewProj, clip);
    return w.xyz / max(abs(w.w), 0.00001f);
}

[numthreads(8, 8, 1)]
void main(
		uint3 tid : SV_DispatchThreadID, 
		uint3 gid : SV_GroupID, 
		uint3 gtid : SV_GroupThreadID, 
		uint gidx : SV_GroupIndex)
{
    // Calculate the base AO-resolution coordinate for this 8x8 group
    uint2 groupBase = gid.xy * 8u;

    // --- PHASE 1: Populate LDS ---
    // All 64 threads fetch 1 position...
    uint2 offset0 = uint2(gidx % 9u, gidx / 9u);
    uint2 fp0 = (groupBase + offset0) * 2u + 1u;
    sharedPos[gidx] = ReconstructWorld(fp0);

    // ...and the first 17 threads fetch a second position to cover the full 9x9 (81) grid.
    if (gidx < 17u)
    {
        uint idx1 = gidx + 64u;
        uint2 offset1 = uint2(idx1 % 9u, idx1 / 9u);
        uint2 fp1 = (groupBase + offset1) * 2u + 1u;
        sharedPos[idx1] = ReconstructWorld(fp1);
    }

    // Wait for all threads to finish projecting & writing to LDS
    GroupMemoryBarrierWithGroupSync();

    // --- PHASE 2: Compute Normals ---
    // IMPORTANT: Early-out bounds checking MUST happen AFTER the barrier, 
    // otherwise edge thread groups will deadlock or leave LDS uninitialized.
    if (tid.x >= aoSize.x || tid.y >= aoSize.y) return;

    // Calculate our thread's index within the local 9x9 LDS array
    uint localIdx = gtid.y * 9u + gtid.x;

    // Read the center, right, and bottom reconstructed positions directly from LDS
    float3 p  = sharedPos[localIdx];       // center
    float3 px = sharedPos[localIdx + 1u];  // center + x
    float3 py = sharedPos[localIdx + 9u];  // center + y

    float3 normal = normalize(cross(px - p, py - p));
    if (dot(normal, cameraPosition.xyz - p) < 0.0f) normal = -normal;

    OutputNormal[tid.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
}