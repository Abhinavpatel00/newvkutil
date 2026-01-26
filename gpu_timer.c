
#include "gpu_timer.h"
#include <string.h>
#include <stdio.h>

static uint32_t prof_stamp(VkCommandBuffer cmd, GpuProfiler* p, VkPipelineStageFlags2 stage)
{
    uint32_t idx = p->cursor++;
    if(idx >= p->capacity)
        idx = p->capacity - 1;  // clamp (or assert if you want strict)

    vkCmdWriteTimestamp2(cmd, stage, p->pool, idx);
    return idx;
}

static bool get_ts(GpuProfiler* p, uint32_t q, uint64_t* out)
{
    VkResult r = vkGetQueryPoolResults(p->device, p->pool, q, 1, sizeof(uint64_t), out, sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    return r == VK_SUCCESS;
}

bool gpu_prof_init(GpuProfiler* p, VkDevice device, VkPhysicalDevice gpu, uint32_t query_capacity)
{
    if(!p || query_capacity < 2)
        return false;

    *p = (GpuProfiler){
        .device              = device,
        .gpu                 = gpu,
        .pool                = VK_NULL_HANDLE,
        .timestamp_period_ns = 0.0f,
        .capacity            = query_capacity,
        .cursor              = 0,
        .scope_count         = 0,
        .stack_top           = 0,
    };
printf("gpu_prof_init: gpu arg=%p  p->gpu=%p\n", (void*)gpu, (void*)p->gpu);
    VkPhysicalDeviceProperties props = {0};
    vkGetPhysicalDeviceProperties(gpu, &props);
    p->timestamp_period_ns = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo qpi = {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = query_capacity,
    };

    return vkCreateQueryPool(device, &qpi, NULL, &p->pool) == VK_SUCCESS;
}

void gpu_prof_destroy(GpuProfiler* p)
{
    if(!p)
        return;

    if(p->pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(p->device, p->pool, NULL);

    *p = (GpuProfiler){0};
}

void gpu_prof_begin_frame(VkCommandBuffer cmd, GpuProfiler* p)
{
    p->cursor      = 0;
    p->scope_count = 0;
    p->stack_top   = 0;

    vkCmdResetQueryPool(cmd, p->pool, 0, p->capacity);

    // Create a root "frame" scope automatically
    gpu_prof_scope_begin(cmd, p, "frame", VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
}

void gpu_prof_end_frame(VkCommandBuffer cmd, GpuProfiler* p)
{
    // Close root "frame" scope if still open
    if(p->stack_top > 0)
        gpu_prof_scope_end(cmd, p, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

void gpu_prof_scope_begin(VkCommandBuffer cmd, GpuProfiler* p, const char* name, VkPipelineStageFlags2 stage)
{
    if(p->scope_count >= GPU_PROF_MAX_SCOPES)
        return;

    uint32_t  id = p->scope_count++;
    GpuScope* s  = &p->scopes[id];

    if(name && name[0])
    {
        strncpy(s->name, name, GPU_PROF_NAME_MAX - 1);
        s->name[GPU_PROF_NAME_MAX - 1] = 0;
    }
    else
    {
        strncpy(s->name, "scope", GPU_PROF_NAME_MAX - 1);
        s->name[GPU_PROF_NAME_MAX - 1] = 0;
    }

    s->q_begin = prof_stamp(cmd, p, stage);
    s->q_end   = UINT32_MAX;

    if(p->stack_top < GPU_PROF_MAX_SCOPES)
        p->stack[p->stack_top++] = id;
}

void gpu_prof_scope_end(VkCommandBuffer cmd, GpuProfiler* p, VkPipelineStageFlags2 stage)
{
    if(p->stack_top == 0)
        return;

    uint32_t  id = p->stack[--p->stack_top];
    GpuScope* s  = &p->scopes[id];
    s->q_end     = prof_stamp(cmd, p, stage);
}

bool gpu_prof_get_us(GpuProfiler* p, const char* name, float* out_us)
{
    if(!p || !name || !out_us)
        return false;

    for(uint32_t i = 0; i < p->scope_count; i++)
    {
        GpuScope* s = &p->scopes[i];
        if(s->q_end == UINT32_MAX)
            continue;
        if(strncmp(s->name, name, GPU_PROF_NAME_MAX) != 0)
            continue;

        uint64_t a = 0, b = 0;
        if(!get_ts(p, s->q_begin, &a))
            return false;
        if(!get_ts(p, s->q_end, &b))
            return false;

        double ns = (double)(b - a) * (double)p->timestamp_period_ns;
        *out_us   = (float)(ns / 1000.0);
        return true;
    }

    return false;
}

void gpu_prof_dump(GpuProfiler* p)
{
    if(!p)
        return;

    for(uint32_t i = 0; i < p->scope_count; i++)
    {
        GpuScope* s = &p->scopes[i];
        if(s->q_end == UINT32_MAX)
            continue;

        uint64_t a = 0, b = 0;
        if(!get_ts(p, s->q_begin, &a))
            continue;
        if(!get_ts(p, s->q_end, &b))
            continue;

        double ns = (double)(b - a) * (double)p->timestamp_period_ns;
        printf("[GPU] %-16s %8.3f us\n", s->name, ns / 1000.0);
    }
}
