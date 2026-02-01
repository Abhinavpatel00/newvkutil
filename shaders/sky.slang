// ============================================================================
// Ultra-cheap procedural sky
// One file, vertex + fragment
// No textures, no noise buffers, no loops
// Slang -> Vulkan SPIR-V
// ============================================================================

// ----------------------------------------------------------------------------
// Push constants
// ----------------------------------------------------------------------------
struct SkyParams
{
    float3 sunDirection;   // normalized, world-space
    float  sunIntensity;

    float3 skyZenithColor; // top of sky
    float  skyHorizonColor;

    float3 sunColor;
    float  hazeStrength;
    float  exposure;
};

[[vk::push_constant]]
SkyParams params;

// ----------------------------------------------------------------------------
// Vertex output
// ----------------------------------------------------------------------------
struct VSOut
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

// ----------------------------------------------------------------------------
// Vertex shader
// Fullscreen triangle, reconstruct view direction
// ----------------------------------------------------------------------------
[shader("vertex")]
VSOut vsMain(uint vid : SV_VertexID)
{
    // Fullscreen triangle (fewer vertices, fewer problems)
    float2 pos;
    pos.x = (vid == 2) ? 3.0 : -1.0;
    pos.y = (vid == 1) ? 3.0 : -1.0;

    VSOut o;
    o.pos = float4(pos, 0.0, 1.0);

    // Reconstruct view ray assuming clip-space quad
    // Z forward, Y up, X right
    float3 ray = normalize(float3(pos.x, -pos.y, 1.0));
    o.dir = ray;

    return o;
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
float saturate(float x) { return clamp(x, 0.0, 1.0); }
float3 saturate(float3 x) { return clamp(x, 0.0, 1.0); }

// ----------------------------------------------------------------------------
// Atmospheric-ish sky model (cheap on purpose)
// ----------------------------------------------------------------------------
float3 skyGradient(float y)
{
    // y in [-1, 1] -> [0,1]
    float t = saturate(y * 0.5 + 0.5);
    return lerp(params.skyHorizonColor, params.skyZenithColor, t);
}

float sunDisk(float3 dir)
{
    float d = dot(dir, params.sunDirection);
    return saturate((d - 0.999) * 500.0);
}

float sunGlow(float3 dir)
{
    float d = saturate(dot(dir, params.sunDirection));
    return pow(d, 32.0);
}

float horizonHaze(float y)
{
    float h = saturate(1.0 - abs(y));
    return pow(h, 3.0) * params.hazeStrength;
}

// ----------------------------------------------------------------------------
// Fragment shader
// ----------------------------------------------------------------------------
[shader("fragment")]
float4 psMain(VSOut i) : SV_Target
{
    float3 dir = normalize(i.dir);

    // Base sky gradient
    float3 color = skyGradient(dir.y);

    // Sun
    float disk = sunDisk(dir);
    float glow = sunGlow(dir);

    color += params.sunColor * disk * params.sunIntensity;
    color += params.sunColor * glow * params.sunIntensity * 0.25;

    // Horizon haze
    color += horizonHaze(dir.y);

    // Exposure
    color = 1.0 - exp(-color * params.exposure);

    return float4(saturate(color), 1.0);
}
