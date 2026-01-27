#version 450
#extension GL_GOOGLE_include_directive: require

#include "coord.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vUV;
layout(location = 2) flat out uint vMaterialIndex;

layout(set = 0, binding = 0) uniform GlobalUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} ubo;

struct WaterInstanceGpu
{
    mat4 model;
    uint materialIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(set = 0, binding = 2, std430) readonly buffer WaterInstances
{
    WaterInstanceGpu instances[];
} inst_buf;

void main()
{
    WaterInstanceGpu inst = inst_buf.instances[gl_InstanceIndex];

    vec4 world = inst.model * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    vUV = inUV;
    vMaterialIndex = inst.materialIndex;

    gl_Position = ubo.viewproj * world;
}
