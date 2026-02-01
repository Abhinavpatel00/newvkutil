#ifndef VK_PIPELINES_H_
#define VK_PIPELINES_H_

#include "vk_defaults.h"
#include "vk_pipeline_layout.h"
#include "vk_descriptor.h"
#include "vk_shader_reflect.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

// Forward declaration for RenderObjectSpec (defined in render_object.h)
typedef struct RenderObjectSpec RenderObjectSpec;





typedef enum {
    GLSL,
    SLANG
} ShaderType;

typedef struct  {
    const char* vert;
    const char* frag;
    const char* comp;
    ShaderType  shader;
} Shaderspec;

// ============================================================================
// Shader Program Descriptor Types (for Slang/GLSL compilation path)
// ============================================================================

typedef struct ShaderStageDesc {
    const char*          file;   // Source file path (GLSL) or NULL for Slang
    const char*          entry;  // Entry point name (e.g., "main", "vertexMain")
    VkShaderStageFlagBits stage;  // VK_SHADER_STAGE_VERTEX_BIT, etc.
} ShaderStageDesc;

typedef struct ShaderProgram {
    ShaderType       type;         // GLSL or SLANG
    const char*      source;       // Slang: source file path; GLSL: NULL
    ShaderStageDesc  stages[8];    // Stage descriptors
    uint32_t         stage_count;  // Number of stages
} ShaderProgram;

static inline const char* shader_stage_default_entry(VkShaderStageFlagBits stage)
{
    switch(stage)
    {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "vsMain";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "psMain";
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return "computeMain";
        default:
            return "main";
    }
}

typedef struct CompiledShaderStage {
    VkShaderStageFlagBits stage;
    const void*           code;   // SPIR-V bytecode (caller frees)
    size_t                size;   // Size in bytes
    char*                 entry;  // Entry point name (caller frees)
} CompiledShaderStage;

// Helper to create ShaderProgram from RenderObjectSpec
static inline ShaderProgram shader_program_from_spec(const Shaderspec* spec)
{
    ShaderProgram prog ;
    prog.type = spec->shader;
    
    if (spec->shader == SLANG) {
        // For Slang, we use a single source file with multiple entry points
        // Assume vert/frag specify the source file and entry point names
        prog.source = spec->vert;  // Use vert path as the source file
        
        if (spec->vert) {
            prog.stages[prog.stage_count].file = NULL;
            prog.stages[prog.stage_count].entry = shader_stage_default_entry(VK_SHADER_STAGE_VERTEX_BIT);
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
            prog.stage_count++;
        }
        if (spec->frag) {
            prog.stages[prog.stage_count].file = NULL;
            prog.stages[prog.stage_count].entry = shader_stage_default_entry(VK_SHADER_STAGE_FRAGMENT_BIT);
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            prog.stage_count++;
        }
        if (spec->comp) {
            prog.source = spec->comp;
            prog.stages[prog.stage_count].file = NULL;
            prog.stages[prog.stage_count].entry = shader_stage_default_entry(VK_SHADER_STAGE_COMPUTE_BIT);
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_COMPUTE_BIT;
            prog.stage_count++;
        }
    } else {
        // GLSL: separate files per stage
        prog.source = NULL;
        
        if (spec->vert) {
            prog.stages[prog.stage_count].file = spec->vert;
            prog.stages[prog.stage_count].entry = "main";
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
            prog.stage_count++;
        }
        if (spec->frag) {
            prog.stages[prog.stage_count].file = spec->frag;
            prog.stages[prog.stage_count].entry = "main";
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            prog.stage_count++;
        }
        if (spec->comp) {
            prog.stages[prog.stage_count].file = spec->comp;
            prog.stages[prog.stage_count].entry = "main";
            prog.stages[prog.stage_count].stage = VK_SHADER_STAGE_COMPUTE_BIT;
            prog.stage_count++;
        }
    }
    
    return prog;
}

// Forward declaration of compile function (defined in vk_pipelines.c)
bool compile_shader_program(
    const ShaderProgram* prog,
    CompiledShaderStage* out,
    uint32_t*            out_count,
    uint64_t*            out_stamp);

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

VkPipeline create_graphics_pipeline_from_spec(VkDevice                device,
                                              VkPipelineCache         cache,
                                              DescriptorLayoutCache*  desc_cache,
                                              PipelineLayoutCache*    pipe_cache,
                                              const RenderObjectSpec* spec,
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
