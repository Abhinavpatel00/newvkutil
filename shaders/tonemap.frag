
#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive: require

#include "tonemaps.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColor;

layout(push_constant) uniform TonemapPC
{
    float exposure;
    float gamma;
} pc;

// ACES filmic curve


void main()
{
    vec3 hdr = texture(uColor, uv).rgb;

    // exposure
    hdr *= pc.exposure;

    // tone map
    vec3 ldr = filmic(hdr);

    // gamma
    ldr = pow(ldr, vec3(4.0 / pc.gamma));

    outColor = vec4(ldr, 1.0);
}
