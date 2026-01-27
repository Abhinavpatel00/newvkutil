#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location=0) in vec3 vWorldPos;
layout(location=1) in vec3 vWorldNrm;
layout(location=2) in float vBladeT;
layout(location=3) in float vRand;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform GrassPC {
    float time;
    float heightScale;
    float freq;
    float worldScale;
    vec2  mapMin;
    vec2  mapMax;
    vec2  noiseOffset;
    float bladeHeight;
    float bladeWidth;
    float windStrength;
    float density;
    float farDistance;
    float pad0;
} pc;


void main()
{
    vec3 N = normalize(vWorldNrm);

    // --------------------------------------------------------
    // Blade gradient (base -> tip)
    // --------------------------------------------------------
    float tip = clamp(vBladeT, 0.0, 1.0);
    tip = tip * tip * (3.0 - 2.0 * tip); // smoothstep without function call

    vec3 baseA = vec3(0.08, 0.32, 0.12); // dark green
    vec3 baseB = vec3(0.12, 0.38, 0.18);
    vec3 tipA  = vec3(0.28, 0.55, 0.22); // lighter tip
    vec3 tipB  = vec3(0.32, 0.62, 0.26);

    vec3 baseCol = mix(baseA, baseB, vRand);
    vec3 tipCol  = mix(tipA,  tipB,  vRand);
    vec3 albedo  = mix(baseCol, tipCol, tip);

    // --------------------------------------------------------
    // Hemisphere lighting (cheap + stable)
    // --------------------------------------------------------
    vec3 skyCol    = vec3(0.60, 0.70, 0.90);
    vec3 groundCol = vec3(0.18, 0.20, 0.16);

    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(groundCol, skyCol, hemi);

    vec3 color = albedo * ambient;

    // --------------------------------------------------------
    // Distance fog (soft, hides LOD crimes)
    // --------------------------------------------------------
    float dist = length(ubo.cameraPos.xyz - vWorldPos);
    float fog = exp(-dist * 0.0025);
    color = mix(ambient * 0.9, color, fog);

    outColor = vec4(color, 1.0);
}
