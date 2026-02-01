// ============================================================================
// Post-process mega compute shader
// One pass, compile-time toggles, Slang-compatible
// ============================================================================

// ---------------------------------------------------------------------------
// COMPILE-TIME FEATURE TOGGLES
// Recompile to change behavior
// ---------------------------------------------------------------------------

#define ENABLE_TONEMAP
#define ENABLE_COLOR_GRADE
#define ENABLE_VIGNETTE
#define ENABLE_FILM_GRAIN
//#define ENABLE_SHARPEN
//#define ENABLE_CHROM_AB
#define ENABLE_GAMMA

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

[[vk::binding(0, 0)]] Texture2D<float4>   inputImage;
[[vk::binding(1, 0)]] RWTexture2D<float4> outputImage;
[[vk::binding(2, 0)]] SamplerState        linearSampler;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------

struct Params
{
    float2 resolution;
    float  time;
    float  exposure;

    float3 shadows;     float shadowWeight;
    float3 midtones;    float midtoneWeight;
    float3 highlights; float highlightWeight;

    float  saturation;
    float  contrast;
    float  brightness;

    float  vignetteIntensity;
    float  vignetteRadius;
    float  vignetteSoftness;

    float  grainIntensity;
    float  grainSize;

    float  sharpenStrength;
    float  chromaticAberrationStrength;
    float  gamma;
};

[[vk::push_constant]]
Params params;

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------

float luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

float3 tonemapACES(float3 x)
{
    return saturate(
        (x * (2.51 * x + 0.03)) /
        (x * (2.43 * x + 0.59) + 0.14)
    );
}

// ---------------------------------------------------------------------------
// Compute entry
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= params.resolution.x || tid.y >= params.resolution.y)
        return;

    float2 uv = (float2(tid.xy) + 0.5) / params.resolution;
    float2 px = 1.0 / params.resolution;

    float3 color;

    // -----------------------------------------------------------------------
    // SHARPEN
    // -----------------------------------------------------------------------
#ifdef ENABLE_SHARPEN
    {
        float3 c = inputImage.SampleLevel(linearSampler, uv, 0).rgb;
        float3 n = inputImage.SampleLevel(linearSampler, uv + float2(0, -px.y), 0).rgb;
        float3 s = inputImage.SampleLevel(linearSampler, uv + float2(0,  px.y), 0).rgb;
        float3 e = inputImage.SampleLevel(linearSampler, uv + float2( px.x, 0), 0).rgb;
        float3 w = inputImage.SampleLevel(linearSampler, uv + float2(-px.x, 0), 0).rgb;

        float3 edge = c * 4.0 - n - s - e - w;
        color = c + edge * params.sharpenStrength;
    }
#else
    color = inputImage.SampleLevel(linearSampler, uv, 0).rgb;
#endif

    // -----------------------------------------------------------------------
    // CHROMATIC ABERRATION
    // -----------------------------------------------------------------------
#ifdef ENABLE_CHROM_AB
    {
        float2 d = uv - 0.5;
        float k = dot(d, d) * params.chromaticAberrationStrength;
        color = float3(
            inputImage.SampleLevel(linearSampler, uv + d * k, 0).r,
            inputImage.SampleLevel(linearSampler, uv,         0).g,
            inputImage.SampleLevel(linearSampler, uv - d * k, 0).b
        );
    }
#endif

    // -----------------------------------------------------------------------
    // TONEMAP
    // -----------------------------------------------------------------------
#ifdef ENABLE_TONEMAP
    color *= params.exposure;
    color = tonemapACES(color);
#endif

    // -----------------------------------------------------------------------
    // COLOR GRADING
    // -----------------------------------------------------------------------
#ifdef ENABLE_COLOR_GRADE
    {
        float l = luminance(color);

        float sMask = 1.0 - smoothstep(0.0, 0.33, l);
        float hMask = smoothstep(0.55, 1.0, l);
        float mMask = 1.0 - sMask - hMask;

        color += params.shadows    * sMask * params.shadowWeight;
        color *= lerp(1.0.xxx, params.midtones, mMask * params.midtoneWeight);
        color += params.highlights * hMask * params.highlightWeight;

        float g = luminance(color);
        color = lerp(g.xxx, color, params.saturation);
        color = (color - 0.5) * params.contrast + 0.5;
        color += params.brightness;
        color = max(color, 0.0);
    }
#endif

    // -----------------------------------------------------------------------
    // VIGNETTE
    // -----------------------------------------------------------------------
#ifdef ENABLE_VIGNETTE
    {
        float2 d = uv - 0.5;
        float v = 1.0 - smoothstep(
            params.vignetteRadius,
            params.vignetteRadius + params.vignetteSoftness,
            length(d)
        );
        color *= lerp(1.0, v, params.vignetteIntensity);
    }
#endif

    // -----------------------------------------------------------------------
    // FILM GRAIN
    // -----------------------------------------------------------------------
#ifdef ENABLE_FILM_GRAIN
    {
        float n = hash12(uv * params.grainSize + params.time);
        n = (n - 0.5) * params.grainIntensity;
        float m = 1.0 - abs(luminance(color) - 0.5) * 2.0;
        color += n * m;
    }
#endif

    // -----------------------------------------------------------------------
    // GAMMA
    // -----------------------------------------------------------------------
#ifdef ENABLE_GAMMA
    color = pow(max(color, 0.0), 1.0 / params.gamma);
#endif

    outputImage[tid.xy] = float4(color, 1.0);
}
