// color picker texture: saturation/value field with the hue strip at the bottom and
// the current selection indicator, ported from the old engine's ColorPickFrag.glsl
cbuffer ColorPickParams : register(b0, space2)
{
    float3 uHSV;
    float  uPad0;
    float2 uSize;
    float2 uPad1;
};

[[vk::image_format("rgba8")]] RWTexture2D<float4> Result : register(u0, space1);

float3 hsv2rgb(float3 c)
{
    c.z = 1.0 - c.z;
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float3 hue2rgb(float h)
{
    float r = abs(h * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(h * 6.0 - 2.0);
    float b = 2.0 - abs(h * 6.0 - 4.0);
    return clamp(float3(r, g, b), 0.0, 1.0);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= (uint)uSize.x || tid.y >= (uint)uSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5) / uSize;
    float3 hsv = uHSV;
    float4 result = float4(((1.0 - uv.y)).xxx, 1.0);

    if (uv.y > 0.88)
    {
        result.rgb = hue2rgb(uv.x);
    }
    else if (uv.x < 0.88)
    {
        result.rgb = hsv2rgb(float3(hsv.x, uv));

        // selection indicator circle, same constants as the original shader
        float2 aspect = uSize / max(uSize.x, uSize.y);
        float2 suv = uv * 0.88 * aspect;
        hsv.z = 1.0 - hsv.z;
        hsv.z *= 0.8 * aspect.y;
        hsv.y *= 0.78 * aspect.x;
        float dst = distance(suv, hsv.yz);
        if (dst < 0.025)
        {
            float intensity = 1.0 - (dst * 40.0); // 40 is 1/0.025
            result = lerp(result, float4(hsv.yyy, 1.0), intensity);
        }
    }

    result.a = 1.0;
    Result[tid.xy] = result;
}
