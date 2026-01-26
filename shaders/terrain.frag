
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

// ------------------------------------------------------------
// Tiny cheap hash noise (no textures needed)
// ------------------------------------------------------------
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float smoothNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash12(i + vec2(0,0));
    float b = hash12(i + vec2(1,0));
    float c = hash12(i + vec2(0,1));
    float d = hash12(i + vec2(1,1));

    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
}

// ------------------------------------------------------------
// Terrain Albedo
// ------------------------------------------------------------
vec3 terrain_albedo(float h, float slope)
{
    vec3 grass = vec3(0.12, 0.42, 0.18);
    vec3 dirt  = vec3(0.22, 0.18, 0.12);
    vec3 rock  = vec3(0.35, 0.33, 0.32);
    vec3 snow  = vec3(0.92, 0.95, 1.00);

    float snowLine  = pc.heightScale * 0.75;
    float grassLine = pc.heightScale * 0.25;

    float grassMask = smoothstep(-1.0, grassLine, h);
    float snowMask  = smoothstep(snowLine, pc.heightScale, h);

    float rockMask = smoothstep(0.35, 0.85, slope);

    vec3 base = mix(dirt, grass, grassMask);
    base = mix(base, rock, rockMask);
    base = mix(base, snow, snowMask);

    return base;
}

// Cheap stylized grass streaks (no trig; distance-fade in main).
float grass_blades(vec2 xz)
{
    vec2 cell = floor(xz * 0.08);
    vec2 dir = normalize(vec2(hash12(cell + 3.1), hash12(cell + 7.7)) * 2.0 - 1.0);

    float along = dot(xz, dir);
    float phase = hash12(cell + 11.7);

    float stripe = abs(fract(along * 9.5 + phase) - 0.5);
    stripe = 1.0 - smoothstep(0.18, 0.45, stripe * 2.0);

    float cluster = smoothNoise(xz * 0.08);
    return stripe * cluster;
}

void main()
{
    vec3 N = normalize(vWorldNrm);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);

    vec2 xz = vWorldPos.xz;

    // ------------------------------------------------------------
    // Lighting setup
    // ------------------------------------------------------------
    vec3 sunDir  = normalize(vec3(-0.45, 0.95, 0.25));
    vec3 sunCol  = vec3(1.05, 0.98, 0.88);

    vec3 fillDir = normalize(vec3(0.6, 0.25, -0.35));
    vec3 fillCol = vec3(0.55, 0.70, 0.95);

    float ndl     = clamp(dot(N, sunDir), 0.0, 1.0);
    float ndlFill = clamp(dot(N, fillDir), 0.0, 1.0);

    // slope: 0 = flat, 1 = vertical cliff
    float slope = 1.0 - clamp(N.y, 0.0, 1.0);

    // ------------------------------------------------------------
    // Reused noise values (reduced samples for performance)
    // ------------------------------------------------------------
    float n_macro = smoothNoise(xz * 0.02);
    float n_var   = smoothNoise(xz * 0.12);
    float n_micro = smoothNoise(xz * 0.35);

    // ------------------------------------------------------------
    // Base albedo
    // ------------------------------------------------------------
    vec3 base = terrain_albedo(vHeight, slope);

    // Slight slope dryness tint
    base *= mix(vec3(1.0), vec3(0.92, 0.90, 0.88), slope * 0.6);

    // ------------------------------------------------------------
    // Grass layer (single system)
    // ------------------------------------------------------------
    float grassFlat = clamp(1.0 - slope, 0.0, 1.0);

    float h01 = clamp(vHeight / max(pc.heightScale, 1e-5), 0.0, 1.0);
    float grassNoSnow = 1.0 - smoothstep(0.65, 0.90, h01);

    float maskBlobs = smoothstep(0.35, 0.85, n_var);

    float grassMask = grassFlat * grassNoSnow * maskBlobs;

    float blades = grass_blades(xz);

    vec3 grassBase = vec3(0.10, 0.32, 0.12);
    vec3 grassDry  = vec3(0.18, 0.28, 0.10);
    vec3 grassCol  = mix(grassBase, grassDry, n_var);

    base = mix(base, base * 0.85 + grassCol * 0.75, grassMask);

    float dist = length(ubo.cameraPos.xyz - vWorldPos);
    float detailFade = clamp(1.0 - dist * 0.02, 0.0, 1.0);

    float grazing = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);
    float bladeLight = blades * grazing * detailFade;
    base += grassCol * bladeLight * grassMask * 0.40;

    // ------------------------------------------------------------
    // Macro color variation
    // ------------------------------------------------------------
    vec3 tintA = vec3(0.90, 1.05, 0.90);
    vec3 tintB = vec3(1.10, 0.95, 0.85);
    vec3 tint  = mix(tintA, tintB, n_macro);

    float flatMask = clamp(1.0 - slope, 0.0, 1.0);
    base *= mix(vec3(1.0), tint, flatMask * 0.65);

    base *= 0.92 + 0.16 * n_micro;

    // ------------------------------------------------------------
    // Contour lines
    // ------------------------------------------------------------
    float contourFreq = 0.25;
    float c = abs(fract(vHeight * contourFreq) - 0.5);
    float contourMask = 1.0 - smoothstep(0.02, 0.05, c);
    base *= mix(1.0, 0.72, contourMask * 0.35);

    // ------------------------------------------------------------
    // Shading
    // ------------------------------------------------------------
    vec3 ambientSky    = vec3(0.55, 0.65, 0.85);
    vec3 ambientGround = vec3(0.22, 0.21, 0.20);
    float hemi         = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientCol    = mix(ambientGround, ambientSky, hemi);

    float ao = mix(0.55, 1.0, pow(1.0 - slope, 0.65));
    ao *= mix(0.80, 1.0, h01);

    vec3 ambient = base * mix(0.18, 0.48, hemi) * ao;

    float wrap = 0.30;
    float diff = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 diffuse = base * diff * 1.15 * sunCol;

    vec3 fill = base * (ndlFill * 0.35) * fillCol;

    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.2);
    vec3 rimLight = base * rim * 0.25;

    vec3 H = normalize(sunDir + V);
    float spec = pow(clamp(dot(N, H), 0.0, 1.0), 64.0);
    spec *= 0.10 * (0.25 + 0.75 * diff);

    vec3 col = ambient + diffuse + fill + rimLight + spec;

    // ------------------------------------------------------------
    // Fog
    // ------------------------------------------------------------
    float fog = exp(-dist * 0.0020);
    col = mix(ambientCol, col, fog);

    // ------------------------------------------------------------
    // Debug
    // ------------------------------------------------------------
    int mode = 0;

    if(mode == 1)
    {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    else if(mode == 2)
    {
        outColor = vec4(base, 1.0);
        return;
    }
    else if(mode == 3)
    {
        outColor = vec4(vec3(h01), 1.0);
        return;
    }
    else if(mode == 4)
    {
        outColor = vec4(vec3(slope), 1.0);
        return;
    }

    // ------------------------------------------------------------
    // Brush visualization
    // ------------------------------------------------------------
    if(pc.brushActive > 0.0)
    {
        float brushDist = length(vWorldPos.xz - pc.brushXZ);
        float innerRing = abs(brushDist - pc.brushRadius * 0.95);
        float outerRing = abs(brushDist - pc.brushRadius);

        float ringWidth = 0.15;
        float ring = smoothstep(ringWidth, 0.0, outerRing);
        float innerLine = smoothstep(ringWidth * 0.5, 0.0, innerRing);

        vec3 hoverCol = vec3(0.3, 0.9, 1.0);
        vec3 upCol    = vec3(0.2, 1.0, 0.3);
        vec3 downCol  = vec3(1.0, 0.3, 0.2);

        float dirT    = clamp(pc.brushDelta * 0.5 + 0.5, 0.0, 1.0);
        vec3 dragCol  = mix(downCol, upCol, dirT);
        vec3 brushCol = (pc.brushActive > 0.75) ? dragCol : hoverCol;

        float fillMask = smoothstep(pc.brushRadius, pc.brushRadius * 0.8, brushDist);
        col = mix(col, col + brushCol * 0.1, fillMask * pc.brushActive);

        col = mix(col, brushCol, ring * 0.6 * pc.brushActive);
        col = mix(col, brushCol * 1.2, innerLine * 0.4 * pc.brushActive);

        float centerDot = smoothstep(0.5, 0.2, brushDist);
        col = mix(col, brushCol, centerDot * 0.8 * pc.brushActive);
    }

    outColor = vec4(col, 1.0);
}






