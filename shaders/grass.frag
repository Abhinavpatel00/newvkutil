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
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 N = normalize(vWorldNrm);
    if(dot(N, V) < 0.0)
        N = -N;

    vec3 sunDir  = normalize(vec3(-0.45, 0.95, 0.25));
    vec3 sunCol  = vec3(1.05, 0.98, 0.88);
    vec3 fillDir = normalize(vec3(0.6, 0.25, -0.35));
    vec3 fillCol = vec3(0.55, 0.70, 0.95);

    float ndl = clamp(dot(N, sunDir), 0.0, 1.0);
    float ndlFill = clamp(dot(N, fillDir), 0.0, 1.0);

    float tip = pow(clamp(vBladeT, 0.0, 1.0), 1.25);

    vec3 baseA = vec3(0.08, 0.26, 0.12);
    vec3 baseB = vec3(0.14, 0.34, 0.16);
    vec3 tipA  = vec3(0.22, 0.58, 0.28);
    vec3 tipB  = vec3(0.32, 0.78, 0.34);

    vec3 baseCol = mix(baseA, baseB, vRand);
    vec3 tipCol  = mix(tipA,  tipB,  vRand);
    vec3 albedo  = mix(baseCol, tipCol, tip);

    float wrap = 0.25;
    float diff = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);

    float back = clamp(dot(-N, sunDir), 0.0, 1.0);
    vec3 sss = albedo * back * 0.35;

    vec3 ambientSky    = vec3(0.55, 0.65, 0.85);
    vec3 ambientGround = vec3(0.20, 0.20, 0.18);
    float hemi         = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientCol    = mix(ambientGround, ambientSky, hemi);

    vec3 ambient = albedo * mix(0.18, 0.45, hemi);
    vec3 diffuse = albedo * diff * 1.15 * sunCol;
    vec3 fill    = albedo * ndlFill * 0.35 * fillCol;

    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.0);
    vec3 rimLight = albedo * rim * 0.35;

    vec3 col = ambient + diffuse + fill + rimLight + sss;

    float dist = length(ubo.cameraPos.xyz - vWorldPos);
    float fog = exp(-dist * 0.0025);
    col = mix(ambientCol, col, fog);

    outColor = vec4(col, 1.0);
}
