
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
    float opacity;

    float normalScale;
    float foamStrength;

    float specular;
    float specPower;

    vec4  sunDirIntensity;
} pc;

void main()
{
    WaterMaterialGpu mat = mat_buf.materials[vMaterialIndex];

    // ------------------------------------------------------------
    // UVs
    // ------------------------------------------------------------
    vec2 baseUV = vUV * mat.params0.x;

    // ------------------------------------------------------------
    // Normal animation (dominant cost, keep tight)
    // ------------------------------------------------------------
    vec2 nUV1 = baseUV + vec2(pc.time * mat.params0.z,
                              pc.time * mat.params0.z * 0.7);
    vec2 nUV2 = baseUV * 1.7 + vec2(pc.time * mat.params1.x,
                                    pc.time * mat.params1.x * -0.6);

    vec3 n1 = texture(u_textures[nonuniformEXT(mat.textures.x)], nUV1).xyz * 2.0 - 1.0;
    vec3 n2 = texture(u_textures[nonuniformEXT(mat.textures.x)], nUV2).xyz * 2.0 - 1.0;

    vec2 nxy = n1.xy * pc.normalScale + n2.xy * mat.params1.y;
    vec3 N = normalize(vec3(nxy, 1.0));

    // ------------------------------------------------------------
    // Activity-based color (REPLACES Fresnel)
    // ------------------------------------------------------------
    float activity = clamp(length(nxy), 0.0, 1.0);
    float shallowMix = smoothstep(0.2, 0.6, activity);

    vec3 baseColor = mix(
        mat.deepColor.rgb,
        mat.shallowColor.rgb,
        shallowMix
    );

    // ------------------------------------------------------------
    // Lighting (sun only, stable)
    // ------------------------------------------------------------
    vec3 L = normalize(pc.sunDirIntensity.xyz);
    float ndl = max(dot(N, L), 0.0);

    vec3 diffuse = baseColor * (0.35 + 0.65 * ndl);

    // Cheap specular (no half vector)
    vec3 R = reflect(-L, N);
    float spec = pow(max(R.y, 0.0), pc.specPower);
    spec *= pc.specular * pc.sunDirIntensity.w;

    vec3 color = diffuse + spec;

    // ------------------------------------------------------------
    // Foam (visual anchor, overrides everything)
    // ------------------------------------------------------------
    float foam = texture(
        u_textures[nonuniformEXT(mat.textures.y)],
        vUV * mat.params0.y + N.xy * 0.1 + pc.time * 0.05
    ).r;

    foam = smoothstep(0.6, 0.9, foam) * pc.foamStrength;
    color = mix(color, mat.foamColor.rgb, foam);

    // ------------------------------------------------------------
    // Alpha
    // ------------------------------------------------------------
    float alpha = clamp(pc.opacity + foam * 0.35, 0.0, 1.0);
    outColor = vec4(color, alpha);
}





