#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColor;
layout(set = 0, binding = 1) uniform sampler2D uDepth;

layout(push_constant) uniform DOFPC
{
    float focalDistance;   // meters
    float focalLength;     // meters (e.g. 0.05 = 50mm)
    float cocScale;        // focalLength^2 / (sensorHeight * fstop)
    float maxCoC;          // max blur radius in pixels
    float zNear;           // reverse-Z near
    float maxDepth;        // clamp far depth
} pc;

const float EPS = 1e-6;
// Reverse-Z, infinite far
float linearDepthReverseZ(float d)
{
    return pc.zNear / max(d, EPS);
}
float circleOfConfusionPx(float z)
{
    float focus = pc.focalDistance;
    float f     = pc.focalLength;

    float coc = pc.cocScale * abs(z - focus)
              / (z * max(focus - f, EPS));

    float screenHeight = float(textureSize(uColor, 0).y);
    return clamp(coc * screenHeight, 0.0, pc.maxCoC);
}


float gaussian(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma));
}


void main()
{
    ivec2 size = textureSize(uColor, 0);
    vec2 texel = 1.0 / vec2(size);

    float rawDepth = texture(uDepth, uv).r;
    float z = min(linearDepthReverseZ(rawDepth), pc.maxDepth);

    float cocPx = circleOfConfusionPx(z);

    // In focus → no blur
    if(cocPx < 0.5)
    {
        outColor = texture(uColor, uv);
        return;
    }

    int radius = int(round(cocPx * 0.5));
    radius = clamp(radius, 1, int(pc.maxCoC));

    float sigma = max(float(radius) * 0.5, 1.0);

    vec3 sum = vec3(0.0);
    float wsum = 0.0;

    // 2D Gaussian (small radius only)
    for(int y = -radius; y <= radius; y++)
    {
        for(int x = -radius; x <= radius; x++)
        {
            float w = gaussian(length(vec2(x, y)), sigma);
            vec2 offset = vec2(x, y) * texel;
            sum += texture(uColor, uv + offset).rgb * w;
            wsum += w;
        }
    }

    vec3 color = sum / max(wsum, EPS);
    outColor = vec4(color, 1.0);
}


