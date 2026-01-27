
#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColor;

layout(push_constant) uniform TonemapPC
{
    float exposure;
    float gamma;
} pc;

// ACES filmic curve
vec3 aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uColor, uv).rgb;

    // exposure
    hdr *= pc.exposure;

    // tone map
    vec3 ldr = aces(hdr);

    // gamma
    ldr = pow(ldr, vec3(1.0 / pc.gamma));

    outColor = vec4(ldr, 1.0);
}
