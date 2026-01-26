
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

// Two heightmaps for hybrid terrain:
// baseHeight: baked once on CPU (procedural terrain)
// sculptDelta: user edits via compute (starts at 0)
layout(set=1, binding=0) uniform sampler2D uBaseHeight;
layout(set=1, binding=1) uniform sampler2D uSculptDelta;

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
// Sample final height = baseHeight + sculptDelta
// No procedural noise at runtime - all baked on CPU!
// ------------------------------------------------------------
float sampleHeight(vec2 xz)
{
    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = clamp((xz - pc.mapMin) / denom, vec2(0.0), vec2(1.0));

    float base = texture(uBaseHeight, uv).r;
    float delta = texture(uSculptDelta, uv).r;
    return base + delta;
}

// ------------------------------------------------------------
// Compute normal from final height using 4 texture taps
// Much cheaper than 4x procedural FBM!
// ------------------------------------------------------------
vec3 terrainNormal(vec2 xz)
{
    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = clamp((xz - pc.mapMin) / denom, vec2(0.0), vec2(1.0));
    vec2 texel = 1.0 / vec2(textureSize(uBaseHeight, 0));

    // Sample heights at neighboring texels
    float hL = texture(uBaseHeight, uv - vec2(texel.x, 0)).r + texture(uSculptDelta, uv - vec2(texel.x, 0)).r;
    float hR = texture(uBaseHeight, uv + vec2(texel.x, 0)).r + texture(uSculptDelta, uv + vec2(texel.x, 0)).r;
    float hD = texture(uBaseHeight, uv - vec2(0, texel.y)).r + texture(uSculptDelta, uv - vec2(0, texel.y)).r;
    float hU = texture(uBaseHeight, uv + vec2(0, texel.y)).r + texture(uSculptDelta, uv + vec2(0, texel.y)).r;

    // World-space step size for normal computation
    float worldStep = (pc.mapMax.x - pc.mapMin.x) * texel.x;

    vec3 N = normalize(vec3(
        -(hR - hL) / (2.0 * worldStep),
        1.0,
        -(hU - hD) / (2.0 * worldStep)
    ));
    return N;
}

void main()
{
    vec2 worldXZ = inPos.xz * pc.worldScale;

    // Keep push-constant size consistent across stages (prevents driver trimming)
    float _pc_keep = pc.brushRadius + pc.brushActive + pc.brushDelta + pc.brushXZ.x * 0.0;

    float h = sampleHeight(worldXZ);

    vec3 worldPos = vec3(worldXZ.x, h, worldXZ.y);
    vec3 N = terrainNormal(worldXZ);

    vWorldPos = worldPos;
    vWorldNrm = N;
    vHeight   = h;

    gl_Position = ubo.viewproj * vec4(worldPos, 1.0);
}

