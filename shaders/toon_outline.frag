#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(set = 1, binding = 0) uniform sampler2D u_textures[];

struct MaterialGpu
{
    uvec4 textures;
    vec4  diffuseFactor;
    vec4  specularFactor;
    vec4  emissiveFactor;
};

layout(set = 0, binding = 4, std430) readonly buffer Materials
{
    MaterialGpu materials[];
} materials_buf;

layout(location = 0) flat in uint in_drawId;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_wpos;
layout(location = 4) flat in uint in_materialIndex;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ToonPC
{
    vec4 lightDirIntensity;    // xyz=direction, w=intensity
    vec4 indirectMinColor;     // rgb=min color, a=multiplier
    vec4 shadowMapColor;       // rgb=shadow color, a=receiveShadowAmount
    vec4 outlineColor;         // rgb=outline color
    vec4 params0;              // x=celMidPoint, y=celSoftness, z=outlineWidth, w=outlineZOffset
    vec4 params1;              // x=useAlphaClip, y=cutoff, z=useEmission, w=emissionMulByBase
    vec4 params2;              // x=useOcclusion, y=occlusionStrength, z=occlusionRemapStart, w=occlusionRemapEnd
    vec4 params3;              // x=isFace, y=outlineZOffsetRemapStart, z=outlineZOffsetRemapEnd, w=unused
} pc;

float invLerpClamp(float from, float to, float value)
{
    return clamp((value - from) / (to - from), 0.0, 1.0);
}

void main()
{
    MaterialGpu mat = materials_buf.materials[in_materialIndex];

    vec4 baseTex = texture(u_textures[nonuniformEXT(mat.textures.x)], in_uv);
    vec4 baseColor = baseTex * mat.diffuseFactor;

    if(pc.params1.x > 0.5 && baseColor.a < pc.params1.y)
        discard;

    float occlusion = 1.0;
    if(pc.params2.x > 0.5)
    {
        float occ = texture(u_textures[nonuniformEXT(mat.textures.z)], in_uv).r;
        occ = mix(1.0, occ, pc.params2.y);
        occlusion = invLerpClamp(pc.params2.z, pc.params2.w, occ);
    }

    vec3 emission = mat.emissiveFactor.rgb;
    if(pc.params1.z > 0.5)
    {
        vec3 emissionMap = texture(u_textures[nonuniformEXT(mat.textures.y)], in_uv).rgb;
        vec3 emissive = emissionMap * mat.emissiveFactor.rgb;
        emission = mix(emissive, emissive * baseColor.rgb, pc.params1.w);
    }

    vec3 indirect = pc.indirectMinColor.rgb;
    indirect *= mix(1.0, occlusion, 0.5) * pc.indirectMinColor.a;

    vec3 N = normalize(in_normal);
    vec3 L = normalize(pc.lightDirIntensity.xyz);
    float NoL = dot(N, L);

    float litOrShadowArea = smoothstep(pc.params0.x - pc.params0.y, pc.params0.x + pc.params0.y, NoL);
    litOrShadowArea *= occlusion;

    if(pc.params3.x > 0.5)
        litOrShadowArea = mix(0.5, 1.0, litOrShadowArea);

    float shadowAttenuation = 1.0;
    litOrShadowArea *= mix(1.0, shadowAttenuation, pc.shadowMapColor.a);

    vec3 litOrShadowColor = mix(pc.shadowMapColor.rgb, vec3(1.0), litOrShadowArea);
    vec3 direct = litOrShadowColor * pc.lightDirIntensity.w;

    vec3 rawLightSum = max(indirect, direct);
    vec3 surfaceColor = baseColor.rgb * rawLightSum + emission;

    vec3 outline = surfaceColor * pc.outlineColor.rgb;
    outColor = vec4(outline, baseColor.a);
}
