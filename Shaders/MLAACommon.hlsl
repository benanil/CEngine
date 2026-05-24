#ifndef MLAA_COMMON_H
#define MLAA_COMMON_H

#define MLAA_COUNT_BITS 4u
#define MLAA_MAX_EDGE_LENGTH ((1u << (MLAA_COUNT_BITS - 1u)) - 1u)

#define MLAA_UPPER_MASK (1u << 0u)
#define MLAA_RIGHT_MASK (1u << 1u)
#define MLAA_STOP_BIT (1u << (MLAA_COUNT_BITS - 1u))
#define MLAA_STOP_BIT_POSITION (MLAA_COUNT_BITS - 1u)
#define MLAA_NEG_COUNT_SHIFT MLAA_COUNT_BITS
#define MLAA_POS_COUNT_SHIFT 0u
#define MLAA_COUNT_SHIFT_MASK ((1u << MLAA_COUNT_BITS) - 1u)

cbuffer MLAAParams : register(b0, space2)
{
    uint2 outputSize;
    float threshold;
    uint showEdges;
};

int2 ClampPixel(int2 p)
{
    return clamp(p, int2(0, 0), int2(outputSize) - 1);
}

float SceneLuma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

bool CompareLuma(float a, float b)
{
    return abs(a - b) > threshold;
}

uint RemoveStopBit(uint count)
{
    return count & (MLAA_STOP_BIT - 1u);
}

uint DecodeCountNoStopBit(uint count, uint shift)
{
    return RemoveStopBit((count >> shift) & MLAA_COUNT_SHIFT_MASK);
}

uint EncodeCount(uint negCount, uint posCount)
{
    return ((negCount & MLAA_COUNT_SHIFT_MASK) << MLAA_NEG_COUNT_SHIFT) | (posCount & MLAA_COUNT_SHIFT_MASK);
}

bool IsBitSet(uint value, uint bitPosition)
{
    return (value & (1u << bitPosition)) != 0u;
}

#endif
