#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types_int16: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_GOOGLE_include_directive: require
#extension GL_ARB_shader_draw_parameters: require

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

layout(set = 0, binding = 0, std430) readonly buffer DrawCommands
{
    uint drawId[];
} drawCommands;

struct MeshDraw
{
    vec4 position_scale;
    vec4 orientation;
    uvec4 meta;
};

layout(set = 0, binding = 1, std430) readonly buffer Draws
{
    MeshDraw draws[];
} draws;

struct VertexPacked
{
    uint16_t vx;
    uint16_t vy;
    uint16_t vz;
    uint16_t tp;
    uint np;
    uint16_t tu;
    uint16_t tv;
};

layout(set = 0, binding = 2, std430) readonly buffer Vertices
{
    VertexPacked vertices[];
} vb;

layout(location = 0) out flat uint out_drawId;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_normal;
layout(location = 3) out vec3 out_wpos;
layout(location = 4) out flat uint out_materialIndex;

layout(set = 0, binding = 3, std140) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} g;

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

vec3 rotateQuat(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec3 decodeOct(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-v.z, 0);
    v.xy += vec2(v.x >= 0 ? -t : t, v.y >= 0 ? -t : t);
    return normalize(v);
}

void unpackTBN(uint np, uint tp, out vec3 normal, out vec4 tangent)
{
    normal = (vec3(ivec3((uvec3(np) >> uvec3(0,10,20)) & uvec3(1023u))) - 511.0) / 511.0;
    tangent.xyz = decodeOct((vec2(ivec2((uvec2(tp) >> uvec2(0,8)) & uvec2(255u))) - 127.0) / 127.0);
    tangent.w = ((np & (1u << 30)) != 0u) ? -1.0 : 1.0;
}

float invLerpClamp(float from, float to, float value)
{
    return clamp((value - from) / (to - from), 0.0, 1.0);
}

float getCameraFovDeg()
{
    float t = abs(g.proj[1][1]);
    float fov = atan(1.0 / t) * 2.0 * 57.295779513;
    return fov;
}

float getOutlineCameraFovAndDistanceFix(float positionVS_Z)
{
    float cameraMulFix = abs(positionVS_Z);
    cameraMulFix = clamp(cameraMulFix, 0.0, 1.0);
    cameraMulFix *= getCameraFovDeg();
    return cameraMulFix * 0.00005;
}

void main()
{
    uint drawId = drawCommands.drawId[gl_DrawIDARB];
    MeshDraw meshDraw = draws.draws[drawId];

    uint vi = gl_VertexIndex;

    uint vxy = uint(vb.vertices[vi].vx) | (uint(vb.vertices[vi].vy) << 16);
    vec2 pos_xy = unpackHalf2x16(vxy);
    float vz = unpackHalf2x16(uint(vb.vertices[vi].vz)).x;
    vec3 position = vec3(pos_xy, vz);

    uint tuv = uint(vb.vertices[vi].tu) | (uint(vb.vertices[vi].tv) << 16);
    vec2 texcoord = unpackHalf2x16(tuv);

    vec3 normal;
    vec4 tangent;
    unpackTBN(vb.vertices[vi].np, uint(vb.vertices[vi].tp), normal, tangent);

    vec3 normalWS = rotateQuat(normal, meshDraw.orientation);

    vec3 wpos = rotateQuat(position, meshDraw.orientation) * meshDraw.position_scale.w + meshDraw.position_scale.xyz;

    vec4 viewPos = g.view * vec4(wpos, 1.0);

    if(pc.params0.z > 0.0)
    {
        float outlineExpand = pc.params0.z * getOutlineCameraFovAndDistanceFix(viewPos.z);
        wpos += normalWS * outlineExpand;
        viewPos = g.view * vec4(wpos, 1.0);

        uint maskIndex = materials_buf.materials[meshDraw.meta.z].textures.w;
        float mask = textureLod(u_textures[nonuniformEXT(maskIndex)], texcoord, 0.0).r;
        mask = 1.0 - mask;
        mask = invLerpClamp(pc.params3.y, pc.params3.z, mask);

        viewPos.z += pc.params0.w * mask + 0.03 * pc.params3.x;
    }

    gl_Position = g.proj * viewPos;

    out_drawId  = drawId;
    out_uv      = texcoord;
    out_normal  = normalWS;
    out_wpos    = wpos;
    out_materialIndex = meshDraw.meta.z;
}
