#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Params
{
    vec2  uResolution;
    float uTime;
} params;

#define PI 3.14159265359

void main()
{
    vec2 uv = gl_FragCoord.xy / params.uResolution;
    outColor = vec4(uv, 0.0, 0.0);
}
