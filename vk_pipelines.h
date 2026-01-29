#ifndef VK_PIPELINES_H_
#define VK_PIPELINES_H_

#include "vk_defaults.h"
#include "vk_pipeline_layout.h"
#include "vk_descriptor.h"
#include "vk_shader_reflect.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>




typedef struct GraphicsPipelineKey
{
    uint64_t config_hash;
    uint64_t layout_hash;
    uint64_t shader_hash;
} GraphicsPipelineKey;

typedef struct GraphicsPipelineCacheEntry
{
    GraphicsPipelineKey key;
    VkPipeline          pipeline;
} GraphicsPipelineCacheEntry;

typedef struct GraphicsPipelineCache
{
    GraphicsPipelineCacheEntry* entries;
    size_t                      count;
    size_t                      capacity;
} GraphicsPipelineCache;

typedef struct ComputePipelineKey
{
    uint64_t shader_hash;
    uint64_t layout_hash;
} ComputePipelineKey;


typedef struct ComputePipelineCacheEntry
{
    ComputePipelineKey key;
    VkPipeline         pipeline;
} ComputePipelineCacheEntry;

typedef struct ComputePipelineCache
{
    ComputePipelineCacheEntry* entries;
    size_t                     count;
    size_t                     capacity;
} ComputePipelineCache;
// ============================================================================
// Graphics Pipeline Config - minimal, no shader module fields
// ============================================================================

typedef struct GraphicsPipelineConfig
{
    // Rasterization
    VkCullModeFlags cull_mode;
    VkFrontFace     front_face;
    VkPolygonMode   polygon_mode;
    //
    //

    bool primitive_restart_enable;
    // Input assembly
    VkPrimitiveTopology topology;

    // Depth/stencil
    bool        depth_test_enable;
    bool        depth_write_enable;
    VkCompareOp depth_compare_op;
    // Attachments (dynamic rendering)
    uint32_t        color_attachment_count;
    const VkFormat* color_formats;
    VkFormat        depth_format;
    VkFormat        stencil_format;
    bool            use_vertex_input;
    bool            blend_enable;
    bool            reloadable;
} GraphicsPipelineConfig;

// ============================================================================
// Pipeline Creation API - simple, file-path based
// ============================================================================

// Creates a graphics pipeline from SPIR-V file paths.
// Loads shaders, reflects descriptor/push-constant layout, creates pipeline.
// Returns pipeline handle; optionally outputs the created pipeline layout.
VkPipeline create_graphics_pipeline(VkDevice                device,
                                    VkPipelineCache         cache,
                                    DescriptorLayoutCache*  desc_cache,
                                    PipelineLayoutCache*    pipe_cache,
                                    const char*             vert_shader_path,
                                    const char*             frag_shader_path,
                                    GraphicsPipelineConfig* config,
                                    VkPipelineLayout        forced_layout,
                                    VkPipelineLayout*       out_layout);


void vk_cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent);

// Creates a compute pipeline from a SPIR-V file path.
// Loads shader, reflects descriptor/push-constant layout, creates pipeline.
// Returns pipeline handle; optionally outputs the created pipeline layout.
VkPipeline create_compute_pipeline(VkDevice               device,
                                   VkPipelineCache        cache,
                                   DescriptorLayoutCache* desc_cache,
                                   PipelineLayoutCache*   pipe_cache,
                                   const char*            comp_shader_path,
                                   VkPipelineLayout*      out_layout);

// ============================================================================
// Shader hot-reload helpers
// ============================================================================

// Registers a graphics pipeline for hot-reload when config->reloadable is true.
// Keeps the pipeline handle updated in-place when the shader files change.
void pipeline_hot_reload_register_graphics(VkPipeline*             pipeline,
                                           VkPipelineLayout*       layout,
                                           VkDevice                device,
                                           VkPipelineCache         cache,
                                           DescriptorLayoutCache*  desc_cache,
                                           PipelineLayoutCache*    pipe_cache,
                                           const char*             vert_shader_path,
                                           const char*             frag_shader_path,
                                           GraphicsPipelineConfig* config,
                                           VkPipelineLayout        forced_layout);

// Registers a compute pipeline for hot-reload when reloadable is true.
// Keeps the pipeline handle updated in-place when the shader file changes.
void pipeline_hot_reload_register_compute(VkPipeline*            pipeline,
                                          VkPipelineLayout*      layout,
                                          VkDevice               device,
                                          VkPipelineCache        cache,
                                          DescriptorLayoutCache* desc_cache,
                                          PipelineLayoutCache*   pipe_cache,
                                          const char*            comp_shader_path,
                                          bool                   reloadable);

// Checks registered pipelines and reloads any whose shader files have changed.
void pipeline_hot_reload_update(void);

// ============================================================================
// Default config helper
// ============================================================================

static inline GraphicsPipelineConfig graphics_pipeline_config_default(void)
{
    return (GraphicsPipelineConfig){
        .cull_mode              = VK_CULL_MODE_NONE,
        .front_face             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .polygon_mode           = VK_POLYGON_MODE_FILL,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .depth_test_enable      = VK_FALSE,
        .depth_write_enable     = VK_FALSE,
        .depth_compare_op       = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .color_attachment_count = 1,
        .color_formats          = NULL,
        .use_vertex_input       = true,
        .blend_enable           = true,
        .depth_format           = VK_FORMAT_UNDEFINED,
        .stencil_format         = VK_FORMAT_UNDEFINED,
        .reloadable             = false,
    };
}


VkPipeline get_or_create_graphics_pipeline(
    GraphicsPipelineCache*        pso_cache,
    VkDevice                      device,
    VkPipelineCache               vk_cache,
    DescriptorLayoutCache*        desc_cache,
    PipelineLayoutCache*          pipe_cache,
    const char*                   vert_path,
    const char*                   frag_path,
    const GraphicsPipelineConfig* user_cfg,
    VkPipelineLayout              forced_layout,
    VkPipelineLayout*             out_layout);


#endif  // VK_PIPELINES_H_
