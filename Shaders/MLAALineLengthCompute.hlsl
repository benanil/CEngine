#include "MLAACommon.hlsl"

Texture2D<uint> EdgeMaskTexture : register(t0, space0);
[[vk::image_format("rg32ui")]] RWTexture2D<uint2> EdgeCountTexture : register(u0, space1);

// Fast bit-scanning helper to find how far an edge extends
uint FindEdgeDistance(uint edgeMask, int2 startPos, int2 stepDir, uint targetBit)
{
    // If the starting pixel doesn't even have this edge, it's 0 length
    if ((edgeMask & targetBit) == 0u) return 0u;

    uint count = 0u;
    bool edgeFound = true;

    // We keep a tight scalar loop instead of forcing huge unrolled vector registers
    [loop]
    for (int i = 1; i <= int(MLAA_MAX_EDGE_LENGTH); i++)
    {
        int2 scanPos = ClampPixel(startPos + stepDir * i);
        uint nextPixel = EdgeMaskTexture.Load(int3(scanPos, 0));

        // Mask out the bit we care about
        if (edgeFound && ((nextPixel & targetBit) != 0u))
        {
            count++;
        }
        else
        {
            // Edge broke or hit a stop bit. Append the stop bit to our final count.
            // (Assuming MLAA_STOP_BIT format matches your encoding layout)
            return count | MLAA_STOP_BIT;
        }
    }
    return count;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    uint pixel = EdgeMaskTexture.Load(int3(p, 0));
    
    // OPTIMIZATION 1: Global Wave Early Out
    // If no pixels in the current wave have an edge, the entire wave skips memory lookups
    bool anyEdge = (pixel & (MLAA_UPPER_MASK | MLAA_RIGHT_MASK)) != 0u;
    if (!WaveActiveAnyTrue(anyEdge))
    {
        EdgeCountTexture[tid.xy] = uint2(0u, 0u);
        return;
    }

    uint hCountNeg = 0u;
    uint hCountPos = 0u;
    uint vCountPos = 0u;
    uint vCountNeg = 0u;

    // OPTIMIZATION 2: Branching Scalarization
    // Only lanes containing active edges execute the memory walks
    if (anyEdge)
    {
        // Scan Left (-X) & Right (+X) for Upper Edges
        hCountNeg = FindEdgeDistance(pixel, p, int2(-1,  0), MLAA_UPPER_MASK);
        hCountPos = FindEdgeDistance(pixel, p, int2( 1,  0), MLAA_UPPER_MASK);

        // Scan Down (+Y) & Up (-Y) for Right Edges
        vCountPos = FindEdgeDistance(pixel, p, int2( 0,  1), MLAA_RIGHT_MASK);
        vCountNeg = FindEdgeDistance(pixel, p, int2( 0, -1), MLAA_RIGHT_MASK);
    }

    // Pack values exactly as expected by your original EncodeCount function
    uint encodedH = EncodeCount(hCountNeg, hCountPos);
    uint encodedV = EncodeCount(vCountPos, vCountNeg);

    EdgeCountTexture[tid.xy] = uint2(encodedH, encodedV);
}