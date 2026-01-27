#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location=0) in vec3 vWorldPos;
layout(location=1) in vec3 vWorldNrm;
layout(location=2) in float vHeight;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform TerrainPC {
    float time;
    float heightScale;
    float freq;
    float worldScale;
    vec2  mapMin;
    vec2  mapMax;
    vec2  noiseOffset;
    vec2  brushXZ;
    float brushRadius;
    float brushActive;
    float brushDelta;
} pc;

// super cheap hash (1 op style noise)
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// toon ramp: quantize 0..1 into steps
float toonSteps(float x, float steps)
{
    x = clamp(x, 0.0, 1.0);
    return floor(x * steps) / max(steps - 1.0, 1.0);
}

void main()
{
    vec3 N = normalize(vWorldNrm);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);

    // --- lighting setup ---
    vec3 sunDir = normalize(vec3(-0.45, 0.95, 0.25));
    vec3 sunCol = vec3(1.05, 0.98, 0.88);

    float ndl = max(dot(N, sunDir), 0.0);

    // cel banding
    float bands = toonSteps(ndl, 4.0);  // 3-5 looks good
    float shade = mix(0.25, 1.0, bands);

    // slope (0 flat, 1 cliff)
    float slope = 1.0 - clamp(N.y, 0.0, 1.0);

    // height 0..1
    float h01 = clamp(vHeight / max(pc.heightScale, 1e-5), 0.0, 1.0);

    // --- base palette ---
    vec3 grass = vec3(0.12, 0.42, 0.18);
    vec3 dirt  = vec3(0.22, 0.18, 0.12);
    vec3 rock  = vec3(0.35, 0.33, 0.32);
    vec3 snow  = vec3(0.92, 0.95, 1.00);

    // material masks
    float grassMask = smoothstep(0.05, 0.35, h01) * (1.0 - smoothstep(0.35, 0.85, slope));
    float rockMask  = smoothstep(0.35, 0.85, slope);
    float snowMask  = smoothstep(0.70, 0.92, h01);

    vec3 base = dirt;
    base = mix(base, grass, grassMask);
    base = mix(base, rock, rockMask);
    base = mix(base, snow, snowMask);

    // cheap macro variation (no smooth noise)
    float macro = hash12(floor(vWorldPos.xz * 0.08));
    base *= mix(vec3(0.92, 0.95, 0.90), vec3(1.08, 1.02, 0.95), macro);

    // shadow tint (toon style)
    vec3 shadowTint = vec3(0.55, 0.65, 0.90);
    vec3 lit = base * sunCol * shade;
    vec3 shadow = base * shadowTint * 0.35;

    // mix based on banding
    vec3 col = mix(shadow, lit, smoothstep(0.2, 0.6, ndl));

    // rim light (subtle)
    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.0);
    col += base * rim * 0.15;

    // contour lines (cheap, optional)
    float contour = abs(fract(vHeight * 0.25) - 0.5);
    float contourMask = 1.0 - smoothstep(0.03, 0.06, contour);
    col *= mix(1.0, 0.78, contourMask * 0.25);

    // brush visualization (keep it)
    if(pc.brushActive > 0.0)
    {
        float d = length(vWorldPos.xz - pc.brushXZ);
        float ring = abs(d - pc.brushRadius);
        float ringMask = 1.0 - smoothstep(0.0, 0.10, ring);

        vec3 hoverCol = vec3(6.3, 0.9, 1.0);
        vec3 upCol    = vec3(1.2, 1.0, 0.3);
        vec3 downCol  = vec3(1.0, 0.3, 0.2);

        float dirT = clamp(pc.brushDelta * 0.5 + 0.5, 0.0, 1.0);
        vec3 dragCol = mix(downCol, upCol, dirT);
        vec3 brushCol = (pc.brushActive > 0.75) ? dragCol : hoverCol;

        col = mix(col, brushCol, ringMask * 2.75);
    }

    outColor = vec4(col, 1.0);
}
