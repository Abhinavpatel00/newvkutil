#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vUV;
layout(location = 2) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

layout(set = 0, binding = 3) uniform sampler2D uHeightMap;

layout(set = 1, binding = 0) uniform sampler2D u_textures[];

struct WaterMaterialGpu
{
    vec4 shallowColor;
    vec4 deepColor;
    vec4 foamColor;
    vec4 params0;   // x=tiling, y=foamTiling, z=normalSpeed1, w=foamStrength
    vec4 params1;   // x=normalSpeed2, y=normalScale2, z=colorVariation, w=distortion
    uvec4 textures; // x=normal, y=foam, z=noise, w=unused
};

layout(set = 0, binding = 1, std430) readonly buffer WaterMaterials
{
    WaterMaterialGpu materials[];
} mat_buf;

layout(push_constant) uniform WaterPC
{
    float time;
    float heightScale;
    float freq;
    float waterHeight;
    vec2  mapMin;
    vec2  mapMax;
    vec2  noiseOffset;
    float depthFade;
    float foamDistance;
    float foamScale;
    float foamSpeed;
    float normalScale;
    float specular;
    float opacity;
    float fresnelPower;
    float fresnelStrength;
    float specPower;
    float pad0;
    vec4  sunDirIntensity;
} pc;

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
    p = warp_dot(p, 0.6);

    float h = ridged_fbm_dot(p);
    h = pow(h, 1.25);

    vec2 denom = max(pc.mapMax - pc.mapMin, vec2(1e-5));
    vec2 uv = (xz - pc.mapMin) / denom;
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    float hm = texture(uHeightMap, uv).r;

    return h * pc.heightScale + hm;
}

void main()
{
    WaterMaterialGpu mat = mat_buf.materials[vMaterialIndex];

    vec2 baseUV = vUV * mat.params0.x;
    vec2 foamUV = vUV * mat.params0.y;

    float terrainH = terrain_height(vWorldPos.xz);
    float depth = max(pc.waterHeight - terrainH, 0.0);
    float depthT = clamp(depth / max(pc.depthFade, 1e-4), 0.0, 1.0);

    vec3 baseColor = mix(mat.shallowColor.rgb, mat.deepColor.rgb, depthT);

    vec2 nUV1 = baseUV + vec2(pc.time * mat.params0.z, pc.time * mat.params0.z * 0.7);
    vec2 nUV2 = baseUV * 1.7 + vec2(pc.time * mat.params1.x, pc.time * mat.params1.x * -0.6);
    vec3 n1 = texture(u_textures[nonuniformEXT(mat.textures.x)], nUV1).xyz * 2.0 - 1.0;
    vec3 n2 = texture(u_textures[nonuniformEXT(mat.textures.x)], nUV2).xyz * 2.0 - 1.0;
    vec2 nxy = n1.xy * pc.normalScale + n2.xy * mat.params1.y;
    vec3 N = normalize(vec3(nxy, 1.0));

    vec2 foamDistort = nxy * mat.params1.w;
    vec2 foamAnim = foamUV * pc.foamScale + vec2(pc.time * pc.foamSpeed) + foamDistort;
    vec3 foamTex = texture(u_textures[nonuniformEXT(mat.textures.y)], foamAnim).rgb;
    float foamNoise = max(foamTex.r, max(foamTex.g, foamTex.b));
    float foamMask = 1.0 - smoothstep(0.0, pc.foamDistance, depth);
    float foam = smoothstep(0.35, 0.85, foamNoise) * foamMask;
    foam = clamp(foam * mat.params0.w + foamMask * 0.15 * mat.params0.w, 0.0, 1.0);

    float noise = texture(u_textures[nonuniformEXT(mat.textures.z)], baseUV * 0.5 + nxy * 0.25).r;
    baseColor *= mix(1.0 - mat.params1.z, 1.0 + mat.params1.z, noise);

    vec3 L = normalize(pc.sunDirIntensity.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    float ndl = max(dot(N, L), 0.0);
    vec3 diffuse = baseColor * (0.35 + 0.65 * ndl);

    float spec = pow(max(dot(reflect(-L, N), V), 0.0), pc.specPower);
    spec *= pc.specular * pc.sunDirIntensity.w * (0.25 + 0.75 * ndl);

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), pc.fresnelPower) * pc.fresnelStrength;
    vec3 color = diffuse + spec;
    color += fresnel * vec3(0.6, 0.8, 1.0);
    color = mix(color, mat.foamColor.rgb, clamp(foam, 0.0, 1.0));

    float alpha = pc.opacity * (0.4 + 0.6 * depthT) + foam * 0.35;
    alpha = clamp(alpha, 0.0, 1.0);
    outColor = vec4(color, alpha);
}
