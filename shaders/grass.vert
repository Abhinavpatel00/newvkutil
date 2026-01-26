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

layout(set=1, binding=0) uniform sampler2D uHeightMap;

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

// Dot-noise from terrain
const float PHI = 1.618033988;
const mat3 GOLD = mat3(
    -0.571464913, +0.814921382, +0.096597072,
    -0.278044873, -0.303026659, +0.911518454,
    +0.772087367, +0.494042493, +0.399753815
);

float dot_noise(vec3 p)
{
    return dot(cos(GOLD * p), sin(PHI * p * GOLD));
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
    return sum;
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
    p = warp_dot(p, 0.7);

    float h = ridged_fbm_dot(p);
    h = pow(h, 1.25);

    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = (xz - pc.mapMin) / denom;
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    float hm = texture(uHeightMap, uv).r;

    return h * pc.heightScale + hm;
}

vec3 terrain_normal(vec2 xz)
{
    float eps = 0.25 * pc.worldScale;
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

    float h = terrain_height(worldXZ);
    vec3  up = terrain_normal(worldXZ);

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

    const vec2 blade[6] = vec2[](
        vec2(-0.5, 0.0), vec2( 0.5, 0.0), vec2(-0.08, 1.0),
        vec2( 0.5, 0.0), vec2( 0.08, 1.0), vec2(-0.08, 1.0)
    );

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

    gl_Position = ubo.viewproj * vec4(pos, 0.0);
}
