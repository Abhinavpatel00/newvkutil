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
layout(location = 3) in vec4 in_tangent;
layout(location = 4) in vec3 in_wpos;
layout(location = 5) flat in uint in_materialIndex;

layout(location = 0) out vec4 outColor;

void main()
{
    uint texIndex = materials_buf.materials[in_materialIndex].textures.x;

vec4 tex = texture(u_textures[nonuniformEXT(texIndex)], in_uv);
    vec4 diffuse = materials_buf.materials[in_materialIndex].diffuseFactor;
    vec3 n = normalize(in_normal) * 0.5 + 0.5;
    outColor = vec4(n, 1.0) * tex * diffuse;
}
