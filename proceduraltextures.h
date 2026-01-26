#pragma once

#include <stdint.h>

void procedural_fill_checker_rgba8(uint8_t* out, uint32_t w, uint32_t h, uint32_t check_size,
                                   uint8_t r0, uint8_t g0, uint8_t b0,
                                   uint8_t r1, uint8_t g1, uint8_t b1);

void procedural_fill_gradient_rgba8(uint8_t* out, uint32_t w, uint32_t h);

void procedural_fill_solid_rgba8(uint8_t* out, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
