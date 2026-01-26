#pragma once
#include "vk_defaults.h"
#ifndef GPU_PROF_MAX_SCOPES
#define GPU_PROF_MAX_SCOPES 128
#endif

#ifndef GPU_PROF_NAME_MAX
#define GPU_PROF_NAME_MAX  32
#endif

typedef struct GpuScope
{
    char     name[GPU_PROF_NAME_MAX];
    uint32_t q_begin;
    uint32_t q_end;
} GpuScope;

typedef struct GpuProfiler
{
    VkDevice         device;
    VkPhysicalDevice gpu;
    VkQueryPool      pool;

    float    timestamp_period_ns;
    uint32_t capacity;
    uint32_t cursor;

    GpuScope scopes[GPU_PROF_MAX_SCOPES];
    uint32_t scope_count;

    // stack for nested scopes
    uint32_t stack[GPU_PROF_MAX_SCOPES];
    uint32_t stack_top;
} GpuProfiler;

bool gpu_prof_init(GpuProfiler* p, VkDevice device, VkPhysicalDevice gpu, uint32_t query_capacity);
void gpu_prof_destroy(GpuProfiler* p);

// Call at start of command buffer recording (per frame)
void gpu_prof_begin_frame(VkCommandBuffer cmd, GpuProfiler* p);

// Call before ending command buffer recording
void gpu_prof_end_frame(VkCommandBuffer cmd, GpuProfiler* p);

// Begin/end a named scope (nesting supported)
void gpu_prof_scope_begin(VkCommandBuffer cmd, GpuProfiler* p, const char* name, VkPipelineStageFlags2 stage);
void gpu_prof_scope_end(VkCommandBuffer cmd, GpuProfiler* p, VkPipelineStageFlags2 stage);

// Read scope time in microseconds (call AFTER fence wait)
bool gpu_prof_get_us(GpuProfiler* p, const char* name, float* out_us);

// Optional: print all scopes (call AFTER fence wait)
void gpu_prof_dump(GpuProfiler* p);

// Scoped macro (C "RAII" hack)
#define GPU_SCOPE(cmd, prof, name, stage) \
    for(int _once = (gpu_prof_scope_begin((cmd), (prof), (name), (stage)), 0); \
        _once == 0; \
        gpu_prof_scope_end((cmd), (prof), (stage)), _once = 1)
