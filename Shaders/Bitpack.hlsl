#ifndef BITPACK_HLSL
#define BITPACK_HLSL

#include "Common.hlsl"

fp16_3 UnpackVec3XY11Z10Snorm(uint packed) {
    const fp16 scale11 = fp16(1.0 / 1023.0);
    const fp16 scale10 = fp16(1.0 / 511.0);
    return fp16_3(
        fp16((int)(packed << 21) >> 21) * scale11,
        fp16((int)(packed << 10) >> 21) * scale11,
        fp16((int)(packed      ) >> 22) * scale10
    );
}

fp16_2 unpackHalf2x16(uint packed) {
    #if INT16_SUPPORTED
    return asfloat16(uint16_t2(u16(packed), u16(packed >> 16)));
    #else
    return fp16_2(f16tof32(packed), f16tof32(packed >> 16));
    #endif
}

fp16_4 UnpackRGBA16Snorm(uint xy, uint zw) {
    return fp16_4(
        fp16(int(xy << 16u) >> 16),
        fp16(int(xy)        >> 16),
        fp16(int(zw << 16u) >> 16),
        fp16(int(zw)        >> 16)
    ) * fp16(1.0 / 32767.0);
}

fp16_3 UnpackVec3XY11Z10Unorm(uint packed) {
    const fp16 scale11 = fp16(1.0 / 2047.0);
    const fp16 scale10 = fp16(1.0 / 1023.0);
    return fp16_3(
        fp16( packed        & 0x7FFu) * scale11,
        fp16((packed >> 11) & 0x7FFu) * scale11,
        fp16( packed >> 22          ) * scale10
    );
}

fp16_3 OctDecode(fp16_2 f)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    fp16_3 n = fp16_3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    fp16  t  = saturate(-n.z);
    n.xy    += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

fp16_2 DecodeDiamond(fp16 p)
{
    fp16   p_sign = fp16(sign(p - fp16(0.5)));
    fp16_2 v;
    v.x = mad(p_sign * fp16(-4.0), p, fp16(1.0) + p_sign * fp16(2.0));
    v.y = p_sign * (fp16(1.0) - abs(v.x));
    return normalize(v);
}

// https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/
fp16_3 DecodeTangent(fp16_3 normal, fp16 diamond_tangent)
{
    fp16_3 t1_a = normalize(fp16_3( normal.y, -normal.x, fp16(0.0)));
    fp16_3 t1_b = normalize(fp16_3( normal.z,  fp16(0.0), -normal.x));
    fp16_3 t1   = select(abs(normal.y) > abs(normal.z), t1_a, t1_b);

    fp16_2 packed_tangent = DecodeDiamond(diamond_tangent);
    return packed_tangent.x * t1 + packed_tangent.y * cross(t1, normal);
}

void UnpackNormalTangent(uint packed, out fp16_3 normal, out fp16_3 tangent)
{
    fp16_3 oct    = UnpackVec3XY11Z10Snorm(packed);
    normal        = OctDecode(oct.xy);
    tangent       = DecodeTangent(normal, mad(oct.z, fp16(0.5), fp16(0.5)));
}

#endif