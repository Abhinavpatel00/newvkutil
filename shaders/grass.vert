#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location=0) out vec3 vWorldPos;
layout(location=1) out vec3 vWorldNrm;
layout(location=2) out float vBladeT;
layout(location=3) out float vRand;

layout(set=0, binding=0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

// Two heightmaps for hybrid terrain
layout(set=1, binding=0) uniform sampler2D uBaseHeight;
layout(set=1, binding=1) uniform sampler2D uSculptDelta;

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

const uint GRASS_GRID = 256u;
const float PI = 3.14159265;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// ------------------------------------------------------------
// Sample final height = baseHeight + sculptDelta
// No procedural noise at runtime!
// ------------------------------------------------------------
float sampleHeight(vec2 xz)
{
    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = clamp((xz - pc.mapMin) / denom, vec2(0.0), vec2(1.0));

    float base = texture(uBaseHeight, uv).r;
    float delta = texture(uSculptDelta, uv).r;
    return base + delta;
}

// Normal from heightmap (4 texture taps, fast)
vec3 heightmapNormal(vec2 xz)
{
    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = clamp((xz - pc.mapMin) / denom, vec2(0.0), vec2(1.0));
    vec2 texel = 1.0 / vec2(textureSize(uBaseHeight, 0));

    float hL = texture(uBaseHeight, uv - vec2(texel.x, 0)).r + texture(uSculptDelta, uv - vec2(texel.x, 0)).r;
    float hR = texture(uBaseHeight, uv + vec2(texel.x, 0)).r + texture(uSculptDelta, uv + vec2(texel.x, 0)).r;
    float hD = texture(uBaseHeight, uv - vec2(0, texel.y)).r + texture(uSculptDelta, uv - vec2(0, texel.y)).r;
    float hU = texture(uBaseHeight, uv + vec2(0, texel.y)).r + texture(uSculptDelta, uv + vec2(0, texel.y)).r;

    float worldStep = (pc.mapMax.x - pc.mapMin.x) * texel.x;

    vec3 N = normalize(vec3(
        -(hR - hL) / (2.0 * worldStep),
        1.0,
        -(hU - hD) / (2.0 * worldStep)
    ));
    return N;
}

    const vec2 blade[6] = vec2[](
        vec2(-0.5, 0.0), vec2( 0.5, 0.0), vec2(-0.08, 1.0),
        vec2( 0.5, 0.0), vec2( 0.08, 1.0), vec2(-0.08, 1.0)
    );


void main()
{
    uint id = uint(gl_InstanceIndex);
    uint gx = id % GRASS_GRID;
    uint gy = id / GRASS_GRID;

    vec2 cell = vec2(gx, gy);
    float r0 = hash12(cell + 1.7);
    float r1 = hash12(cell + 5.3);
    float r2 = hash12(cell + 9.1);

    vec2 jitter = vec2(r0, r1) - 0.5;
    vec2 uv = (vec2(gx, gy) + 0.5 + jitter * 0.35) / float(GRASS_GRID);
    uv = clamp(uv, vec2(0.0), vec2(1.0));

    vec2 worldXZ = mix(pc.mapMin, pc.mapMax, uv);

    float h = sampleHeight(worldXZ);
    vec3  up = heightmapNormal(worldXZ);

    float slopeMask = smoothstep(0.35, 0.80, up.y);

    float densityMask = step(r2, pc.density);

    float bladeH = pc.bladeHeight * mix(0.6, 1.3, r0) * slopeMask * densityMask;

    vec3 base = vec3(worldXZ.x, h, worldXZ.y);
    float dist = length(ubo.cameraPos.xyz - base);
    float fade = clamp(1.0 - dist / pc.farDistance, 0.0, 1.0);
    bladeH *= fade;

    float angle = r1 * PI * 2.0;
    vec2 dir2 = vec2(cos(angle), sin(angle));
    vec3 forward = normalize(vec3(dir2.x, 0.0, dir2.y));
    vec3 side = normalize(cross(up, forward));
    vec3 bendDir = normalize(cross(side, up));


    vec2 b = blade[gl_VertexIndex];
    vBladeT = b.y;

    float wind = sin(pc.time * 1.6 + r0 * 6.28 + worldXZ.x * 0.08 + worldXZ.y * 0.08);
    float bend = wind * pc.windStrength;

    vec3 pos = base;
    pos += side * (b.x * pc.bladeWidth);
    pos += up * (b.y * bladeH);
    pos += bendDir * (b.y * b.y * bend);

    vWorldPos = pos;
    vWorldNrm = normalize(mix(up, bendDir, 0.2));
    vRand = r0;

    gl_Position = ubo.viewproj * vec4(pos, 1.0);
}
