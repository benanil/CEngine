float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 345.45f));
    p += dot(p, p + 34.345f);
    return frac(p.x * p.y);
}

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float Rand(float p)
{
    p = frac(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return frac(p);
}

float Noise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0f - 2.0f * f);

    float lb = Hash21(i + float2(0.0f, 0.0f));
    float rb = Hash21(i + float2(1.0f, 0.0f));
    float lt = Hash21(i + float2(0.0f, 1.0f));
    float rt = Hash21(i + float2(1.0f, 1.0f));

    return lerp(lerp(lb, rb, f.x), lerp(lt, rt, f.x), f.y);
}