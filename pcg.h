#ifndef PCG_GAME_RNG_H_INCLUDED
#define PCG_GAME_RNG_H_INCLUDED

/*
    pcg_game_rng.h - Single-header PCG32 RNG with game-dev helpers.
    - Fast, good quality randomness for gameplay + procedural generation
    - Deterministic with a seed (great for reproducible worlds)
    - NOT cryptographically secure (do not use for passwords/keys)

    Usage:
        #define PCG_GAME_RNG_IMPLEMENTATION
        #include "pcg_game_rng.h"

        PcgRng rng;
        pcg_rng_init_auto(&rng);                 // quick seed from time/clock/address
        // OR deterministic:
        // pcg_rng_init(&rng, 12345, 54);

        float x = pcg_rng_range_f32(&rng, -10.0f, 10.0f);
        int   i = pcg_rng_range_i32(&rng, 0, 9);
*/

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PcgRng
{
    uint64_t state;
    uint64_t stream; // internal increment (must be odd)
} PcgRng;

typedef struct PcgVec2 { float x, y; } PcgVec2;
typedef struct PcgVec3 { float x, y, z; } PcgVec3;
typedef struct PcgColor4 { float r, g, b, a; } PcgColor4;

// ---------- Core ----------
void     pcg_rng_init(PcgRng* rng, uint64_t seed, uint64_t stream_id);
void     pcg_rng_init_auto(PcgRng* rng); // convenience non-deterministic-ish seed

uint32_t pcg_rng_u32(PcgRng* rng);
uint64_t pcg_rng_u64(PcgRng* rng);

// ---------- Integers ----------
uint32_t pcg_rng_u32_bounded(PcgRng* rng, uint32_t upper_bound); // [0, upper_bound)
int32_t  pcg_rng_range_i32(PcgRng* rng, int32_t min_inclusive, int32_t max_inclusive);
uint32_t pcg_rng_range_u32(PcgRng* rng, uint32_t min_inclusive, uint32_t max_inclusive);

// ---------- Floats ----------
float    pcg_rng_f32_01(PcgRng* rng);                         // [0,1)
float    pcg_rng_f32_signed(PcgRng* rng);                     // [-1,1)
float    pcg_rng_range_f32(PcgRng* rng, float min, float max);// [min,max)
float    pcg_rng_centered_f32(PcgRng* rng, float center, float radius);

// ---------- Common game helpers ----------
bool     pcg_rng_chance(PcgRng* rng, float probability_0_to_1); // true with p
bool     pcg_rng_coinflip(PcgRng* rng);                         // 50/50
int32_t  pcg_rng_sign(PcgRng* rng);                             // -1 or +1

// ---------- Vectors ----------
PcgVec2  pcg_rng_vec2(PcgRng* rng, float min, float max);
PcgVec3  pcg_rng_vec3(PcgRng* rng, float min, float max);

PcgVec2  pcg_rng_vec2_in_square(PcgRng* rng, float half_extent);
PcgVec2  pcg_rng_vec2_in_circle(PcgRng* rng, float radius);

PcgVec3  pcg_rng_vec3_in_box(PcgRng* rng, float half_extent_xy, float min_z, float max_z);

// ---------- Colors ----------
PcgColor4 pcg_rng_color_rgb(PcgRng* rng);                      // random RGB, a=1
PcgColor4 pcg_rng_color_hsv(PcgRng* rng, float s, float v);     // hue random, s/v fixed

// ---------- Picking ----------
uint32_t pcg_rng_pick_index(PcgRng* rng, uint32_t count);       // [0,count)
void     pcg_rng_shuffle_u32(PcgRng* rng, uint32_t* data, uint32_t count);

// Weighted pick: weights must be >= 0, returns index or -1 if all weights 0
int32_t  pcg_rng_pick_weighted(PcgRng* rng, const float* weights, uint32_t count);

// ---------- Hashing seeds (useful for world chunks) ----------
uint64_t pcg_hash_u64(uint64_t x);

#ifdef __cplusplus
}
#endif


// ======================= IMPLEMENTATION =======================
#ifdef PCG_GAME_RNG_IMPLEMENTATION

#include <time.h>   // time(), clock()
#include <stddef.h> // uintptr_t

static inline uint32_t pcg_rot_r32(uint32_t v, uint32_t r)
{
    return (v >> r) | (v << ((uint32_t)(-(int32_t)r) & 31u));
}

void pcg_rng_init(PcgRng* rng, uint64_t seed, uint64_t stream_id)
{
    rng->state  = 0u;
    rng->stream = (stream_id << 1u) | 1u; // must be odd

    // warm-up
    (void)pcg_rng_u32(rng);
    rng->state += seed;
    (void)pcg_rng_u32(rng);
}

uint32_t pcg_rng_u32(PcgRng* rng)
{
    uint64_t old = rng->state;
    rng->state = old * 6364136223846793005ULL + rng->stream;

    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot        = (uint32_t)(old >> 59u);

    return pcg_rot_r32(xorshifted, rot);
}

uint64_t pcg_rng_u64(PcgRng* rng)
{
    // combine two u32 outputs
    uint64_t lo = (uint64_t)pcg_rng_u32(rng);
    uint64_t hi = (uint64_t)pcg_rng_u32(rng);
    return lo | (hi << 32);
}

uint32_t pcg_rng_u32_bounded(PcgRng* rng, uint32_t upper_bound)
{
    if (upper_bound == 0) return 0;

    uint32_t threshold = (uint32_t)(-upper_bound % upper_bound);
    for (;;)
    {
        uint32_t r = pcg_rng_u32(rng);
        if (r >= threshold)
            return r % upper_bound;
    }
}

uint32_t pcg_rng_range_u32(PcgRng* rng, uint32_t min_inclusive, uint32_t max_inclusive)
{
    if (max_inclusive < min_inclusive)
    {
        uint32_t tmp = min_inclusive;
        min_inclusive = max_inclusive;
        max_inclusive = tmp;
    }

    uint32_t span = (max_inclusive - min_inclusive) + 1u;
    return min_inclusive + pcg_rng_u32_bounded(rng, span);
}

int32_t pcg_rng_range_i32(PcgRng* rng, int32_t min_inclusive, int32_t max_inclusive)
{
    if (max_inclusive < min_inclusive)
    {
        int32_t tmp = min_inclusive;
        min_inclusive = max_inclusive;
        max_inclusive = tmp;
    }

    uint32_t span = (uint32_t)((max_inclusive - min_inclusive) + 1);
    return min_inclusive + (int32_t)pcg_rng_u32_bounded(rng, span);
}

float pcg_rng_f32_01(PcgRng* rng)
{
    // 24-bit precision float in [0,1)
    uint32_t r = pcg_rng_u32(rng);
    return (float)(r >> 8) * (1.0f / 16777216.0f);
}

float pcg_rng_f32_signed(PcgRng* rng)
{
    // [-1,1)
    return pcg_rng_f32_01(rng) * 2.0f - 1.0f;
}

float pcg_rng_range_f32(PcgRng* rng, float min, float max)
{
    return min + (max - min) * pcg_rng_f32_01(rng);
}

float pcg_rng_centered_f32(PcgRng* rng, float center, float radius)
{
    return center + pcg_rng_f32_signed(rng) * radius;
}

bool pcg_rng_coinflip(PcgRng* rng)
{
    return (pcg_rng_u32(rng) & 1u) != 0u;
}

int32_t pcg_rng_sign(PcgRng* rng)
{
    return pcg_rng_coinflip(rng) ? +1 : -1;
}

bool pcg_rng_chance(PcgRng* rng, float probability_0_to_1)
{
    if (probability_0_to_1 <= 0.0f) return false;
    if (probability_0_to_1 >= 1.0f) return true;
    return pcg_rng_f32_01(rng) < probability_0_to_1;
}

PcgVec2 pcg_rng_vec2(PcgRng* rng, float min, float max)
{
    PcgVec2 v;
    v.x = pcg_rng_range_f32(rng, min, max);
    v.y = pcg_rng_range_f32(rng, min, max);
    return v;
}

PcgVec3 pcg_rng_vec3(PcgRng* rng, float min, float max)
{
    PcgVec3 v;
    v.x = pcg_rng_range_f32(rng, min, max);
    v.y = pcg_rng_range_f32(rng, min, max);
    v.z = pcg_rng_range_f32(rng, min, max);
    return v;
}

PcgVec2 pcg_rng_vec2_in_square(PcgRng* rng, float half_extent)
{
    PcgVec2 v;
    v.x = pcg_rng_range_f32(rng, -half_extent, half_extent);
    v.y = pcg_rng_range_f32(rng, -half_extent, half_extent);
    return v;
}

PcgVec2 pcg_rng_vec2_in_circle(PcgRng* rng, float radius)
{
    // Uniform distribution in a circle:
    // angle uniform, radius uses sqrt(t) to avoid center clustering.
    float angle = pcg_rng_range_f32(rng, 0.0f, 6.28318530718f);
    float t     = pcg_rng_f32_01(rng);
    float r     = radius * (float)sqrt(t);

    PcgVec2 v;
    v.x = r * (float)cos(angle);
    v.y = r * (float)sin(angle);
    return v;
}

PcgVec3 pcg_rng_vec3_in_box(PcgRng* rng, float half_extent_xy, float min_z, float max_z)
{
    PcgVec3 v;
    v.x = pcg_rng_range_f32(rng, -half_extent_xy, half_extent_xy);
    v.y = pcg_rng_range_f32(rng, -half_extent_xy, half_extent_xy);
    v.z = pcg_rng_range_f32(rng, min_z, max_z);
    return v;
}

PcgColor4 pcg_rng_color_rgb(PcgRng* rng)
{
    PcgColor4 c;
    c.r = pcg_rng_f32_01(rng);
    c.g = pcg_rng_f32_01(rng);
    c.b = pcg_rng_f32_01(rng);
    c.a = 1.0f;
    return c;
}

// minimal HSV->RGB (h in [0,1), s,v in [0,1])
static inline PcgColor4 pcg_hsv_to_rgb(float h, float s, float v)
{
    float r=0,g=0,b=0;

    float i = (float)((int)(h * 6.0f));
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    int ii = ((int)i) % 6;
    switch (ii)
    {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        case 5: r=v; g=p; b=q; break;
    }

    PcgColor4 c = { r, g, b, 1.0f };
    return c;
}

PcgColor4 pcg_rng_color_hsv(PcgRng* rng, float s, float v)
{
    float h = pcg_rng_f32_01(rng);
    if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
    return pcg_hsv_to_rgb(h, s, v);
}

uint32_t pcg_rng_pick_index(PcgRng* rng, uint32_t count)
{
    if (count == 0) return 0;
    return pcg_rng_u32_bounded(rng, count);
}

void pcg_rng_shuffle_u32(PcgRng* rng, uint32_t* data, uint32_t count)
{
    // Fisher-Yates shuffle
    for (uint32_t i = count; i > 1; i--)
    {
        uint32_t j = pcg_rng_u32_bounded(rng, i);
        uint32_t tmp = data[i - 1];
        data[i - 1] = data[j];
        data[j] = tmp;
    }
}

int32_t pcg_rng_pick_weighted(PcgRng* rng, const float* weights, uint32_t count)
{
    float total = 0.0f;
    for (uint32_t i = 0; i < count; i++)
        if (weights[i] > 0.0f)
            total += weights[i];

    if (total <= 0.0f) return -1;

    float roll = pcg_rng_range_f32(rng, 0.0f, total);

    float acc = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        float w = weights[i];
        if (w <= 0.0f) continue;
        acc += w;
        if (roll < acc)
            return (int32_t)i;
    }

    return (int32_t)(count - 1);
}

uint64_t pcg_hash_u64(uint64_t x)
{
    // SplitMix64 finalizer (great for mixing seeds)
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

void pcg_rng_init_auto(PcgRng* rng)
{
    // Not crypto. Just "varies per run" reasonably.
    uint64_t t  = (uint64_t)time(NULL);
    uint64_t c  = (uint64_t)clock();
    uint64_t a  = (uint64_t)(uintptr_t)rng;

    uint64_t seed      = pcg_hash_u64(t ^ (c << 32) ^ a);
    uint64_t stream_id = pcg_hash_u64(a ^ (t << 1));

    pcg_rng_init(rng, seed, stream_id);
}

#endif // PCG_GAME_RNG_IMPLEMENTATION
#endif // PCG_GAME_RNG_H_INCLUDED
