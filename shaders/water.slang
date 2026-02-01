// ============================================================================
// water.slang
// Single file: vertex + fragment
// Bindless textures, Vulkan-style, Slang-compatible
// No Fresnel, activity-based water coloring (same as your GLSL)
// ============================================================================

// ----------------------------------------------------------------------------
// Global UBO
// ----------------------------------------------------------------------------
struct GlobalUBO
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4   cameraPos;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GlobalUBO> ubo;

// ----------------------------------------------------------------------------
// Water material
// ----------------------------------------------------------------------------
struct WaterMaterialGpu
{
    float4 shallowColor;
    float4 deepColor;
    float4 foamColor;

    float4 params0;   // x=tiling, y=foamTiling, z=normalSpeed1, w=foamStrength
    float4 params1;   // x=normalSpeed2, y=normalScale2, z=unused, w=unused

    uint4  textures;  // x=normalMap, y=foamMap
};

[[vk::binding(1, 0)]]
StructuredBuffer<WaterMaterialGpu> mat_buf;

// ----------------------------------------------------------------------------
// Instance data
// ----------------------------------------------------------------------------
struct WaterInstance
{
    float4x4 model;
    uint     materialIndex;
};

[[vk::binding(2, 0)]]
StructuredBuffer<WaterInstance> inst_buf;

// ----------------------------------------------------------------------------
// Bindless textures + separate sampler (IMPORTANT)
// ----------------------------------------------------------------------------
[[vk::binding(0, 1)]]
Texture2D<float4> u_textures[];

[[vk::binding(1, 1)]]
SamplerState linearSampler;

// ----------------------------------------------------------------------------
// Push constants
// ----------------------------------------------------------------------------
struct WaterPC
{
    float time;
    float opacity;

    float normalScale;
    float foamStrength;

    float specular;
    float specPower;

    float3 sunDir;
    float  sunIntensity;
};

[[vk::push_constant]]
WaterPC pc;

// ----------------------------------------------------------------------------
// Vertex output
// ----------------------------------------------------------------------------
struct VSOut
{
    float4 pos      : SV_Position;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    uint   matIndex : TEXCOORD2;
};

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
float saturate(float x) { return clamp(x, 0.0, 1.0); }

// ----------------------------------------------------------------------------
// Vertex shader
// ----------------------------------------------------------------------------
[shader("vertex")]
VSOut vsMain(
    float3 inPos : POSITION,
    float2 inUV  : TEXCOORD0,
    uint   iid   : SV_InstanceID
)
{
    WaterInstance inst = inst_buf[iid];

    float4 world = mul(inst.model, float4(inPos, 1.0));

    VSOut o;
    o.worldPos = world.xyz;
    o.uv       = inUV;
    o.matIndex = inst.materialIndex;
    o.pos      = mul(ubo.viewproj, world);

    return o;
}

// ----------------------------------------------------------------------------
// Fragment shader
// ----------------------------------------------------------------------------
[shader("fragment")]
float4 psMain(VSOut i) : SV_Target
{
    WaterMaterialGpu mat = mat_buf[i.matIndex];

    // ------------------------------------------------------------
    // UVs
    // ------------------------------------------------------------
    float2 baseUV = i.uv * mat.params0.x;

    // ------------------------------------------------------------
    // Animated normals (dominant cost, unchanged)
    // ------------------------------------------------------------
    float2 uv1 = baseUV + pc.time * mat.params0.z * float2(1.0, 0.7);
    float2 uv2 = baseUV * 1.7 + pc.time * mat.params1.x * float2(1.0, -0.6);

    float3 n1 = u_textures[NonUniformResourceIndex(mat.textures.x)]
                    .Sample(linearSampler, uv1).xyz * 2.0 - 1.0;
    float3 n2 = u_textures[NonUniformResourceIndex(mat.textures.x)]
                    .Sample(linearSampler, uv2).xyz * 2.0 - 1.0;

    float2 nxy = n1.xy * pc.normalScale + n2.xy * mat.params1.y;
    float3 N   = normalize(float3(nxy, 1.0));

    // ------------------------------------------------------------
    // Activity-based water color (NO Fresnel, same as GLSL)
    // ------------------------------------------------------------
    float activity = saturate(length(nxy));
    float mixT = smoothstep(0.2, 0.6, activity);

    float3 baseColor =
        lerp(mat.deepColor.rgb, mat.shallowColor.rgb, mixT);

    // ------------------------------------------------------------
    // Lighting (sun only, stable, cheap)
    // ------------------------------------------------------------
    float3 L = normalize(pc.sunDir);
    float ndl = saturate(dot(N, L));

    float3 diffuse = baseColor * (0.35 + 0.65 * ndl);

    // Cheap specular (reflect, no half vector)
    float3 R = reflect(-L, N);
    float spec = pow(saturate(R.y), pc.specPower);
    spec *= pc.specular * pc.sunIntensity;

    float3 color = diffuse + spec;

    // ------------------------------------------------------------
    // Foam (same logic as GLSL)
    // ------------------------------------------------------------
    float foam = u_textures[NonUniformResourceIndex(mat.textures.y)].Sample(
        linearSampler,
        i.uv * mat.params0.y + N.xy * 0.1 + pc.time * 0.05
    ).r;

    foam = smoothstep(0.6, 0.9, foam) * pc.foamStrength;
    color = lerp(color, mat.foamColor.rgb, foam);

    // ------------------------------------------------------------
    // Alpha
    // ------------------------------------------------------------
    float alpha = saturate(pc.opacity + foam * 0.35);

    return float4(color, alpha);
}
