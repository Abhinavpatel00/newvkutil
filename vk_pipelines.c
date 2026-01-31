#include "vk_pipeline_layout.h"
#include "vk_pipelines.h"
#include "render_object.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "vk_slang_bridge.h"

// ============================================================================
// Internal helpers
// ============================================================================

uint64_t pipeline_layout_hash(PipelineLayoutCache* cache, VkPipelineLayout layout)
{
    for(int i = 0; i < arrlen(cache->entries); i++)
    {
        if(cache->entries[i].layout == layout)
            return cache->entries[i].key.hash;
    }

    // This should never happen if layouts only come from the cache
    return 0;
}
typedef struct DerivedVertexInput
{
    uint32_t                          binding_count;
    uint32_t                          attribute_count;
    VkVertexInputBindingDescription   bindings[8];
    VkVertexInputAttributeDescription attributes[16];
} DerivedVertexInput;

void vk_cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent)
{
    VkViewport vp = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)extent.width,
        .height   = (float)extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D sc = {
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}
static bool read_file(const char* path, void** out_data, size_t* out_size)
{
    *out_data = NULL;
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if(!f)
    {
        log_error("Failed to open '%s' (errno=%d)", path, errno);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    if(len <= 0)
    {
        log_error("Invalid size for '%s'", path);
        fclose(f);
        return false;
    }

    void* data = malloc((size_t)len);
    if(!data)
    {
        log_error("Out of memory reading '%s'", path);
        fclose(f);
        return false;
    }

    if(fread(data, 1, (size_t)len, f) != (size_t)len)
    {
        log_error("Short read for '%s'", path);
        free(data);
        fclose(f);
        return false;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)len;
    return true;
}

static VkShaderModule create_shader_module(VkDevice device, const void* code, size_t size)
{
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = (const uint32_t*)code,
    };

    VkShaderModule mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, NULL, &mod));
    return mod;
}

static uint64_t file_mtime_ns(const char* path)
{
    if(!path)
        return 0;

    struct stat st;
    if(stat(path, &st) != 0)
        return 0;

#if defined(__APPLE__)
    return (uint64_t)st.st_mtimespec.tv_sec * 1000000000ull + (uint64_t)st.st_mtimespec.tv_nsec;
#else
    return (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
#endif
}


static char* str_dup(const char* s)
{
    if(!s)
        return NULL;

    size_t len = strlen(s);
    char*  out = (char*)malloc(len + 1);
    if(!out)
        return NULL;

    memcpy(out, s, len + 1);
    return out;
}

static bool ends_with(const char* s, const char* suffix)
{
    if(!s || !suffix)
        return false;

    size_t sl = strlen(s);
    size_t su = strlen(suffix);

    if(su > sl)
        return false;

    return strcmp(s + (sl - su), suffix) == 0;
}

// compiledshaders/foo.frag.spv -> shaders/foo.frag
static bool spv_to_source_path(char* out, size_t out_cap, const char* spv_path)
{
    if(!spv_path || !out || out_cap == 0)
        return false;

    if(!ends_with(spv_path, ".spv"))
        return false;

    const char* prefix_spv = "compiledshaders/";
    const char* prefix_src = "shaders/";

    if(strncmp(spv_path, prefix_spv, strlen(prefix_spv)) != 0)
        return false;

    const char* rest = spv_path + strlen(prefix_spv);

    size_t rest_len = strlen(rest);
    if(rest_len <= 4)
        return false;

    size_t new_len = rest_len - 4;  // strip ".spv"

    int n = snprintf(out, out_cap, "%s%.*s", prefix_src, (int)new_len, rest);
    return (n > 0 && (size_t)n < out_cap);
}



// ============================================================================
// GLSL compile (Linux dev mode)
// ============================================================================

static bool compile_glsl_program(const ShaderProgram* prog, CompiledShaderStage* out, uint32_t* out_count, uint64_t* out_stamp);
static bool compile_slang_program(const ShaderProgram* prog, CompiledShaderStage* out, uint32_t* out_count, uint64_t* out_stamp);

bool compile_shader_program(
    const ShaderProgram* prog,
    CompiledShaderStage* out,
    uint32_t*            out_count,
    uint64_t*            out_stamp)
{
    if (prog->type == GLSL)
        return compile_glsl_program(prog, out, out_count, out_stamp);
    else
        return compile_slang_program(prog, out, out_count, out_stamp);
}

static bool compile_glsl_to_spv(const char* src_path, const char* spv_path)
{
    if(!src_path || !spv_path)
        return false;

    // Optional: ensure output dir exists
    // mkdir("compiledshaders", 0755);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "glslc \"%s\" -o \"%s\" 2> compiledshaders/shader_errors.txt", src_path, spv_path);

    int r = system(cmd);
    if(r != 0)
    {
        log_error("glslc failed: %s -> %s", src_path, spv_path);

        FILE* f = fopen("compiledshaders/shader_errors.txt", "rb");
        if(f)
        {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            rewind(f);

            if(len > 0)
            {
                char* msg = (char*)malloc((size_t)len + 1);
                if(msg)
                {
                    fread(msg, 1, (size_t)len, f);
                    msg[len] = 0;
                    log_error("Shader compile log:\n%s", msg);
                    free(msg);
                }
            }
            fclose(f);
        }

        return false;
    }

    return true;
}

static bool compile_glsl_program(const ShaderProgram* prog, CompiledShaderStage* out, uint32_t* out_count, uint64_t* out_stamp)
{
    *out_count = 0;
    *out_stamp = 0;

    char spv_path[1024];

    // Simple hash for "stamp" based on source mtimes
    uint64_t total_mtime = 0;

    for (uint32_t i = 0; i < prog->stage_count; i++)
    {
        const ShaderStageDesc* stage = &prog->stages[i];
        
        // Construct output path: compiledshaders/basename.stage.spv
        const char* base = strrchr(stage->file, '/');
        base = base ? base + 1 : stage->file;
        
        const char* ext = "";
        if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT) ext = "vert";
        else if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT) ext = "frag";
        else if (stage->stage == VK_SHADER_STAGE_COMPUTE_BIT) ext = "comp";

        snprintf(spv_path, sizeof(spv_path), "compiledshaders/%s.%s.spv", base, ext);

        // Compile
        if (!compile_glsl_to_spv(stage->file, spv_path))
            return false;
            
        // Read back
        void* code = NULL;
        size_t size = 0;
        if (!read_file(spv_path, &code, &size))
            return false;

        out[*out_count].stage = stage->stage;
        out[*out_count].code = code;
        out[*out_count].size = size;
        out[*out_count].entry = str_dup(stage->entry);
        (*out_count)++;

        total_mtime ^= file_mtime_ns(stage->file);
    }
    
    *out_stamp = total_mtime;
    return true;
}

static bool compile_slang_program(const ShaderProgram* prog, CompiledShaderStage* out, uint32_t* out_count, uint64_t* out_stamp)
{
    *out_count = 0;
    *out_stamp = 0;
    
    if (!prog->source) return false;
    
    uint64_t total_mtime = file_mtime_ns(prog->source);

    for (uint32_t i = 0; i < prog->stage_count; i++)
    {
        const ShaderStageDesc* stage = &prog->stages[i];
        
        void* spv = NULL;
        size_t size = 0;
        
        if (!vk_compile_slang(prog->source, stage->entry, stage->stage, &spv, &size))
        {
            log_error("Failed to compile slang shader: %s : %s", prog->source, stage->entry);
            // Free any previous stages? Caller handles?
            return false;
        }

        out[*out_count].stage = stage->stage;
        out[*out_count].code = spv;
        out[*out_count].size = size;
        out[*out_count].entry = str_dup(stage->entry);
        (*out_count)++;
    }
    
    *out_stamp = total_mtime;
    return true;
}

// ============================================================================
// Hot reload registry
// ============================================================================

typedef struct PipelineHotReloadEntry
{
    bool                   reloadable;
    bool                   is_compute;
    VkDevice               device;
    VkPipelineCache        cache;
    DescriptorLayoutCache* desc_cache;
    PipelineLayoutCache*   pipe_cache;
    VkPipeline*            pipeline;
    VkPipelineLayout*      layout;

    // Graphics (paths are SPV paths!)
    VkPipelineLayout       forced_layout;
    GraphicsPipelineConfig gfx_cfg;
    char*                  vert_path;  // compiledshaders/*.vert.spv
    char*                  frag_path;  // compiledshaders/*.frag.spv

    // Compute (path is SPV path!)
    char* comp_path;  // compiledshaders/*.comp.spv

    // We store SOURCE mtimes (derived from spv paths)
    uint64_t vert_mtime;
    uint64_t frag_mtime;
    uint64_t comp_mtime;
} PipelineHotReloadEntry;
static GraphicsPipelineCache   g_graphics_pso_cache = {0};
static ComputePipelineCache    g_compute_pso_cache  = {0};
static PipelineHotReloadEntry* g_reload_entries     = NULL;
static size_t                  g_reload_count       = 0;
static size_t                  g_reload_cap         = 0;

static void reload_entries_push(const PipelineHotReloadEntry* entry)
{
    if(g_reload_count == g_reload_cap)
    {
        size_t new_cap = (g_reload_cap == 0) ? 8 : g_reload_cap * 2;
        void*  mem     = realloc(g_reload_entries, new_cap * sizeof(PipelineHotReloadEntry));
        if(!mem)
            return;

        g_reload_entries = (PipelineHotReloadEntry*)mem;
        g_reload_cap     = new_cap;
    }

    g_reload_entries[g_reload_count++] = *entry;
}

// ============================================================================
// Public register API
// ============================================================================

void pipeline_hot_reload_register_graphics(VkPipeline*             pipeline,
                                           VkPipelineLayout*       layout,
                                           VkDevice                device,
                                           VkPipelineCache         cache,
                                           DescriptorLayoutCache*  desc_cache,
                                           PipelineLayoutCache*    pipe_cache,
                                           const char*             vert_spv_path,
                                           const char*             frag_spv_path,
                                           GraphicsPipelineConfig* config,
                                           VkPipelineLayout        forced_layout)
{
    if(!config || !config->reloadable || !pipeline)
        return;

    // derive source paths to store mtimes
    char vert_src[1024];
    char frag_src[1024];

    uint64_t vert_src_mtime = 0;
    uint64_t frag_src_mtime = 0;

    if(spv_to_source_path(vert_src, sizeof(vert_src), vert_spv_path))
        vert_src_mtime = file_mtime_ns(vert_src);

    if(spv_to_source_path(frag_src, sizeof(frag_src), frag_spv_path))
        frag_src_mtime = file_mtime_ns(frag_src);

    PipelineHotReloadEntry entry = {
        .reloadable    = true,
        .is_compute    = false,
        .device        = device,
        .cache         = cache,
        .desc_cache    = desc_cache,
        .pipe_cache    = pipe_cache,
        .pipeline      = pipeline,
        .layout        = layout,
        .forced_layout = forced_layout,
        .gfx_cfg       = *config,
        .vert_path     = str_dup(vert_spv_path),
        .frag_path     = str_dup(frag_spv_path),
        .vert_mtime    = vert_src_mtime,
        .frag_mtime    = frag_src_mtime,
    };

    reload_entries_push(&entry);
}

void pipeline_hot_reload_register_compute(VkPipeline*            pipeline,
                                          VkPipelineLayout*      layout,
                                          VkDevice               device,
                                          VkPipelineCache        cache,
                                          DescriptorLayoutCache* desc_cache,
                                          PipelineLayoutCache*   pipe_cache,
                                          const char*            comp_spv_path,
                                          bool                   reloadable)
{
    if(!reloadable || !pipeline)
        return;

    char     comp_src[1024];
    uint64_t comp_src_mtime = 0;
    if(spv_to_source_path(comp_src, sizeof(comp_src), comp_spv_path))
        comp_src_mtime = file_mtime_ns(comp_src);

    PipelineHotReloadEntry entry = {
        .reloadable = true,
        .is_compute = true,
        .device     = device,
        .cache      = cache,
        .desc_cache = desc_cache,
        .pipe_cache = pipe_cache,
        .pipeline   = pipeline,
        .layout     = layout,
        .comp_path  = str_dup(comp_spv_path),
        .comp_mtime = comp_src_mtime,
    };

    reload_entries_push(&entry);
}


// ============================================================================
// Graphics Pipeline
// ============================================================================

VkPipeline create_graphics_pipeline(VkDevice                device,
                                    VkPipelineCache         cache,
                                    DescriptorLayoutCache*  desc_cache,
                                    PipelineLayoutCache*    pipe_cache,
                                    const char*             vert_path,
                                    const char*             frag_path,
                                    GraphicsPipelineConfig* cfg,
                                    VkPipelineLayout        forced_layout,
                                    VkPipelineLayout*       out_layout)
{
    void*  vert_code = NULL;
    size_t vert_size = 0;
    void*  frag_code = NULL;
    size_t frag_size = 0;

    if(!read_file(vert_path, &vert_code, &vert_size))
        return VK_NULL_HANDLE;

    if(!read_file(frag_path, &frag_code, &frag_size))
    {
        free(vert_code);
        return VK_NULL_HANDLE;
    }

    VkShaderModule vert_mod = create_shader_module(device, vert_code, vert_size);
    VkShaderModule frag_mod = create_shader_module(device, frag_code, frag_size);

    // Reflect and build pipeline layout (unless forced)
    const void*      spirvs[2] = {vert_code, frag_code};
    const size_t     sizes[2]  = {vert_size, frag_size};
    VkPipelineLayout layout    = forced_layout;

    if(layout == VK_NULL_HANDLE)
        layout = shader_reflect_build_pipeline_layout(device, desc_cache, pipe_cache, spirvs, sizes, 2);

    if(out_layout)
        *out_layout = layout;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName  = "main",
        },
    };
    DerivedVertexInput derived = {0};
    if(cfg->use_vertex_input)
    {

        ShaderReflection vert_reflect;
        shader_reflect_create(&vert_reflect, vert_code, vert_size);

        derived.attribute_count = shader_reflect_get_vertex_attributes(&vert_reflect, derived.attributes, 16, 0);

        uint32_t stride = 0;
        for(uint32_t i = 0; i < derived.attribute_count; i++)
        {
            uint32_t end = derived.attributes[i].offset;

            switch(derived.attributes[i].format)
            {
                case VK_FORMAT_R32_SFLOAT:
                    end += 4;
                    break;
                case VK_FORMAT_R32G32_SFLOAT:
                    end += 8;
                    break;
                case VK_FORMAT_R32G32B32_SFLOAT:
                    end += 12;
                    break;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                    end += 16;
                    break;
                default:
                    end += 4;
                    break;
            }

            if(end > stride)
                stride = end;
        }

        if(derived.attribute_count > 0)
        {
            derived.binding_count = 1;
            derived.bindings[0]   = (VkVertexInputBindingDescription){
                  .binding   = 0,
                  .stride    = stride,
                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
        }
    }
    else
    {
        derived.binding_count   = 0;
        derived.attribute_count = 0;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = derived.binding_count,
        .pVertexBindingDescriptions      = derived.binding_count ? derived.bindings : NULL,
        .vertexAttributeDescriptionCount = derived.attribute_count,
        .pVertexAttributeDescriptions    = derived.attribute_count ? derived.attributes : NULL,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = cfg->topology,
        .primitiveRestartEnable = cfg->primitive_restart_enable ? VK_TRUE : VK_FALSE,

    };

    VkPipelineViewportStateCreateInfo viewport = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = cfg->polygon_mode,
        .cullMode    = cfg->cull_mode,
        .frontFace   = cfg->front_face,
        .lineWidth   = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = cfg->depth_test_enable,
        .depthWriteEnable = cfg->depth_write_enable,
        .depthCompareOp   = cfg->depth_compare_op,
    };

    VkPipelineColorBlendAttachmentState blend_atts[8] = {0};
    for(uint32_t i = 0; i < cfg->color_attachment_count && i < 8; i++)
    {
        blend_atts[i] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable         = cfg->blend_enable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
        };
    }

    VkPipelineColorBlendStateCreateInfo blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = cfg->color_attachment_count,
        .pAttachments    = blend_atts,
    };

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dyn_states,
    };

    VkPipelineRenderingCreateInfo rendering = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = cfg->color_attachment_count,
        .pColorAttachmentFormats = cfg->color_formats,
        .depthAttachmentFormat   = cfg->depth_format,
        .stencilAttachmentFormat = cfg->stencil_format,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &rendering,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &blend,
        .pDynamicState       = &dynamic,
        .layout              = layout,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, cache, 1, &ci, NULL, &pipeline));


    // ... existing cleanup ...
    vkDestroyShaderModule(device, vert_mod, NULL);
    vkDestroyShaderModule(device, frag_mod, NULL);

    free(vert_code);
    free(frag_code);

    return pipeline;
}

VkPipeline create_graphics_pipeline_from_spec(VkDevice                device,
                                              VkPipelineCache         cache,
                                              DescriptorLayoutCache*  desc_cache,
                                              PipelineLayoutCache*    pipe_cache,
                                              const RenderObjectSpec* spec,
                                              GraphicsPipelineConfig* cfg,
                                              VkPipelineLayout        forced_layout,
                                              VkPipelineLayout*       out_layout)
{
    // Create Shaderspec from RenderObjectSpec
    Shaderspec sspec = {
        .vert = spec->vert_spv,
        .frag = spec->frag_spv,
        .comp = spec->comp_spv,
        .shader = spec->shader
    };
    ShaderProgram prog = shader_program_from_spec(&sspec);
    CompiledShaderStage stages[8];
    uint32_t stage_count = 0;
    uint64_t source_stamp = 0;

    if (!compile_shader_program(&prog, stages, &stage_count, &source_stamp)) {
        return VK_NULL_HANDLE;
    }

    // Identify stages
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    
    // We assume 2 stages for graphics for now based on vk_pipelines logic (src has 2 stages hardcoded mostly)
    // But Slang might output stages differently.
    // The existing logic expects vert_mod and frag_mod variables.
    
    // Create modules
    for (uint32_t i = 0; i < stage_count; i++) {
        VkShaderModule mod = create_shader_module(device, stages[i].code, stages[i].size);
        if (stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT) vert_mod = mod;
        else if (stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT) frag_mod = mod;
        // else ...
    }
    
    if (vert_mod == VK_NULL_HANDLE || frag_mod == VK_NULL_HANDLE) {
        // cleanup modules?
        if (vert_mod) vkDestroyShaderModule(device, vert_mod, NULL);
        if (frag_mod) vkDestroyShaderModule(device, frag_mod, NULL);
        for(uint32_t i=0; i<stage_count; i++) free((void*)stages[i].code);
        return VK_NULL_HANDLE;
    }

    // Reflect
    const void* spirvs[2] = {NULL, NULL};
    size_t sizes[2] = {0, 0};
    
    // Find codes again for reflection
    // Warning: we need to match order for reflection? 
    // shader_reflect_build_pipeline_layout takes array of spirv.
    // Order doesn't matter for reflection usually if it iterates all.
    
    int spv_idx = 0;
    for (uint32_t i = 0; i < stage_count; i++) {
        if (stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT || stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            spirvs[spv_idx] = stages[i].code;
            sizes[spv_idx] = stages[i].size;
            spv_idx++;
        }
    }

    VkPipelineLayout layout = forced_layout;
    if (layout == VK_NULL_HANDLE) {
        layout = shader_reflect_build_pipeline_layout(device, desc_cache, pipe_cache, spirvs, sizes, spv_idx);
    }
    
    if (out_layout) *out_layout = layout;

    VkPipelineShaderStageCreateInfo stage_cis[2];
    uint32_t ci_count = 0;
    
    // Setup stages
    if (vert_mod) {
        stage_cis[ci_count++] = (VkPipelineShaderStageCreateInfo){
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName  = "main", // TODO: GLSL "main", Slang "vs_main" or whatever.
            // Wait, ShaderStageDesc has entry point!
            // CompiledShaderStage has entry!
        };
        // Fix pName: we need to find the entry point for this stage
        for(uint32_t k=0; k<stage_count; k++) { 
             if(stages[k].stage == VK_SHADER_STAGE_VERTEX_BIT) stage_cis[ci_count-1].pName = stages[k].entry; 
        }
    }
    if (frag_mod) {
        stage_cis[ci_count++] = (VkPipelineShaderStageCreateInfo){
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName  = "main", 
        };
        for(uint32_t k=0; k<stage_count; k++) { 
             if(stages[k].stage == VK_SHADER_STAGE_FRAGMENT_BIT) stage_cis[ci_count-1].pName = stages[k].entry; 
        }
    }

    // Reuse DerivedVertexInput logic?
    // It depends on vert code.
    // Logic from create_graphics_pipeline:
    DerivedVertexInput derived = {0};
    if (cfg->use_vertex_input) {
         // Find vertex code buffer
         void* vcode = NULL; size_t vsize = 0;
         for(uint32_t k=0; k<stage_count; k++) if(stages[k].stage == VK_SHADER_STAGE_VERTEX_BIT) { vcode=(void*)stages[k].code; vsize=stages[k].size; break; }
         
         if(vcode) {
            ShaderReflection vert_reflect;
            shader_reflect_create(&vert_reflect, vcode, vsize);
            derived.attribute_count = shader_reflect_get_vertex_attributes(&vert_reflect, derived.attributes, 16, 0);
            
            // ... stride calc logic ...
            uint32_t stride = 0;
            for(uint32_t i = 0; i < derived.attribute_count; i++)
            {
                uint32_t end = derived.attributes[i].offset;
                switch(derived.attributes[i].format) {
                    case VK_FORMAT_R32_SFLOAT: end += 4; break;
                    case VK_FORMAT_R32G32_SFLOAT: end += 8; break;
                    case VK_FORMAT_R32G32B32_SFLOAT: end += 12; break;
                    case VK_FORMAT_R32G32B32A32_SFLOAT: end += 16; break;
                    default: end += 4; break;
                }
                if(end > stride) stride = end;
            }
            if(derived.attribute_count > 0) {
                derived.binding_count = 1;
                derived.bindings[0] = (VkVertexInputBindingDescription){ .binding=0, .stride=stride, .inputRate=VK_VERTEX_INPUT_RATE_VERTEX };
            }
         }
    }

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = derived.binding_count,
        .pVertexBindingDescriptions      = derived.binding_count ? derived.bindings : NULL,
        .vertexAttributeDescriptionCount = derived.attribute_count,
        .pVertexAttributeDescriptions    = derived.attribute_count ? derived.attributes : NULL,
    };
    
    // ... InputAssembly, Viewport, Raster, Multisample, DepthStencil, Blend, Dynamic ...
    // COPY PASTE REST OF CONFIG USAGE
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = cfg->topology,
        .primitiveRestartEnable = cfg->primitive_restart_enable,
    };
     VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = cfg->polygon_mode,
        .cullMode = cfg->cull_mode,
        .frontFace = cfg->front_face,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = cfg->depth_test_enable,
        .depthWriteEnable = cfg->depth_write_enable,
        .depthCompareOp = cfg->depth_compare_op
    };
    VkPipelineColorBlendAttachmentState blend_atts[8] = {0};
    for(uint32_t i = 0; i < cfg->color_attachment_count && i < 8; i++) {
        blend_atts[i] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = 0xF,
            .blendEnable = cfg->blend_enable,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD
        };
    }
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = cfg->color_attachment_count,
        .pAttachments = blend_atts
    };
    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn_states
    };
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = cfg->color_attachment_count,
        .pColorAttachmentFormats = cfg->color_formats,
        .depthAttachmentFormat = cfg->depth_format,
        .stencilAttachmentFormat = cfg->stencil_format
    };
    
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = ci_count,
        .pStages = stage_cis,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = layout
    };
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, cache, 1, &ci, NULL, &pipeline));

    if (vert_mod) vkDestroyShaderModule(device, vert_mod, NULL);
    if (frag_mod) vkDestroyShaderModule(device, frag_mod, NULL);

    for(uint32_t i=0; i<stage_count; i++) {
        free((void*)stages[i].code);
        free((void*)stages[i].entry);
    }
    
    return pipeline;
}

uint64_t hash_graphics_pipeline_config_xx(const GraphicsPipelineConfig* cfg)
{
    XXH64_state_t* h = XXH64_createState();
    XXH64_reset(h, 0xC0FFEEULL);

    GraphicsPipelineConfig tmp = {0};

    tmp.cull_mode                = cfg->cull_mode;
    tmp.front_face               = cfg->front_face;
    tmp.polygon_mode             = cfg->polygon_mode;
    tmp.primitive_restart_enable = cfg->primitive_restart_enable;
    tmp.topology                 = cfg->topology;
    tmp.depth_test_enable        = cfg->depth_test_enable;
    tmp.depth_write_enable       = cfg->depth_write_enable;
    tmp.depth_compare_op         = cfg->depth_compare_op;
    tmp.color_attachment_count   = cfg->color_attachment_count;
    tmp.depth_format             = cfg->depth_format;
    tmp.stencil_format           = cfg->stencil_format;
    tmp.use_vertex_input         = cfg->use_vertex_input;
    tmp.blend_enable             = cfg->blend_enable;

    // explicitly excluded:
    // - color_formats (pointer)
    // - reloadable
    // - vertex bindings / attributes (derived)

    XXH64_update(h, &tmp, sizeof(tmp));

    for(uint32_t i = 0; i < cfg->color_attachment_count; i++)
    {
        XXH64_update(h, &cfg->color_formats[i], sizeof(VkFormat));
    }

    uint64_t out = XXH64_digest(h);
    XXH64_freeState(h);
    return out;
}
// ============================================================================
// Compute Pipeline
// ============================================================================

VkPipeline create_compute_pipeline(VkDevice               device,
                                   VkPipelineCache        cache,
                                   DescriptorLayoutCache* desc_cache,
                                   PipelineLayoutCache*   pipe_cache,
                                   const char*            comp_path,
                                   VkPipelineLayout*      out_layout)
{
    void*  comp_code = NULL;
    size_t comp_size = 0;

    if(!read_file(comp_path, &comp_code, &comp_size))
        return VK_NULL_HANDLE;

    VkShaderModule comp_mod = create_shader_module(device, comp_code, comp_size);

    const void*      spirvs[1] = {comp_code};
    const size_t     sizes[1]  = {comp_size};
    VkPipelineLayout layout    = shader_reflect_build_pipeline_layout(device, desc_cache, pipe_cache, spirvs, sizes, 1);

    if(out_layout)
        *out_layout = layout;

    VkPipelineShaderStageCreateInfo stage = {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = comp_mod,
        .pName  = "main",
    };

    VkComputePipelineCreateInfo ci = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = stage,
        .layout = layout,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, cache, 1, &ci, NULL, &pipeline));

    vkDestroyShaderModule(device, comp_mod, NULL);
    free(comp_code);

    return pipeline;
}


VkPipeline get_or_create_graphics_pipeline(GraphicsPipelineCache*        pso_cache,
                                           VkDevice                      device,
                                           VkPipelineCache               vk_cache,
                                           DescriptorLayoutCache*        desc_cache,
                                           PipelineLayoutCache*          pipe_cache,
                                           const char*                   vert_path,
                                           const char*                   frag_path,
                                           const GraphicsPipelineConfig* user_cfg,
                                           VkPipelineLayout              forced_layout,
                                           VkPipelineLayout*             out_layout)
{
    // --------------------------------------------------
    // Load shaders
    // --------------------------------------------------
    void*  vert_code = NULL;
    size_t vert_size = 0;
    void*  frag_code = NULL;
    size_t frag_size = 0;

    if(!read_file(vert_path, &vert_code, &vert_size))
        return VK_NULL_HANDLE;
    if(!read_file(frag_path, &frag_code, &frag_size))
    {
        free(vert_code);
        return VK_NULL_HANDLE;
    }


    uint64_t shader_hash = XXH64(vert_code, vert_size, 0xA1) ^ (XXH64(frag_code, frag_size, 0xB2) * 0x9E3779B97F4A7C15ull);
    // --------------------------------------------------
    // Build / fetch pipeline layout (reflection)
    // --------------------------------------------------
    const void*  spirvs[2] = {vert_code, frag_code};
    const size_t sizes[2]  = {vert_size, frag_size};

    VkPipelineLayout layout = forced_layout;
    if(layout == VK_NULL_HANDLE)
        layout = shader_reflect_build_pipeline_layout(device, desc_cache, pipe_cache, spirvs, sizes, 2);

    uint64_t layout_hash = pipeline_layout_hash(pipe_cache, layout);

    if(out_layout)
        *out_layout = layout;

    // --------------------------------------------------
    // Build PSO key
    // --------------------------------------------------
    GraphicsPipelineKey key = {
        .config_hash = hash_graphics_pipeline_config_xx(user_cfg),
        .layout_hash = layout_hash,
        .shader_hash = shader_hash,
    };

    // --------------------------------------------------
    // Lookup
    // --------------------------------------------------
    for(size_t i = 0; i < pso_cache->count; i++)
    {
        if(memcmp(&pso_cache->entries[i].key, &key, sizeof(key)) == 0)
        {
            free(vert_code);
            free(frag_code);
            return pso_cache->entries[i].pipeline;
        }
    }

    // --------------------------------------------------
    // Miss → create
    // --------------------------------------------------
    VkPipeline pipeline = create_graphics_pipeline(device, vk_cache, desc_cache, pipe_cache, vert_path, frag_path,
                                                   (GraphicsPipelineConfig*)user_cfg, layout, NULL);

    free(vert_code);
    free(frag_code);

    if(pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    // --------------------------------------------------
    // Insert
    // --------------------------------------------------
    if(pso_cache->count == pso_cache->capacity)
    {
        size_t new_cap      = pso_cache->capacity ? pso_cache->capacity * 2 : 16;
        pso_cache->entries  = realloc(pso_cache->entries, new_cap * sizeof(*pso_cache->entries));
        pso_cache->capacity = new_cap;
    }

    pso_cache->entries[pso_cache->count++] = (GraphicsPipelineCacheEntry){key, pipeline};

    return pipeline;
}


VkPipeline get_or_create_compute_pipeline(ComputePipelineCache*  cache,
                                          VkDevice               device,
                                          VkPipelineCache        vk_cache,
                                          DescriptorLayoutCache* desc_cache,
                                          PipelineLayoutCache*   pipe_cache,
                                          const char*            comp_path,
                                          VkPipelineLayout*      out_layout)
{
    void*  code = NULL;
    size_t size = 0;

    if(!read_file(comp_path, &code, &size))
        return VK_NULL_HANDLE;

    uint64_t shader_hash = XXH64(code, size, 0xC0FFEEULL);

    const void*  spirvs[1] = {code};
    const size_t sizes[1]  = {size};

    VkPipelineLayout layout = shader_reflect_build_pipeline_layout(device, desc_cache, pipe_cache, spirvs, sizes, 1);

    uint64_t layout_hash = pipeline_layout_hash(pipe_cache, layout);

    if(out_layout)
        *out_layout = layout;

    ComputePipelineKey key = {
        .shader_hash = shader_hash,
        .layout_hash = layout_hash,
    };

    for(size_t i = 0; i < cache->count; i++)
    {
        if(memcmp(&cache->entries[i].key, &key, sizeof(key)) == 0)
        {
            free(code);
            return cache->entries[i].pipeline;
        }
    }

    VkPipeline pipeline = create_compute_pipeline(device, vk_cache, desc_cache, pipe_cache, comp_path, NULL);

    free(code);

    if(pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if(cache->count == cache->capacity)
    {
        size_t new_cap  = cache->capacity ? cache->capacity * 2 : 16;
        cache->entries  = realloc(cache->entries, new_cap * sizeof(*cache->entries));
        cache->capacity = new_cap;
    }

    cache->entries[cache->count++] = (ComputePipelineCacheEntry){key, pipeline};

    return pipeline;
}


// ============================================================================
// Hot reload update
// ============================================================================

void pipeline_hot_reload_update(void)
{
    for(size_t i = 0; i < g_reload_count; i++)
    {
        PipelineHotReloadEntry* e = &g_reload_entries[i];

        if(!e->reloadable || !e->pipeline || !e->device)
            continue;

        // ----------------------------
        // Compute
        // ----------------------------
        if(e->is_compute)
        {
            char comp_src[1024];
            if(!spv_to_source_path(comp_src, sizeof(comp_src), e->comp_path))
                continue;

            uint64_t comp_src_mtime = file_mtime_ns(comp_src);
            if(comp_src_mtime == e->comp_mtime)
                continue;

            if(!compile_glsl_to_spv(comp_src, e->comp_path))
                continue;

            e->comp_mtime = comp_src_mtime;

            VkPipelineLayout new_layout = VK_NULL_HANDLE;
            VkPipeline       new_pipe   = get_or_create_compute_pipeline(&g_compute_pso_cache, e->device, e->cache,
                                                                         e->desc_cache, e->pipe_cache, e->comp_path, &new_layout);

            if(new_pipe != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(e->device);

                if(*e->pipeline)
                    vkDestroyPipeline(e->device, *e->pipeline, NULL);

                *e->pipeline = new_pipe;

                if(e->layout)
                    *e->layout = new_layout;
            }

            continue;  // IMPORTANT: don't fall into graphics path
        }

        // ----------------------------
        // Graphics
        // ----------------------------
        char vert_src[1024], frag_src[1024];
        if(!spv_to_source_path(vert_src, sizeof(vert_src), e->vert_path)
           || !spv_to_source_path(frag_src, sizeof(frag_src), e->frag_path))
        {
            continue;
        }

        uint64_t vert_src_mtime = file_mtime_ns(vert_src);
        uint64_t frag_src_mtime = file_mtime_ns(frag_src);

        if(vert_src_mtime == e->vert_mtime && frag_src_mtime == e->frag_mtime)
            continue;

        bool ok = true;

        if(vert_src_mtime != e->vert_mtime)
            ok &= compile_glsl_to_spv(vert_src, e->vert_path);

        if(frag_src_mtime != e->frag_mtime)
            ok &= compile_glsl_to_spv(frag_src, e->frag_path);

        if(!ok)
            continue;

        // Update mtimes only after successful compile
        e->vert_mtime = vert_src_mtime;
        e->frag_mtime = frag_src_mtime;

        VkPipelineLayout new_layout = VK_NULL_HANDLE;
        VkPipeline       new_pipe =
            get_or_create_graphics_pipeline(&g_graphics_pso_cache, e->device, e->cache, e->desc_cache, e->pipe_cache,
                                            e->vert_path, e->frag_path, &e->gfx_cfg, e->forced_layout, &new_layout);
        if(new_pipe != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(e->device);

            if(*e->pipeline)
                vkDestroyPipeline(e->device, *e->pipeline, NULL);

            *e->pipeline = new_pipe;

            if(e->layout)
                *e->layout = new_layout;
        }
    }
}
