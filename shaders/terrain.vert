
#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNrm;
layout(location=2) in vec2 inUV;

layout(location=0) out vec3 vWorldPos;
layout(location=1) out vec3 vWorldNrm;
layout(location=2) out float vHeight;

layout(set=0, binding=0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

layout(set=1, binding=0) uniform sampler2D uHeightMap;

layout(push_constant) uniform TerrainPC {
    float time;
    float heightScale;   // e.g. 20.0
    float freq;          // e.g. 0.02
    float worldScale;    // e.g. 1.0
    vec2  mapMin;        // world min xz
    vec2  mapMax;        // world max xz
    vec2  noiseOffset;   // procedural seed offset
    vec2  brushXZ;       // brush world position
    float brushRadius;   // brush radius
    float brushActive;   // 1.0 if sculpting, 0.5 if hovering, 0.0 if off
    float brushDelta;    // -1..1 direction visualization
} pc;

// ------------------------------------------------------------
// Your dot-noise
// ------------------------------------------------------------
const float PHI = 1.618033988;
const mat3 GOLD = mat3(
    -0.571464913, +0.814921382, +0.096597072,
    -0.278044873, -0.303026659, +0.911518454,
    +0.772087367, +0.494042493, +0.399753815
);

float dot_noise(vec3 p)
{
    return dot(cos(GOLD * p), sin(PHI * p * GOLD)); // ~[-3..+3]
}

float dot_noise11(vec3 p)
{
    return clamp(dot_noise(p) * (1.0/3.0), -1.0, 1.0);
}

float fbm_dot(vec3 p)
{
    float sum = 0.0;
    float amp = 0.5;
    float f   = 1.0;

    for(int i = 0; i < 5; i++)
    {
        sum += amp * dot_noise11(p * f);
        f *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

float ridged_fbm_dot(vec3 p)
{
    float sum = 0.0;
    float amp = 0.5;
    float f   = 1.0;

    for(int i = 0; i < 5; i++)
    {
        float n = dot_noise11(p * f);
        n = 1.0 - abs(n);
        n *= n;
        sum += amp * n;

        f *= 2.0;
        amp *= 0.5;
    }
    return sum; // [0..~1]
}

vec3 warp_dot(vec3 p, float strength)
{
    float wx = fbm_dot(p + vec3(17.1,  3.2, 11.7));
    float wy = fbm_dot(p + vec3( 5.4, 19.3,  7.1));
    float wz = fbm_dot(p + vec3(13.7,  9.2, 21.4));
    return p + strength * vec3(wx, wy, wz);
}

float terrain_height(vec2 xz)
{
    vec3 p = vec3(xz + pc.noiseOffset, 0.0) * pc.freq;

    // mild warp so it doesn't look like a cheap carpet
    p = warp_dot(p, 0.6);

    float h = ridged_fbm_dot(p);     // [0..1]
    h = pow(h, 1.25);                // shape

    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = (xz - pc.mapMin) / denom;
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    float hm = texture(uHeightMap, uv).r;

    return h * pc.heightScale + hm;
}

vec3 terrain_normal(vec2 xz)
{
    // normal sampling step in world units



float eps = 0.25 * pc.worldScale; // instead of 1.0
    float hL = terrain_height(xz + vec2(-eps, 0.0));
    float hR = terrain_height(xz + vec2( eps, 0.0));
    float hD = terrain_height(xz + vec2(0.0, -eps));
    float hU = terrain_height(xz + vec2(0.0,  eps));

    vec3 dx = vec3(2.0 * eps, hR - hL, 0.0);
    vec3 dz = vec3(0.0, hU - hD, 2.0 * eps);

    return normalize(cross(dz, dx));
}

void main()
{

 
vec2 worldXZ = inPos.xz * pc.worldScale;
//Sometimes shader compilers optimize away unused push constant members, and reflection tools can miss them if you build layouts from reflection.
// Keep push-constant size consistent across stages (prevents driver trimming)
float _pc_keep = pc.brushRadius + pc.brushActive + pc.brushDelta + pc.brushXZ.x * 0.0;

float h = terrain_height(worldXZ);

    vec3 worldPos = vec3(worldXZ.x, h, worldXZ.y);
    vec3 N = terrain_normal(worldXZ);

    vWorldPos = worldPos;
    vWorldNrm = N;
    vHeight   = h;

    gl_Position = ubo.viewproj * vec4(worldPos, 1.0);
}

