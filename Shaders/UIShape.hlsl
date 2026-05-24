#include "PBR.hlsl"

struct UIShape
{
    float4 rect;
    float4 params; // x: radius, y: border, z: softness, w: unused
    uint color;
    uint borderColor;
    uint shape;
    uint flags;
    float4 clip;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float2 halfSize : TEXCOORD1;
    nointerpolation float4 params : TEXCOORD2;
    nointerpolation uint color : COLOR0;
    nointerpolation uint borderColor : COLOR1;
    nointerpolation uint shape : TEXCOORD3;
    nointerpolation float4 clip : TEXCOORD4;
};

cbuffer ui_params : register(b0, space1)
{
    float4 ui_screen_scale;
};

StructuredBuffer<UIShape> ShapeBuffer : register(t0);

// Unified, branchless SDF for Box, Rounded Box, and Circle
float EvaluateSdf(float2 p, float2 halfSize, float radius)
{
    // Clamp radius to ensure it never exceeds the half-dimensions
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 d = abs(p) - (halfSize - radius);
    float outsideDist = length(max(d, 0.0f));
    float insideDist = min(max(d.x, d.y), 0.0f);
    return outsideDist + insideDist - radius;
}

VSOutput vert(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    UIShape shape = ShapeBuffer[instanceID];
    // Generate corner coordinates branchlessly using bits: (0,0), (0,1), (1,0), (1,0), (0,1), (1,1)
    float2 uv = float2((vertexID == 2u || vertexID == 3u || vertexID == 5u), 
                       (vertexID == 1u || vertexID == 4u || vertexID == 5u));

    float2 halfSize = shape.rect.zw * 0.5f;
    // Padding calculation for AA/Softness expansion
    float pad = max(shape.params.y + shape.params.z, 1.0f);
    float2 expandedMin = shape.rect.xy - pad;
    float2 expandedSize = shape.rect.zw + pad * 2.0f;
    float2 pixel = expandedMin + expandedSize * uv;
    float2 center = shape.rect.xy + halfSize;
    // Compute NDC positions
    float2 clip = float2(pixel.x / ui_screen_scale.x * 2.0f - 1.0f, 1.0f - pixel.y / ui_screen_scale.y * 2.0f);

    VSOutput o;
    o.position = float4(clip, 0.0f, 1.0f);
    o.local = pixel - center;
    o.halfSize = halfSize;
    o.params = shape.params;
    o.color = shape.color;
    o.borderColor = shape.borderColor;
    o.shape = shape.shape;
    o.clip = shape.clip;
    return o;
}

float4 frag(VSOutput input) : SV_Target0
{
    if (input.position.x < input.clip.x || input.position.y < input.clip.y || input.position.x > input.clip.z || input.position.y > input.clip.w) discard;

    float radius = input.params.x;
    float border = input.params.y;
    float softness = max(input.params.z, 0.0f);
    // Unify shape type overrides branchlessly into a target radius calculation
    // shape 0 = Rect (r=0), shape 1 = Rounded Rect (r=radius), shape 2 & 3 = Circle/Max Rounded Rect
    float maxRadius = min(input.halfSize.x, input.halfSize.y);
    float targetRadius = (input.shape == 0u) ? 0.0f : ((input.shape >= 2u) ? maxRadius : radius);

    // Compute Outer Edge SDF
    float sd = EvaluateSdf(input.local, input.halfSize, targetRadius);
    // Determine screen-space antialiasing width
    float aa = max(fwidth(sd), softness);
    float fillCoverage = saturate(0.5f - sd / aa);

    // Compute Inner Edge SDF (For Border Rendering)
    // If border is 0, innerHalf matches halfSize, innerSd matches sd, and innerCoverage will be 1.0
    float2 innerHalf = max(input.halfSize - border, 0.0f);
    float innerRadius = max(targetRadius - border, 0.0f);
    float innerSd = EvaluateSdf(input.local, innerHalf, innerRadius);
    float innerCoverage = saturate(0.5f - innerSd / aa);

    // Unpack colors
    float4 fill   = UnpackColor4Uint(input.color);
    float4 stroke = UnpackColor4Uint(input.borderColor);
    // Mix border and background branchlessly
    // If border == 0, innerCoverage is forced to 1.0, yielding pure 'fill'
    float4 color = lerp(stroke, fill, innerCoverage);
    // Apply composite coverage and execute premultiplied alpha
    color.a *= fillCoverage;
    color.rgb *= color.a;
    return color;
}
