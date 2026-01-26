#include "proceduraltextures.h"

#include <stdbool.h>

void procedural_fill_checker_rgba8(uint8_t* out, uint32_t w, uint32_t h, uint32_t check_size,
                                   uint8_t r0, uint8_t g0, uint8_t b0,
                                   uint8_t r1, uint8_t g1, uint8_t b1)
{
    for(uint32_t y = 0; y < h; y++)
    {
        for(uint32_t x = 0; x < w; x++)
        {
            uint32_t cx    = (x / check_size) & 1u;
            uint32_t cy    = (y / check_size) & 1u;
            bool     use_a = ((cx ^ cy) == 0u);

            uint8_t* p = out + (y * w + x) * 4;
            p[0]       = use_a ? r0 : r1;
            p[1]       = use_a ? g0 : g1;
            p[2]       = use_a ? b0 : b1;
            p[3]       = 255;
        }
    }
}

void procedural_fill_gradient_rgba8(uint8_t* out, uint32_t w, uint32_t h)
{
    for(uint32_t y = 0; y < h; y++)
    {
        for(uint32_t x = 0; x < w; x++)
        {
            float    fx = (float)x / (float)(w - 1);
            float    fy = (float)y / (float)(h - 1);
            uint8_t* p  = out + (y * w + x) * 4;

            p[0] = (uint8_t)(fx * 255.0f);
            p[1] = (uint8_t)(fy * 255.0f);
            p[2] = (uint8_t)(255.0f - fx * 255.0f);
            p[3] = 255;
        }
    }
}

void procedural_fill_solid_rgba8(uint8_t* out, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for(uint32_t y = 0; y < h; y++)
    {
        for(uint32_t x = 0; x < w; x++)
        {
            uint8_t* p = out + (y * w + x) * 4;
            p[0]       = r;
            p[1]       = g;
            p[2]       = b;
            p[3]       = a;
        }
    }
}
