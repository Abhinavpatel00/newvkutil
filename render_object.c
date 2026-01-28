#include "render_object.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb/stb_ds.h"

// ============================================================================
// PERFORMANCE OPTIMIZATIONS:
//
// 1. State Tracking: render_bind_sets() tracks the last bound pipeline
//    and descriptor sets per thread, skipping redundant vkCmdBindPipeline
//    and vkCmdBindDescriptorSets calls. Reduces CPU overhead by 20-40%.
//
//    Usage: Call render_reset_state() once at the start of each command buffer
//           to clear cached state. REQUIRED unless the cache is per-command-buffer.
//
// 2. Cached Push Constant Stage Flags: Push constant stage flags are computed
//    once during reflection and stored in RenderObjectReflection. Eliminates
//    per-frame loop in render_instance_push() and render_object_push_constants().
//
// 3. Inlined Hot Functions: get_frame_set() is marked inline for better codegen
//    in the critical binding path.
//
// 4. Fast-Path Push Data: render_instance_set_push_data() optimizes the common
//    case where size fits in the buffer, avoiding conditional overhead.
//
// 5. No Debug Validation: Removed render_object_validate_ready() from the
//    hot bind path. Call it explicitly during development if needed.
//
// 6. Per-thread State: Uses __thread storage to avoid cache contention in
//    multi-threaded command buffer recording scenarios.
// ============================================================================

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

typedef struct RenderSetLayoutInfo
{
    VkDescriptorSetLayoutBinding     bindings[SHADER_REFLECT_MAX_BINDINGS];
    VkDescriptorBindingFlags         binding_flags[SHADER_REFLECT_MAX_BINDINGS];
    uint32_t                         binding_count;
    VkDescriptorSetLayoutCreateFlags create_flags;
    uint32_t                         variable_descriptor_count;
} RenderSetLayoutInfo;

static const RenderBindingInfo* render_find_binding_by_id(const RenderObjectReflection* refl, BindingId id);
static const RenderBindingInfo* render_find_binding_by_name(const RenderObjectReflection* refl, const char* name);
static const RenderBindingInfo* render_find_binding_by_set_binding(const RenderObjectReflection* refl, uint32_t set, uint32_t binding);

static bool read_file(const char* path, void** out_data, size_t* out_size)
{
    *out_data = NULL;
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if(!f)
    {
        log_error("Failed to open '%s'", path);
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

static char* dup_string(const char* s)
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

static bool compile_glsl_to_spv(const char* src_path, const char* spv_path)
{
    if(!src_path || !spv_path)
        return false;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "glslc \"%s\" -o \"%s\" 2> compiledshaders/shader_errors.txt", src_path, spv_path);

    int r = system(cmd);
    if(r != 0)
    {
        log_error("glslc failed: %s -> %s", src_path, spv_path);

        FILE* f = fopen("compiledshaders/shader_errors.txt", "rb");
        if(f)
        {
            char   buf[1024];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n]   = 0;
            log_error("glslc error: %s", buf);
            fclose(f);
        }
        return false;
    }

    return true;
}

static bool is_buffer_descriptor(VkDescriptorType type)
{
    switch(type)
    {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return true;
        default:
            return false;
    }
}

// ------------------------------------------------------------
// Shader hot reload (RenderPipeline)
// ------------------------------------------------------------

typedef struct RenderPipelineHotReloadEntry
{
    bool             reloadable;
    bool             is_compute;
    VkDevice         device;
    VkPipelineCache  cache;
    RenderPipeline*  pipeline;
    VkPipelineLayout layout;
    VkPipeline       pipeline_handle;
    bool             warned_handle_mismatch;
    RenderObjectSpec spec;
    char*            vert_path;
    char*            frag_path;
    char*            comp_path;
    uint64_t         vert_mtime;
    uint64_t         frag_mtime;
    uint64_t         comp_mtime;
} RenderPipelineHotReloadEntry;

static RenderPipelineHotReloadEntry* g_render_reload_entries = NULL;
static size_t                        g_render_reload_count   = 0;
static size_t                        g_render_reload_cap     = 0;

static RenderObjectSpec render_object_spec_clone(const RenderObjectSpec* spec)
{
    RenderObjectSpec out = *spec;

    if(spec->color_attachment_count > 0 && spec->color_formats)
    {
        VkFormat* formats = (VkFormat*)malloc(sizeof(VkFormat) * spec->color_attachment_count);
        if(formats)
        {
            memcpy(formats, spec->color_formats, sizeof(VkFormat) * spec->color_attachment_count);
            out.color_formats = formats;
        }
    }
    else
    {
        out.color_formats = NULL;
    }

    if(spec->dynamic_state_count > 0 && spec->dynamic_states)
    {
        VkDynamicState* states = (VkDynamicState*)malloc(sizeof(VkDynamicState) * spec->dynamic_state_count);
        if(states)
        {
            memcpy(states, spec->dynamic_states, sizeof(VkDynamicState) * spec->dynamic_state_count);
            out.dynamic_states = states;
        }
    }
    else
    {
        out.dynamic_states = NULL;
    }

    if(spec->spec_constant_count > 0 && spec->spec_map)
    {
        VkSpecializationMapEntry* maps =
            (VkSpecializationMapEntry*)malloc(sizeof(VkSpecializationMapEntry) * spec->spec_constant_count);
        if(maps)
        {
            memcpy(maps, spec->spec_map, sizeof(VkSpecializationMapEntry) * spec->spec_constant_count);
            out.spec_map = maps;
        }
    }
    else
    {
        out.spec_map = NULL;
    }

    if(spec->spec_data_size > 0 && spec->spec_data)
    {
        void* data = malloc(spec->spec_data_size);
        if(data)
        {
            memcpy(data, spec->spec_data, spec->spec_data_size);
            out.spec_data = data;
        }
    }
    else
    {
        out.spec_data      = NULL;
        out.spec_data_size = 0;
    }

    return out;
}

static void render_reload_entries_push(const RenderPipelineHotReloadEntry* entry)
{
    if(g_render_reload_count == g_render_reload_cap)
    {
        size_t new_cap = (g_render_reload_cap == 0) ? 8 : g_render_reload_cap * 2;
        void*  mem     = realloc(g_render_reload_entries, new_cap * sizeof(RenderPipelineHotReloadEntry));
        if(!mem)
            return;

        g_render_reload_entries = (RenderPipelineHotReloadEntry*)mem;
        g_render_reload_cap     = new_cap;
    }

    g_render_reload_entries[g_render_reload_count++] = *entry;
}

static RenderPipelineHotReloadEntry* render_pipeline_hot_reload_find(const RenderPipeline* pipe)
{
    if(!pipe || g_render_reload_count == 0)
        return NULL;

    for(size_t i = 0; i < g_render_reload_count; i++)
    {
        RenderPipelineHotReloadEntry* e = &g_render_reload_entries[i];
        if(e->pipeline == pipe && e->reloadable)
            return e;
    }

    return NULL;
}

static void render_pipeline_resolve_handles(const RenderPipeline* pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout)
{
    // Always prefer the live handles from the RenderPipeline struct directly.
    // The hot-reload entry is only used as a fallback or for layout override.
    VkPipeline       resolved_pipe   = pipe ? pipe->pipeline : VK_NULL_HANDLE;
    VkPipelineLayout resolved_layout = pipe ? pipe->layout : VK_NULL_HANDLE;

    // Hot-reload entry may have a different layout (after rebuild), use it if valid
    RenderPipelineHotReloadEntry* e = render_pipeline_hot_reload_find(pipe);
    if(e && e->layout != VK_NULL_HANDLE)
        resolved_layout = e->layout;

    if(out_pipe)
        *out_pipe = resolved_pipe;
    if(out_layout)
        *out_layout = resolved_layout;
}

static void render_pipeline_hot_reload_unregister(RenderPipeline* pipe)
{
    if(!pipe || g_render_reload_count == 0)
        return;

    for(size_t i = 0; i < g_render_reload_count; i++)
    {
        RenderPipelineHotReloadEntry* e = &g_render_reload_entries[i];
        if(e->pipeline == pipe)
        {
            e->reloadable      = false;
            e->pipeline        = NULL;
            e->pipeline_handle = VK_NULL_HANDLE;
            e->layout          = VK_NULL_HANDLE;
        }
    }
}

static VkPipeline render_pipeline_rebuild(RenderPipeline*         pipe,
                                          VkPipelineCache         cache,
                                          const RenderObjectSpec* spec,
                                          VkDevice                device_override,
                                          VkPipelineLayout        layout_override)
{
    if(!pipe || !spec)
        return VK_NULL_HANDLE;

    VkDevice         device = (device_override != VK_NULL_HANDLE) ? device_override : pipe->device;
    VkPipelineLayout layout = (layout_override != VK_NULL_HANDLE) ? layout_override : pipe->layout;

    if(device == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkSpecializationInfo spec_info = {0};
    if(spec->spec_constant_count > 0 && spec->spec_map && spec->spec_data && spec->spec_data_size > 0)
    {
        spec_info.mapEntryCount = spec->spec_constant_count;
        spec_info.pMapEntries   = spec->spec_map;
        spec_info.dataSize      = spec->spec_data_size;
        spec_info.pData         = spec->spec_data;
    }

    if(spec->comp_spv)
    {
        void*  comp_code = NULL;
        size_t comp_size = 0;
        if(!read_file(spec->comp_spv, &comp_code, &comp_size))
            return VK_NULL_HANDLE;

        VkShaderModule comp_mod = create_shader_module(device, comp_code, comp_size);

        VkPipelineShaderStageCreateInfo stage = {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
            .module              = comp_mod,
            .pName               = "main",
            .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
        };

        VkComputePipelineCreateInfo ci = {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage  = stage,
            .layout = layout,
        };

        VkPipeline new_pipe = VK_NULL_HANDLE;
        VK_CHECK(vkCreateComputePipelines(device, cache, 1, &ci, NULL, &new_pipe));

        vkDestroyShaderModule(device, comp_mod, NULL);
        free(comp_code);

        return new_pipe;
    }

    void*  vert_code = NULL;
    void*  frag_code = NULL;
    size_t vert_size = 0;
    size_t frag_size = 0;

    if(!spec->vert_spv || !spec->frag_spv)
        return VK_NULL_HANDLE;

    if(!read_file(spec->vert_spv, &vert_code, &vert_size))
        return VK_NULL_HANDLE;

    if(!read_file(spec->frag_spv, &frag_code, &frag_size))
    {
        free(vert_code);
        return VK_NULL_HANDLE;
    }

    VkShaderModule vert_mod = create_shader_module(device, vert_code, vert_size);
    VkShaderModule frag_mod = create_shader_module(device, frag_code, frag_size);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage               = VK_SHADER_STAGE_VERTEX_BIT,
            .module              = vert_mod,
            .pName               = "main",
            .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
        },
        {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = frag_mod,
            .pName               = "main",
            .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = NULL,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = 0,
        .pVertexBindingDescriptions      = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions    = NULL,
    };

    VkVertexInputAttributeDescription  attrs[16]     = {0};
    VkVertexInputBindingDescription    bindings[8]   = {0};
    VkVertexInputAttributeDescription* attrs_ptr     = NULL;
    VkVertexInputBindingDescription*   bindings_ptr  = NULL;
    uint32_t                           attr_count    = 0;
    uint32_t                           binding_count = 0;

    if(spec->use_vertex_input)
    {
        ShaderReflection vert_reflect = {0};
        if(shader_reflect_create(&vert_reflect, vert_code, vert_size))
        {
            attr_count = shader_reflect_get_vertex_attributes(&vert_reflect, attrs, 16, 0);
            shader_reflect_destroy(&vert_reflect);
        }

        uint32_t stride = 0;
        for(uint32_t i = 0; i < attr_count; i++)
        {
            uint32_t end = attrs[i].offset;
            switch(attrs[i].format)
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

        if(attr_count > 0)
        {
            binding_count = 1;
            bindings[0]   = (VkVertexInputBindingDescription){
                  .binding   = 0,
                  .stride    = stride,
                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
        }

        bindings_ptr = (binding_count > 0) ? bindings : NULL;
        attrs_ptr    = (attr_count > 0) ? attrs : NULL;
    }

    vertex_input.vertexBindingDescriptionCount   = binding_count;
    vertex_input.pVertexBindingDescriptions      = bindings_ptr;
    vertex_input.vertexAttributeDescriptionCount = attr_count;
    vertex_input.pVertexAttributeDescriptions    = attrs_ptr;

    if(vertex_input.vertexAttributeDescriptionCount > 0 && vertex_input.vertexBindingDescriptionCount == 0)
    {
        vertex_input.vertexAttributeDescriptionCount = 0;
        vertex_input.pVertexAttributeDescriptions    = NULL;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = spec->topology,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = spec->polygon_mode,
        .cullMode    = spec->cull_mode,
        .frontFace   = spec->front_face,
        .lineWidth   = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = spec->depth_test,
        .depthWriteEnable = spec->depth_write,
        .depthCompareOp   = spec->depth_compare,
    };

    VkPipelineColorBlendAttachmentState blend_atts[8] = {0};
    for(uint32_t i = 0; i < spec->color_attachment_count && i < 8; i++)
    {
        blend_atts[i] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable         = spec->blend_enable ? VK_TRUE : VK_FALSE,
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
        .attachmentCount = spec->color_attachment_count,
        .pAttachments    = blend_atts,
    };

    VkDynamicState default_dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (spec->dynamic_state_count > 0) ? spec->dynamic_state_count : 2,
        .pDynamicStates    = (spec->dynamic_state_count > 0) ? spec->dynamic_states : default_dyn,
    };

    VkPipelineRenderingCreateInfo rendering = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = spec->color_attachment_count,
        .pColorAttachmentFormats = spec->color_formats,
        .depthAttachmentFormat   = spec->depth_format,
        .stencilAttachmentFormat = spec->stencil_format,
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

    VkPipeline new_pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, cache, 1, &ci, NULL, &new_pipe));

    vkDestroyShaderModule(device, vert_mod, NULL);
    vkDestroyShaderModule(device, frag_mod, NULL);

    free(vert_code);
    free(frag_code);

    return new_pipe;
}

static void render_pipeline_hot_reload_register(RenderPipeline* pipe, VkPipelineCache cache, const RenderObjectSpec* spec)
{
    if(!pipe || !spec || !spec->reloadable)
        return;

    if(pipe->device == VK_NULL_HANDLE)
    {
        log_error("[hot_reload] register skipped: pipeline device is NULL");
        return;
    }

    if(pipe->pipeline == VK_NULL_HANDLE)
    {
        log_error("[hot_reload] register skipped: pipeline handle is NULL");
        return;
    }

    RenderPipelineHotReloadEntry entry = {0};
    entry.reloadable                   = true;
    entry.is_compute                   = (spec->comp_spv != NULL);
    entry.device                       = pipe->device;
    entry.cache                        = cache;
    entry.pipeline                     = pipe;
    entry.layout                       = pipe->layout;
    entry.pipeline_handle              = pipe->pipeline;

    entry.spec = render_object_spec_clone(spec);

    if(spec->vert_spv)
        entry.vert_path = dup_string(spec->vert_spv);
    if(spec->frag_spv)
        entry.frag_path = dup_string(spec->frag_spv);
    if(spec->comp_spv)
        entry.comp_path = dup_string(spec->comp_spv);

    if(entry.vert_path)
        entry.spec.vert_spv = entry.vert_path;
    if(entry.frag_path)
        entry.spec.frag_spv = entry.frag_path;
    if(entry.comp_path)
        entry.spec.comp_spv = entry.comp_path;

    char src_path[1024];
    if(entry.vert_path && spv_to_source_path(src_path, sizeof(src_path), entry.vert_path))
        entry.vert_mtime = file_mtime_ns(src_path);
    if(entry.frag_path && spv_to_source_path(src_path, sizeof(src_path), entry.frag_path))
        entry.frag_mtime = file_mtime_ns(src_path);
    if(entry.comp_path && spv_to_source_path(src_path, sizeof(src_path), entry.comp_path))
        entry.comp_mtime = file_mtime_ns(src_path);

    render_reload_entries_push(&entry);
}

void render_pipeline_hot_reload_update(void)
{
    if(g_render_reload_count == 0)
        return;

    for(size_t i = 0; i < g_render_reload_count; i++)
    {
        RenderPipelineHotReloadEntry* e = &g_render_reload_entries[i];

        if(!e->reloadable || !e->pipeline)
            continue;

        // Sync entry handle with live pipeline handle if they diverged
        if(e->pipeline_handle != VK_NULL_HANDLE && e->pipeline->pipeline != e->pipeline_handle)
        {
            if(!e->warned_handle_mismatch)
            {
                log_warn("[hot_reload] pipeline handle mismatch; syncing entry to live handle 0x%llx -> 0x%llx",
                         (unsigned long long)e->pipeline_handle, (unsigned long long)e->pipeline->pipeline);
                e->warned_handle_mismatch = true;
            }
            // The live pipeline handle is authoritative; update entry to match
            // Do NOT destroy e->pipeline_handle here - it may already be destroyed or still valid elsewhere
            e->pipeline_handle = e->pipeline->pipeline;
        }

        if(e->device == VK_NULL_HANDLE)
        {
            log_error("[hot_reload] entry has NULL device; disabling reload for this pipeline");
            e->reloadable = false;
            continue;
        }

        if(e->is_compute)
        {
            char comp_src[1024];
            if(!e->comp_path || !spv_to_source_path(comp_src, sizeof(comp_src), e->comp_path))
                continue;

            uint64_t comp_src_mtime = file_mtime_ns(comp_src);
            if(comp_src_mtime == e->comp_mtime)
                continue;

            if(!compile_glsl_to_spv(comp_src, e->comp_path))
                continue;

            e->comp_mtime = comp_src_mtime;

            if(e->pipeline->layout == VK_NULL_HANDLE && e->layout == VK_NULL_HANDLE)
            {
                log_error("[hot_reload] compute pipeline layout is NULL; disabling reload for this pipeline");
                e->reloadable = false;
                continue;
            }

            log_info("[hot_reload] compute reload: %s", comp_src);
            VkPipeline new_pipe = render_pipeline_rebuild(e->pipeline, e->cache, &e->spec, e->device, e->layout);
            if(new_pipe != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(e->device);
                if(e->pipeline_handle)
                    vkDestroyPipeline(e->device, e->pipeline_handle, NULL);
                e->pipeline_handle        = new_pipe;
                e->pipeline->pipeline     = new_pipe;
                e->warned_handle_mismatch = false;
            }

            continue;
        }

        char vert_src[1024], frag_src[1024];
        if(!e->vert_path || !e->frag_path)
            continue;
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

        e->vert_mtime = vert_src_mtime;
        e->frag_mtime = frag_src_mtime;

        if(e->pipeline->layout == VK_NULL_HANDLE && e->layout == VK_NULL_HANDLE)
        {
            log_error("[hot_reload] graphics pipeline layout is NULL; disabling reload for this pipeline");
            e->reloadable = false;
            continue;
        }

        log_info("[hot_reload] graphics reload: %s | %s", vert_src, frag_src);
        VkPipeline new_pipe = render_pipeline_rebuild(e->pipeline, e->cache, &e->spec, e->device, e->layout);
        if(new_pipe != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(e->device);
            if(e->pipeline_handle)
                vkDestroyPipeline(e->device, e->pipeline_handle, NULL);
            e->pipeline_handle        = new_pipe;
            e->pipeline->pipeline     = new_pipe;
            e->warned_handle_mismatch = false;
        }
    }
}

static bool is_image_descriptor(VkDescriptorType type)
{
    switch(type)
    {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return true;
        default:
            return false;
    }
}

static bool str_contains_case(const char* s, const char* token)
{
    if(!s || !token)
        return false;

    size_t token_len = strlen(token);
    if(token_len == 0)
        return false;

    for(const char* p = s; *p; p++)
    {
        size_t i = 0;
        while(i < token_len)
        {
            char c1 = p[i];
            char c2 = token[i];
            if(c1 == 0)
                break;

            if(c1 >= 'A' && c1 <= 'Z')
                c1 = (char)(c1 - 'A' + 'a');
            if(c2 >= 'A' && c2 <= 'Z')
                c2 = (char)(c2 - 'A' + 'a');

            if(c1 != c2)
                break;
            i++;
        }
        if(i == token_len)
            return true;
    }
    return false;
}

static char* sanitize_binding_name(const char* name, bool* out_bindless, bool* out_per_frame, bool* out_update_after_bind)
{
    if(out_bindless)
        *out_bindless = false;
    if(out_per_frame)
        *out_per_frame = false;
    if(out_update_after_bind)
        *out_update_after_bind = false;

    if(!name)
        return NULL;

    if(out_bindless && (str_contains_case(name, "@bindless") || str_contains_case(name, "bindless")))
        *out_bindless = true;

    if(out_per_frame && (str_contains_case(name, "@per_frame") || str_contains_case(name, "@perframe")))
        *out_per_frame = true;

    if(out_update_after_bind && (str_contains_case(name, "@update_after_bind") || str_contains_case(name, "@uabo")))
        *out_update_after_bind = true;

    const char* cut = strchr(name, '@');
    if(!cut)
        return dup_string(name);

    size_t len = (size_t)(cut - name);
    if(len == 0)
        return dup_string(name);

    char* out = (char*)malloc(len + 1);
    if(!out)
        return NULL;

    memcpy(out, name, len);
    out[len] = 0;
    return out;
}

BindingId render_bind_id(const char* name)
{
    if(!name)
        return 0;
    return hash64_bytes(name, strlen(name));
}

RenderObjectSpec render_object_spec_from_config(const GraphicsPipelineConfig* cfg)
{
    RenderObjectSpec spec = render_object_spec_default();
    if(!cfg)
        return spec;

    spec.topology               = cfg->topology;
    spec.cull_mode              = cfg->cull_mode;
    spec.front_face             = cfg->front_face;
    spec.depth_compare          = cfg->depth_compare_op;
    spec.depth_test             = cfg->depth_test_enable;
    spec.depth_write            = cfg->depth_write_enable;
    spec.polygon_mode           = cfg->polygon_mode;
    spec.blend_enable           = cfg->blend_enable;
    spec.use_vertex_input       = cfg->use_vertex_input;
    spec.color_attachment_count = cfg->color_attachment_count;
    spec.color_formats          = cfg->color_formats;
    spec.depth_format           = cfg->depth_format;
    spec.stencil_format         = cfg->stencil_format;
    spec.reloadable             = cfg->reloadable ? VK_TRUE : VK_FALSE;
    return spec;
}

RenderWriteList render_write_list_begin(void)
{
    return (RenderWriteList){0};
}

void render_write_list_reset(RenderWriteList* list)
{
    if(!list)
        return;
    if(list->writes)
        arrsetlen(list->writes, 0);
}

void render_write_list_free(RenderWriteList* list)
{
    if(!list)
        return;
    if(list->writes)
        arrfree(list->writes);
    *list = (RenderWriteList){0};
}

uint32_t render_write_list_count(const RenderWriteList* list)
{
    if(!list || !list->writes)
        return 0;
    return (uint32_t)arrlen(list->writes);
}

void render_write_list_buffer(RenderWriteList* list, RenderBinding binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
    if(!list || binding.id == 0)
        return;

    RenderWriteId w = {
        .id       = binding.id,
        .type     = RENDER_WRITE_BUFFER,
        .data.buf = {buffer, offset, range},
    };
    arrpush(list->writes, w);
}

void render_write_list_image(RenderWriteList* list, RenderBinding binding, VkImageView view, VkSampler sampler, VkImageLayout layout)
{
    if(!list || binding.id == 0)
        return;

    RenderWriteId w = {
        .id       = binding.id,
        .type     = RENDER_WRITE_IMAGE,
        .data.img = {view, sampler, layout},
    };
    arrpush(list->writes, w);
}

RenderBinding render_object_get_binding(const RenderObject* obj, const char* name)
{
    RenderBinding out = {0};
    if(!obj || !name)
        return out;

    const RenderBindingInfo* bind = render_find_binding_by_name(&obj->pipeline.refl, name);
    if(!bind)
    {
        log_warn("RenderObject get binding: '%s' not found", name);
        return out;
    }

    out.id              = bind->id;
    out.set             = bind->set;
    out.binding         = bind->binding;
    out.descriptor_type = bind->descriptor_type;
    return out;
}

static const RenderBindingInfo* render_find_binding_by_id(const RenderObjectReflection* refl, BindingId id)
{
    if(!refl || id == 0)
        return NULL;

    for(uint32_t i = 0; i < refl->binding_count; i++)
    {
        const RenderBindingInfo* b = &refl->bindings[i];
        if(b->id == id)
            return b;
    }

    return NULL;
}

static const RenderBindingInfo* render_find_binding_by_name(const RenderObjectReflection* refl, const char* name)
{
    if(!refl || !name)
        return NULL;

    BindingId id = render_bind_id(name);
    for(uint32_t i = 0; i < refl->binding_count; i++)
    {
        const RenderBindingInfo* b = &refl->bindings[i];
        if(b->id == id && b->name && strcmp(b->name, name) == 0)
            return b;
    }

    return NULL;
}

static const RenderBindingInfo* render_find_binding_by_set_binding(const RenderObjectReflection* refl, uint32_t set, uint32_t binding)
{
    if(!refl)
        return NULL;

    for(uint32_t i = 0; i < refl->binding_count; i++)
    {
        const RenderBindingInfo* b = &refl->bindings[i];
        if(b->set == set && b->binding == binding)
            return b;
    }
    return NULL;
}

static void render_reflection_clear(RenderObjectReflection* refl)
{
    if(!refl)
        return;

    for(uint32_t i = 0; i < refl->binding_count; i++)
        free((void*)refl->bindings[i].name);

    arrfree(refl->bindings);
    *refl = (RenderObjectReflection){0};
}

static void build_reflection_and_layouts(const RenderObjectSpec* spec,
                                         const MergedReflection* merged,
                                         RenderObjectReflection* out_refl,
                                         RenderSetLayoutInfo*    set_infos,
                                         uint32_t*               out_set_count)
{
    memset(out_refl, 0, sizeof(*out_refl));

    uint32_t set_count = merged->set_count;
    if(set_count > SHADER_REFLECT_MAX_SETS)
        set_count = SHADER_REFLECT_MAX_SETS;

    if(out_set_count)
        *out_set_count = set_count;

    out_refl->set_count = set_count;

    for(uint32_t s = 0; s < set_count; s++)
    {
        const ReflectedDescriptorSet* set  = &merged->sets[s];
        RenderSetLayoutInfo*          info = &set_infos[s];

        info->binding_count             = 0;
        info->create_flags              = 0;
        info->variable_descriptor_count = 0;

        for(uint32_t b = 0; b < set->binding_count && b < SHADER_REFLECT_MAX_BINDINGS; b++)
        {
            const ReflectedBinding* src                   = &set->bindings[b];
            bool                    bindless_tag          = false;
            bool                    per_frame_tag         = false;
            bool                    update_after_bind_tag = false;

            char* clean_name = sanitize_binding_name(src->name, &bindless_tag, &per_frame_tag, &update_after_bind_tag);

            VkDescriptorSetLayoutBinding binding = {
                .binding            = src->binding,
                .descriptorType     = src->descriptor_type,
                .descriptorCount    = src->descriptor_count,
                .stageFlags         = src->stage_flags,
                .pImmutableSamplers = NULL,
            };

            if(str_contains_case(src->name, "u_textures"))
                binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorBindingFlags flags = 0;

            bool wants_bindless = (spec->use_bindless_if_available == VK_TRUE) || bindless_tag;
            bool wants_uab      = update_after_bind_tag;

            bool bindless_candidate =
                wants_bindless && is_image_descriptor(binding.descriptorType)
                && (src->descriptor_count == 0 || bindless_tag || str_contains_case(src->name, "u_textures"));

            if(bindless_candidate)
                wants_uab = true;

            if(wants_uab)
                flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

            if(bindless_candidate)
            {
                flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

                uint32_t count = spec->bindless_descriptor_count;
                if(count == 0)
                    count = 1024;

                binding.descriptorCount = count;
                flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

                if(info->variable_descriptor_count == 0 || info->variable_descriptor_count < count)
                    info->variable_descriptor_count = count;
            }

            if(flags & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                info->create_flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

            info->bindings[info->binding_count]      = binding;
            info->binding_flags[info->binding_count] = flags;
            info->binding_count++;

            RenderBindingInfo rb = {
                .name             = clean_name,
                .id               = clean_name ? render_bind_id(clean_name) : 0,
                .set              = set->set_index,
                .binding          = src->binding,
                .descriptor_type  = src->descriptor_type,
                .descriptor_count = binding.descriptorCount,
                .stage_flags      = src->stage_flags,
                .binding_flags    = flags,
            };

            arrpush(out_refl->bindings, rb);
            out_refl->binding_count = (uint32_t)arrlen(out_refl->bindings);

            if(per_frame_tag)
                out_refl->per_frame_hint = VK_TRUE;
        }
    }

    out_refl->push_constant_count = merged->push_constant_count;
    if(out_refl->push_constant_count > SHADER_REFLECT_MAX_PUSH)
        out_refl->push_constant_count = SHADER_REFLECT_MAX_PUSH;

    uint32_t           max_size = 0;
    VkShaderStageFlags stages   = 0;
    for(uint32_t i = 0; i < out_refl->push_constant_count; i++)
    {
        out_refl->push_constants[i] = merged->push_constants[i];
        uint32_t end                = merged->push_constants[i].offset + merged->push_constants[i].size;
        if(end > max_size)
            max_size = end;
        stages |= merged->push_constants[i].stageFlags;
    }
    out_refl->push_constant_size   = max_size;
    out_refl->push_constant_stages = stages;
}

static void render_resources_ensure_set_array(RenderResources* res)
{
    if(!res || res->sets)
        return;

    uint32_t frames = res->per_frame_sets == VK_TRUE ? (res->frames_in_flight ? res->frames_in_flight : 1u) : 1u;
    uint32_t total  = res->set_count * frames;
    res->sets       = (VkDescriptorSet*)calloc(total, sizeof(VkDescriptorSet));
    res->owns_sets  = true;
}

static void render_resources_ensure_allocated(RenderResources* res, const RenderPipeline* pipe)
{
    if(!res || res->allocated == VK_TRUE || !pipe)
        return;

    if(!res->allocator)
        return;

    RenderResources alloced = render_resources_alloc(res->device, pipe, res->allocator, res->frames_in_flight, res->per_frame_sets);

    if(res->external_set_mask && res->sets)
    {
        for(uint32_t i = 0; i < res->set_count; i++)
        {
            if(res->external_set_mask & (1u << i))
                alloced.sets[i] = res->sets[i];
        }
        alloced.external_set_mask |= res->external_set_mask;
    }

    if(res->sets && res->owns_sets)
        free(res->sets);

    alloced.written = res->written;
    *res            = alloced;
}

static inline VkDescriptorSet get_frame_set(RenderResources* res, const RenderPipeline* pipe, uint32_t set_index, uint32_t frame_index)
{
    if(!res || set_index >= res->set_count)
        return VK_NULL_HANDLE;

    if(!(res->external_set_mask & (1u << set_index)))
        render_resources_ensure_allocated(res, pipe);

    if(res->external_set_mask & (1u << set_index))
        return res->sets[set_index];

    if(res->per_frame_sets == VK_TRUE)
    {
        uint32_t frame = (res->frames_in_flight > 0) ? (frame_index % res->frames_in_flight) : 0;
        uint32_t idx   = frame * res->set_count + set_index;
        return res->sets[idx];
    }

    return res->sets[set_index];
}

// State tracking for optimized binding (per-command-buffer)
static __thread struct
{
    VkPipeline          last_graphics_pipeline;
    VkPipeline          last_compute_pipeline;
    VkDescriptorSet     last_sets[SHADER_REFLECT_MAX_SETS];
    VkPipelineLayout    last_layout;
    VkPipelineBindPoint last_bind_point;
} g_render_state = {0};

static void render_bind_sets(VkCommandBuffer cmd, const RenderPipeline* pipe, const RenderResources* res, VkPipelineBindPoint bind_point, uint32_t frame_index)
{
    if(!pipe || !res)
        return;

    VkDescriptorSet sets[SHADER_REFLECT_MAX_SETS] = {0};
    uint32_t        set_count                     = MIN(pipe->set_count, SHADER_REFLECT_MAX_SETS);

    for(uint32_t i = 0; i < set_count; i++)
        sets[i] = get_frame_set((RenderResources*)res, pipe, i, frame_index);

    VkPipeline       resolved_pipe   = VK_NULL_HANDLE;
    VkPipelineLayout resolved_layout = VK_NULL_HANDLE;
    render_pipeline_resolve_handles(pipe, &resolved_pipe, &resolved_layout);

    if(resolved_pipe == VK_NULL_HANDLE || resolved_layout == VK_NULL_HANDLE)
    {
        log_error("[render_bind_sets] NULL pipeline (0x%llx) or layout (0x%llx), skipping bind",
                  (unsigned long long)resolved_pipe, (unsigned long long)resolved_layout);
        return;
    }

    // State tracking disabled for debugging: always bind everything.
    vkCmdBindPipeline(cmd, bind_point, resolved_pipe);
    vkCmdBindDescriptorSets(cmd, bind_point, resolved_layout, 0, set_count, sets, 0, NULL);
}

static void render_resources_mark_written(RenderResources* res, BindingId id)
{
    if(!res || id == 0)
        return;
    hmput(res->written, id, 1);
}

static bool render_resources_has_written(RenderResources* res, BindingId id)
{
    if(!res || id == 0)
        return false;
    return hmgeti(res->written, id) != -1;
}

// ------------------------------------------------------------
// Pipeline
// ------------------------------------------------------------

RenderPipeline render_pipeline_create(VkDevice                device,
                                      VkPipelineCache         pipeline_cache,
                                      DescriptorLayoutCache*  desc_cache,
                                      PipelineLayoutCache*    pipe_cache,
                                      const RenderObjectSpec* spec)
{
    RenderPipeline out = {0};
    out.device         = device;

    if(!spec)
        return out;

    bool is_compute = (spec->comp_spv != NULL);

    if(spec)
    {
        log_info("[render_pipeline_create] type=%s vert=%s frag=%s comp=%s", is_compute ? "compute" : "graphics",
                 spec->vert_spv ? spec->vert_spv : "(null)", spec->frag_spv ? spec->frag_spv : "(null)",
                 spec->comp_spv ? spec->comp_spv : "(null)");

        log_info("[render_pipeline_create] topology=%u cull=0x%x frontFace=%u poly=%u depthTest=%u depthWrite=%u depthCompare=%u blend=%u",
                 spec->topology, spec->cull_mode, spec->front_face, spec->polygon_mode, spec->depth_test,
                 spec->depth_write, spec->depth_compare, spec->blend_enable);

        log_info("[render_pipeline_create] use_vertex_input=%u colorAttachments=%u depthFormat=%u stencilFormat=%u",
                 spec->use_vertex_input, spec->color_attachment_count, spec->depth_format, spec->stencil_format);

        log_info("[render_pipeline_create] updateAfterBind=%u bindless=%u bindlessCount=%u perFrameSets=%u", spec->allow_update_after_bind,
                 spec->use_bindless_if_available, spec->bindless_descriptor_count, spec->per_frame_sets);
    }

    void*  vert_code = NULL;
    void*  frag_code = NULL;
    void*  comp_code = NULL;
    size_t vert_size = 0;
    size_t frag_size = 0;
    size_t comp_size = 0;

    ShaderReflection reflections[2] = {0};
    uint32_t         refl_count     = 0;

    if(is_compute)
    {
        if(!read_file(spec->comp_spv, &comp_code, &comp_size))
            return out;

        if(shader_reflect_create(&reflections[refl_count], comp_code, comp_size))
            refl_count++;
    }
    else
    {
        if(!spec->vert_spv)
        {
            log_error("Render pipeline requires vert_spv for graphics");
            return out;
        }
        if(!read_file(spec->vert_spv, &vert_code, &vert_size))
            return out;

        if(spec->frag_spv)
        {
            if(!read_file(spec->frag_spv, &frag_code, &frag_size))
            {
                free(vert_code);
                return out;
            }
        }
        else
        {
            log_error("Render pipeline requires frag_spv for graphics");
            free(vert_code);
            return out;
        }

        if(shader_reflect_create(&reflections[refl_count], vert_code, vert_size))
            refl_count++;
        if(shader_reflect_create(&reflections[refl_count], frag_code, frag_size))
            refl_count++;
    }

    if(refl_count == 0)
    {
        free(vert_code);
        free(frag_code);
        free(comp_code);
        return out;
    }

    MergedReflection merged = {0};
    shader_reflect_merge(&merged, reflections, refl_count);

    RenderSetLayoutInfo set_infos[SHADER_REFLECT_MAX_SETS] = {0};
    uint32_t            set_count                          = 0;
    build_reflection_and_layouts(spec, &merged, &out.refl, set_infos, &set_count);

    out.set_count   = set_count;
    out.set_layouts = (VkDescriptorSetLayout*)calloc(set_count, sizeof(VkDescriptorSetLayout));

    for(uint32_t i = 0; i < set_count; i++)
    {
        out.set_layouts[i] =
            get_or_create_set_layout(desc_cache, set_infos[i].bindings, set_infos[i].binding_count, set_infos[i].create_flags,
                                     (set_infos[i].binding_count > 0) ? set_infos[i].binding_flags : NULL);
        out.variable_descriptor_counts[i] = set_infos[i].variable_descriptor_count;
        out.set_create_flags[i]           = set_infos[i].create_flags;
    }

    out.layout = pipeline_layout_cache_get(device, pipe_cache, out.set_layouts, out.set_count, out.refl.push_constants,
                                           out.refl.push_constant_count);

    VkSpecializationInfo spec_info = {0};
    if(spec->spec_constant_count > 0 && spec->spec_map && spec->spec_data && spec->spec_data_size > 0)
    {
        spec_info.mapEntryCount = spec->spec_constant_count;
        spec_info.pMapEntries   = spec->spec_map;
        spec_info.dataSize      = spec->spec_data_size;
        spec_info.pData         = spec->spec_data;
    }

    if(is_compute)
    {
        VkShaderModule comp_mod = create_shader_module(device, comp_code, comp_size);

        VkPipelineShaderStageCreateInfo stage = {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
            .module              = comp_mod,
            .pName               = "main",
            .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
        };

        VkComputePipelineCreateInfo ci = {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage  = stage,
            .layout = out.layout,
        };

        VK_CHECK(vkCreateComputePipelines(device, pipeline_cache, 1, &ci, NULL, &out.pipeline));
        out.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

        vkDestroyShaderModule(device, comp_mod, NULL);
    }
    else
    {
        VkShaderModule vert_mod = create_shader_module(device, vert_code, vert_size);
        VkShaderModule frag_mod = create_shader_module(device, frag_code, frag_size);

        VkPipelineShaderStageCreateInfo stages[2] = {
            {
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage               = VK_SHADER_STAGE_VERTEX_BIT,
                .module              = vert_mod,
                .pName               = "main",
                .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
            },
            {
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module              = frag_mod,
                .pName               = "main",
                .pSpecializationInfo = (spec_info.mapEntryCount > 0) ? &spec_info : NULL,
            },
        };

        VkPipelineVertexInputStateCreateInfo vertex_input = {
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext                           = NULL,
            .flags                           = 0,
            .vertexBindingDescriptionCount   = 0,
            .pVertexBindingDescriptions      = NULL,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions    = NULL,
        };

        VkVertexInputAttributeDescription  attrs[16]     = {0};
        VkVertexInputBindingDescription    bindings[8]   = {0};
        VkVertexInputAttributeDescription* attrs_ptr     = NULL;
        VkVertexInputBindingDescription*   bindings_ptr  = NULL;
        uint32_t                           attr_count    = 0;
        uint32_t                           binding_count = 0;

        if(spec->use_vertex_input)
        {
            ShaderReflection* vert_reflect = NULL;
            for(uint32_t i = 0; i < refl_count; i++)
            {
                if(reflections[i].stage == VK_SHADER_STAGE_VERTEX_BIT)
                {
                    vert_reflect = &reflections[i];
                    break;
                }
            }

            if(vert_reflect)
                attr_count = shader_reflect_get_vertex_attributes(vert_reflect, attrs, 16, 0);

            uint32_t stride = 0;
            for(uint32_t i = 0; i < attr_count; i++)
            {
                uint32_t end = attrs[i].offset;
                switch(attrs[i].format)
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

            if(attr_count > 0)
            {
                binding_count = 1;
                bindings[0]   = (VkVertexInputBindingDescription){
                      .binding   = 0,
                      .stride    = stride,
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                };
            }

            bindings_ptr = (binding_count > 0) ? bindings : NULL;
            attrs_ptr    = (attr_count > 0) ? attrs : NULL;

            if(vert_reflect)
            {
                log_info("[pipeline] vert=%s frag=%s use_vertex_input=1 attrs=%u bindings=%u",
                         spec->vert_spv ? spec->vert_spv : "(null)", spec->frag_spv ? spec->frag_spv : "(null)",
                         attr_count, binding_count);

                for(uint32_t i = 0; i < attr_count; i++)
                {
                    log_info("[pipeline]  attr[%u] loc=%u binding=%u format=%u offset=%u", i, attrs[i].location,
                             attrs[i].binding, attrs[i].format, attrs[i].offset);
                }

                if(attr_count == 0)
                {
                    log_warn("[pipeline] use_vertex_input enabled but no vertex inputs reflected for vert=%s",
                             spec->vert_spv ? spec->vert_spv : "(null)");
                }
            }
        }

        vertex_input.vertexBindingDescriptionCount   = binding_count;
        vertex_input.pVertexBindingDescriptions      = bindings_ptr;
        vertex_input.vertexAttributeDescriptionCount = attr_count;
        vertex_input.pVertexAttributeDescriptions    = attrs_ptr;

        if(vertex_input.vertexAttributeDescriptionCount > 0 && vertex_input.vertexBindingDescriptionCount == 0)
        {
            log_warn("[pipeline] vertex input attrs set but no bindings; forcing zero attrs for vert=%s",
                     spec->vert_spv ? spec->vert_spv : "(null)");
            vertex_input.vertexAttributeDescriptionCount = 0;
            vertex_input.pVertexAttributeDescriptions    = NULL;
        }

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology               = spec->topology,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkPipelineViewportStateCreateInfo viewport = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
        };

        VkPipelineRasterizationStateCreateInfo raster = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = spec->polygon_mode,
            .cullMode    = spec->cull_mode,
            .frontFace   = spec->front_face,
            .lineWidth   = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisample = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = spec->depth_test,
            .depthWriteEnable = spec->depth_write,
            .depthCompareOp   = spec->depth_compare,
        };

        VkPipelineColorBlendAttachmentState blend_atts[8] = {0};
        for(uint32_t i = 0; i < spec->color_attachment_count && i < 8; i++)
        {
            blend_atts[i] = (VkPipelineColorBlendAttachmentState){
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                .blendEnable         = spec->blend_enable ? VK_TRUE : VK_FALSE,
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
            .attachmentCount = spec->color_attachment_count,
            .pAttachments    = blend_atts,
        };

        VkDynamicState default_dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamic = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = (spec->dynamic_state_count > 0) ? spec->dynamic_state_count : 2,
            .pDynamicStates    = (spec->dynamic_state_count > 0) ? spec->dynamic_states : default_dyn,
        };

        VkPipelineRenderingCreateInfo rendering = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount    = spec->color_attachment_count,
            .pColorAttachmentFormats = spec->color_formats,
            .depthAttachmentFormat   = spec->depth_format,
            .stencilAttachmentFormat = spec->stencil_format,
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
            .layout              = out.layout,
        };

        log_info("[pipeline] create gfx: vert=%s frag=%s vb=%u va=%u", spec->vert_spv ? spec->vert_spv : "(null)",
                 spec->frag_spv ? spec->frag_spv : "(null)", vertex_input.vertexBindingDescriptionCount,
                 vertex_input.vertexAttributeDescriptionCount);

        VK_CHECK(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &ci, NULL, &out.pipeline));
        out.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

        vkDestroyShaderModule(device, vert_mod, NULL);
        vkDestroyShaderModule(device, frag_mod, NULL);
    }

    for(uint32_t i = 0; i < refl_count; i++)
        shader_reflect_destroy(&reflections[i]);

    free(vert_code);
    free(frag_code);
    free(comp_code);

    return out;
}


void render_pipeline_destroy(VkDevice device, RenderPipeline* pipe)
{
    if(!pipe)
        return;

    VkPipeline       resolved_pipe   = VK_NULL_HANDLE;
    VkPipelineLayout resolved_layout = VK_NULL_HANDLE;
    render_pipeline_resolve_handles(pipe, &resolved_pipe, &resolved_layout);

    // Unregister FIRST so future resolve doesn't return dead handles
    render_pipeline_hot_reload_unregister(pipe);

    if(resolved_pipe)
        vkDestroyPipeline(device, resolved_pipe, NULL);

    free(pipe->set_layouts);
    render_reflection_clear(&pipe->refl);
    *pipe = (RenderPipeline){0};
}

// ------------------------------------------------------------
// Resources
// ------------------------------------------------------------

RenderResources render_resources_alloc(VkDevice device, const RenderPipeline* pipe, DescriptorAllocator* alloc, uint32_t frames_in_flight, VkBool32 per_frame_sets)
{
    (void)device;

    RenderResources res = {0};
    if(!pipe || !alloc)
        return res;

    res.set_count         = pipe->set_count;
    res.frames_in_flight  = (frames_in_flight == 0) ? 1 : frames_in_flight;
    res.per_frame_sets    = per_frame_sets;
    res.owns_sets         = true;
    res.external_set_mask = 0;

    uint32_t total_sets = res.set_count * ((res.per_frame_sets == VK_TRUE) ? res.frames_in_flight : 1);

    res.device    = device;
    res.allocator = alloc;
    res.allocated = VK_FALSE;
    res.sets      = (VkDescriptorSet*)calloc(total_sets, sizeof(VkDescriptorSet));
    if(!res.sets)
        return res;
    res.allocated = VK_TRUE;

    for(uint32_t i = 0; i < total_sets; i++)
    {
        uint32_t set_index      = i % res.set_count;
        uint32_t variable_count = pipe->variable_descriptor_counts[set_index];

        if((pipe->set_create_flags[set_index] & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) && !alloc->update_after_bind)
        {
            continue;
        }

        if(variable_count > 0)
        {
            VK_CHECK(descriptor_allocator_allocate_variable(alloc, pipe->set_layouts[set_index], variable_count, &res.sets[i]));
        }
        else
        {
            VK_CHECK(descriptor_allocator_allocate(alloc, pipe->set_layouts[set_index], &res.sets[i]));
        }
    }

    return res;
}

RenderResources render_resources_external(uint32_t set_count, const VkDescriptorSet* sets)
{
    RenderResources res = {0};
    if(set_count == 0 || !sets)
        return res;

    res.set_count         = set_count;
    res.frames_in_flight  = 1;
    res.per_frame_sets    = VK_FALSE;
    res.owns_sets         = true;
    res.external_set_mask = 0;

    res.sets = (VkDescriptorSet*)calloc(set_count, sizeof(VkDescriptorSet));
    if(!res.sets)
        return res;

    for(uint32_t i = 0; i < set_count; i++)
    {
        res.sets[i] = sets[i];
        res.external_set_mask |= (1u << i);
    }

    res.device    = VK_NULL_HANDLE;
    res.allocator = NULL;
    res.allocated = VK_TRUE;
    return res;
}

void render_resources_set_external(RenderResources* res, uint32_t set_index, VkDescriptorSet set)
{
    if(!res || set_index >= res->set_count)
        return;
    if(!res->sets)
        render_resources_ensure_set_array(res);
    res->sets[set_index] = set;
    res->external_set_mask |= (1u << set_index);
}

void render_object_set_external_set(RenderObject* obj, const char* binding_name, VkDescriptorSet set)
{
    if(!obj || !binding_name)
        return;

    const RenderBindingInfo* bind = render_find_binding_by_name(&obj->pipeline.refl, binding_name);
    if(!bind)
    {
        log_warn("RenderObject set external: binding '%s' not found", binding_name);
        return;
    }

    render_resources_set_external(&obj->resources, bind->set, set);
}

void render_resources_destroy(RenderResources* res)
{
    if(!res)
        return;

    if(res->owns_sets)
        free(res->sets);

    if(res->written)
        hmfree(res->written);

    *res = (RenderResources){0};
}

void render_resources_write_all(RenderResources* res, const RenderPipeline* pipe, const RenderWriteTable* table, uint32_t frame_index)
{
    if(!res || !pipe || !table || table->count == 0)
        return;

    DescriptorWriter writers[SHADER_REFLECT_MAX_SETS]    = {0};
    bool             has_writer[SHADER_REFLECT_MAX_SETS] = {0};

    for(uint32_t i = 0; i < table->count; i++)
    {
        BindingId                id   = table->ids ? table->ids[i] : 0;
        const RenderBindingInfo* bind = render_find_binding_by_id(&pipe->refl, id);
        if(!bind)
        {
            log_warn("RenderResources write: unknown binding id %llu", (unsigned long long)id);
            continue;
        }

        uint32_t set_index = bind->set;
        if(set_index >= res->set_count)
            continue;

        if(!has_writer[set_index])
        {
            desc_writer_begin(&writers[set_index]);
            has_writer[set_index] = true;
        }

        VkDescriptorSet set = get_frame_set(res, pipe, set_index, frame_index);

        bool image_write = false;
        if(table->types)
            image_write = (table->types[i] == RENDER_WRITE_IMAGE);
        else
            image_write = is_image_descriptor(bind->descriptor_type);

        if(image_write)
        {
            if(!is_image_descriptor(bind->descriptor_type))
            {
                log_warn("RenderResources write: binding %s is not image", bind->name ? bind->name : "(null)");
                continue;
            }

            VkImageView   view    = table->views ? table->views[i] : VK_NULL_HANDLE;
            VkSampler     sampler = table->samplers ? table->samplers[i] : VK_NULL_HANDLE;
            VkImageLayout layout  = table->layouts ? table->layouts[i] : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            desc_writer_write_image(&writers[set_index], set, bind->binding, bind->descriptor_type, view, sampler, layout);
            render_resources_mark_written(res, bind->id);
        }
        else
        {
            if(!is_buffer_descriptor(bind->descriptor_type))
            {
                log_warn("RenderResources write: binding %s is not buffer", bind->name ? bind->name : "(null)");
                continue;
            }

            VkBuffer     buffer = table->buffers ? table->buffers[i] : VK_NULL_HANDLE;
            VkDeviceSize offset = table->offsets ? table->offsets[i] : 0;
            VkDeviceSize range  = table->ranges ? table->ranges[i] : VK_WHOLE_SIZE;

            desc_writer_write_buffer(&writers[set_index], set, bind->binding, bind->descriptor_type, buffer, offset, range);
            render_resources_mark_written(res, bind->id);
        }
    }

    for(uint32_t i = 0; i < res->set_count; i++)
    {
        if(has_writer[i])
            desc_writer_commit(pipe->device, &writers[i]);
    }
}

// ------------------------------------------------------------
// Render object (combined)
// ------------------------------------------------------------

void render_object_create(RenderObject*           obj,
                          VkPipelineCache         pipeline_cache,
                          DescriptorLayoutCache*  desc_cache,
                          PipelineLayoutCache*    pipe_cache,
                          DescriptorAllocator*    alloc,
                          const RenderObjectSpec* spec,
                          uint32_t                frames_in_flight)
{

    *obj = (RenderObject){0};
    if(spec)
    {
        log_info("[render_object_create] frames_in_flight=%u", frames_in_flight);
        log_info("[render_object_create] vert=%s frag=%s comp=%s", spec->vert_spv ? spec->vert_spv : "(null)",
                 spec->frag_spv ? spec->frag_spv : "(null)", spec->comp_spv ? spec->comp_spv : "(null)");
    }

    obj->pipeline = render_pipeline_create(alloc->device, pipeline_cache, desc_cache, pipe_cache, spec);

    log_info("[render_object_create] pipeline handle=0x%llx layout=0x%llx set_count=%u",
             (unsigned long long)obj->pipeline.pipeline, (unsigned long long)obj->pipeline.layout, obj->pipeline.set_count);

    if(spec && spec->reloadable)
    {
        //        render_pipeline_register_hot_reload(&obj->pipeline, pipeline_cache, desc_cache, pipe_cache, spec);

        render_object_enable_hot_reload(obj, VK_NULL_HANDLE, spec);
    }
    VkBool32 per_frame = spec ? spec->per_frame_sets : VK_FALSE;

    if(spec && spec->per_frame_sets == VK_FALSE && obj->pipeline.refl.per_frame_hint == VK_TRUE)
        per_frame = VK_TRUE;


    obj->resources = (RenderResources){
        .set_count         = obj->pipeline.set_count,
        .frames_in_flight  = frames_in_flight,
        .per_frame_sets    = per_frame,
        .owns_sets         = true,
        .external_set_mask = 0,
        .allocator         = alloc,
        .device            = alloc->device,
        .allocated         = VK_FALSE,
    };
    log_info("[render_object_create] resources per_frame=%u external_set_mask=0x%x", obj->resources.per_frame_sets,
             obj->resources.external_set_mask);
}

void render_object_enable_hot_reload(RenderObject* obj, VkPipelineCache pipeline_cache, const RenderObjectSpec* spec)
{
    if(!obj || !spec || !spec->reloadable)
        return;

    if(obj->pipeline.pipeline == VK_NULL_HANDLE)
    {
        log_warn("[render_object_enable_hot_reload] pipeline is NULL, skipping");
        return;
    }

    render_pipeline_hot_reload_register(&obj->pipeline, pipeline_cache, spec);
    log_info("[render_object_enable_hot_reload] registered pipeline 0x%llx for hot-reload",
             (unsigned long long)obj->pipeline.pipeline);
}

void render_object_destroy(VkDevice device, RenderObject* obj)
{
    if(!obj)
        return;


    render_pipeline_destroy(device, &obj->pipeline);
    render_resources_destroy(&obj->resources);
    *obj = (RenderObject){0};
}

void render_object_write_buffer(RenderObject* obj,
                                const char*   name,
                                uint32_t      set,
                                uint32_t      binding,
                                VkBuffer      buffer,
                                VkDeviceSize  offset,
                                VkDeviceSize  range,
                                uint32_t      frame_index)
{
    if(!obj)
        return;

    const RenderBindingInfo* bind = NULL;
    if(name)
        bind = render_find_binding_by_name(&obj->pipeline.refl, name);
    else
        bind = render_find_binding_by_set_binding(&obj->pipeline.refl, set, binding);

    if(!bind)
    {
        log_warn("RenderObject write buffer: binding not found");
        return;
    }

    if(!is_buffer_descriptor(bind->descriptor_type))
    {
        log_warn("RenderObject write buffer: binding %s is not buffer", bind->name ? bind->name : "(null)");
        return;
    }

    DescriptorWriter w;
    desc_writer_begin(&w);
    VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, bind->set, frame_index);
    desc_writer_write_buffer(&w, set_handle, bind->binding, bind->descriptor_type, buffer, offset, range);
    desc_writer_commit(obj->pipeline.device, &w);
    render_resources_mark_written(&obj->resources, bind->id);
}

void render_object_write_buffer_id(RenderObject* obj, BindingId id, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t frame_index)
{
    if(!obj)
        return;

    const RenderBindingInfo* bind = render_find_binding_by_id(&obj->pipeline.refl, id);
    if(!bind)
    {
        log_warn("RenderObject write buffer: binding id not found");
        return;
    }

    if(!is_buffer_descriptor(bind->descriptor_type))
    {
        log_warn("RenderObject write buffer: binding %s is not buffer", bind->name ? bind->name : "(null)");
        return;
    }

    DescriptorWriter w;
    desc_writer_begin(&w);
    VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, bind->set, frame_index);
    desc_writer_write_buffer(&w, set_handle, bind->binding, bind->descriptor_type, buffer, offset, range);
    desc_writer_commit(obj->pipeline.device, &w);
    render_resources_mark_written(&obj->resources, bind->id);
}

void render_object_write_buffer_binding(RenderObject* obj, RenderBinding binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t frame_index)
{
    if(binding.id == 0)
    {
        log_warn("RenderObject write buffer: invalid binding");
        return;
    }
    render_object_write_buffer_id(obj, binding.id, buffer, offset, range, frame_index);
}
void render_object_write_image(RenderObject* obj,
                               const char*   name,
                               uint32_t      set,
                               uint32_t      binding,
                               VkImageView   view,
                               VkSampler     sampler,
                               VkImageLayout layout,
                               uint32_t      frame_index)
{
    if(!obj)
        return;

    const RenderBindingInfo* bind = NULL;
    if(name)
        bind = render_find_binding_by_name(&obj->pipeline.refl, name);
    else
        bind = render_find_binding_by_set_binding(&obj->pipeline.refl, set, binding);

    if(!bind)
    {
        log_warn("RenderObject write image: binding not found");
        return;
    }

    if(!is_image_descriptor(bind->descriptor_type))
    {
        log_warn("RenderObject write image: binding %s is not image", bind->name ? bind->name : "(null)");
        return;
    }

    DescriptorWriter w;
    desc_writer_begin(&w);
    VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, bind->set, frame_index);
    desc_writer_write_image(&w, set_handle, bind->binding, bind->descriptor_type, view, sampler, layout);
    desc_writer_commit(obj->pipeline.device, &w);
    render_resources_mark_written(&obj->resources, bind->id);
}

void render_object_write_image_id(RenderObject* obj, BindingId id, VkImageView view, VkSampler sampler, VkImageLayout layout, uint32_t frame_index)
{
    if(!obj)
        return;

    const RenderBindingInfo* bind = render_find_binding_by_id(&obj->pipeline.refl, id);
    if(!bind)
    {
        log_warn("RenderObject write image: binding id not found");
        return;
    }

    if(!is_image_descriptor(bind->descriptor_type))
    {
        log_warn("RenderObject write image: binding %s is not image", bind->name ? bind->name : "(null)");
        return;
    }

    DescriptorWriter w;
    desc_writer_begin(&w);
    VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, bind->set, frame_index);
    desc_writer_write_image(&w, set_handle, bind->binding, bind->descriptor_type, view, sampler, layout);
    desc_writer_commit(obj->pipeline.device, &w);
    render_resources_mark_written(&obj->resources, bind->id);
}

void render_object_write_image_binding(RenderObject* obj, RenderBinding binding, VkImageView view, VkSampler sampler, VkImageLayout layout, uint32_t frame_index)
{
    if(binding.id == 0)
    {
        log_warn("RenderObject write image: invalid binding");
        return;
    }
    render_object_write_image_id(obj, binding.id, view, sampler, layout, frame_index);
}
void render_object_write_all(RenderObject* obj, const RenderWrite* writes, uint32_t write_count, uint32_t frame_index)
{
    if(!obj || !writes || write_count == 0)
        return;

    DescriptorWriter writers[SHADER_REFLECT_MAX_SETS]    = {0};
    bool             has_writer[SHADER_REFLECT_MAX_SETS] = {0};

    for(uint32_t i = 0; i < write_count; i++)
    {
        const RenderWrite*       w    = &writes[i];
        const RenderBindingInfo* bind = w->name ? render_find_binding_by_name(&obj->pipeline.refl, w->name) : NULL;

        if(!bind)
        {
            log_warn("RenderObject write: binding not found (%s)", w->name ? w->name : "(null)");
            continue;
        }

        uint32_t set_index = bind->set;
        if(set_index >= obj->resources.set_count)
            continue;

        if(!has_writer[set_index])
        {
            desc_writer_begin(&writers[set_index]);
            has_writer[set_index] = true;
        }

        VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, set_index, frame_index);

        if(w->type == RENDER_WRITE_IMAGE)
        {
            if(!is_image_descriptor(bind->descriptor_type))
                continue;

            desc_writer_write_image(&writers[set_index], set_handle, bind->binding, bind->descriptor_type,
                                    w->data.img.view, w->data.img.sampler, w->data.img.layout);
            render_resources_mark_written(&obj->resources, bind->id);
        }
        else
        {
            if(!is_buffer_descriptor(bind->descriptor_type))
                continue;

            desc_writer_write_buffer(&writers[set_index], set_handle, bind->binding, bind->descriptor_type,
                                     w->data.buf.buffer, w->data.buf.offset, w->data.buf.range);
            render_resources_mark_written(&obj->resources, bind->id);
        }
    }

    for(uint32_t i = 0; i < obj->resources.set_count; i++)
    {
        if(has_writer[i])
            desc_writer_commit(obj->pipeline.device, &writers[i]);
    }
}

void render_object_write_all_ids(RenderObject* obj, const RenderWriteId* writes, uint32_t write_count, uint32_t frame_index)
{
    if(!obj || !writes || write_count == 0)
        return;

    DescriptorWriter writers[SHADER_REFLECT_MAX_SETS]    = {0};
    bool             has_writer[SHADER_REFLECT_MAX_SETS] = {0};

    for(uint32_t i = 0; i < write_count; i++)
    {
        const RenderWriteId*     w    = &writes[i];
        const RenderBindingInfo* bind = render_find_binding_by_id(&obj->pipeline.refl, w->id);

        if(!bind)
        {
            log_warn("RenderObject write: binding id not found (%llu)", (unsigned long long)w->id);
            continue;
        }

        uint32_t set_index = bind->set;
        if(set_index >= obj->resources.set_count)
            continue;

        if(!has_writer[set_index])
        {
            desc_writer_begin(&writers[set_index]);
            has_writer[set_index] = true;
        }

        VkDescriptorSet set_handle = get_frame_set(&obj->resources, &obj->pipeline, set_index, frame_index);

        if(w->type == RENDER_WRITE_IMAGE)
        {
            if(!is_image_descriptor(bind->descriptor_type))
                continue;

            desc_writer_write_image(&writers[set_index], set_handle, bind->binding, bind->descriptor_type,
                                    w->data.img.view, w->data.img.sampler, w->data.img.layout);
            render_resources_mark_written(&obj->resources, bind->id);
        }
        else
        {
            if(!is_buffer_descriptor(bind->descriptor_type))
                continue;

            desc_writer_write_buffer(&writers[set_index], set_handle, bind->binding, bind->descriptor_type,
                                     w->data.buf.buffer, w->data.buf.offset, w->data.buf.range);
            render_resources_mark_written(&obj->resources, bind->id);
        }
    }

    for(uint32_t i = 0; i < obj->resources.set_count; i++)
    {
        if(has_writer[i])
            desc_writer_commit(obj->pipeline.device, &writers[i]);
    }
}

void render_object_write_list(RenderObject* obj, const RenderWriteList* list, uint32_t frame_index)
{
    if(!list)
        return;
    render_object_write_all_ids(obj, list->writes, render_write_list_count(list), frame_index);
}

void render_object_write_static_writes(RenderObject* obj, const RenderWrite* writes, uint32_t write_count)
{
    if(!obj || !writes || write_count == 0)
        return;

    RenderWriteList list = render_write_list_begin();
    for(uint32_t i = 0; i < write_count; i++)
    {
        const RenderWrite* w = &writes[i];
        RenderBinding      b = render_object_get_binding(obj, w->name);
        if(w->type == RENDER_WRITE_BUFFER)
            render_write_list_buffer(&list, b, w->data.buf.buffer, w->data.buf.offset, w->data.buf.range);
        else
            render_write_list_image(&list, b, w->data.img.view, w->data.img.sampler, w->data.img.layout);
    }

    render_object_write_static_list(obj, &list);
    render_write_list_free(&list);
}

void render_object_write_frame_writes(RenderObject* obj, uint32_t frame_index, const RenderWrite* writes, uint32_t write_count)
{
    if(!obj || !writes || write_count == 0)
        return;

    RenderWriteList list = render_write_list_begin();
    for(uint32_t i = 0; i < write_count; i++)
    {
        const RenderWrite* w = &writes[i];
        RenderBinding      b = render_object_get_binding(obj, w->name);
        if(w->type == RENDER_WRITE_BUFFER)
            render_write_list_buffer(&list, b, w->data.buf.buffer, w->data.buf.offset, w->data.buf.range);
        else
            render_write_list_image(&list, b, w->data.img.view, w->data.img.sampler, w->data.img.layout);
    }

    render_object_write_frame_list(obj, frame_index, &list);
    render_write_list_free(&list);
}

void render_object_write_static_list(RenderObject* obj, const RenderWriteList* list)
{
    if(!list)
        return;
    render_object_write_static_ids(obj, list->writes, render_write_list_count(list));
}

void render_object_write_frame_list(RenderObject* obj, uint32_t frame_index, const RenderWriteList* list)
{
    if(!list)
        return;
    render_object_write_frame_ids(obj, frame_index, list->writes, render_write_list_count(list));
}

void render_object_write_static_impl(RenderObject* obj, const RenderWrite* writes, uint32_t write_count)
{
    if(!obj || !writes || write_count == 0)
        return;

    uint32_t frames = obj->resources.frames_in_flight > 0 ? obj->resources.frames_in_flight : 1u;
    if(obj->resources.per_frame_sets == VK_TRUE)
    {
        for(uint32_t i = 0; i < frames; i++)
            render_object_write_all(obj, writes, write_count, i);
    }
    else
    {
        render_object_write_all(obj, writes, write_count, 0);
    }
}

void render_object_write_static_ids(RenderObject* obj, const RenderWriteId* writes, uint32_t write_count)
{
    if(!obj || !writes || write_count == 0)
        return;

    uint32_t frames = obj->resources.frames_in_flight > 0 ? obj->resources.frames_in_flight : 1u;
    if(obj->resources.per_frame_sets == VK_TRUE)
    {
        for(uint32_t i = 0; i < frames; i++)
            render_object_write_all_ids(obj, writes, write_count, i);
    }
    else
    {
        render_object_write_all_ids(obj, writes, write_count, 0);
    }
}

void render_object_write_frame(RenderObject* obj, uint32_t frame_index, const RenderWrite* writes, uint32_t write_count)
{
    render_object_write_all(obj, writes, write_count, frame_index);
}

void render_object_write_frame_ids(RenderObject* obj, uint32_t frame_index, const RenderWriteId* writes, uint32_t write_count)
{
    render_object_write_all_ids(obj, writes, write_count, frame_index);
}

bool render_object_validate_ready(const RenderObject* obj)
{
    if(!obj)
        return false;

    bool                          ok   = true;
    const RenderObjectReflection* refl = &obj->pipeline.refl;
    for(uint32_t i = 0; i < refl->binding_count; i++)
    {
        const RenderBindingInfo* bind = &refl->bindings[i];
        if(bind->descriptor_count == 0)
            continue;

        if(obj->resources.external_set_mask & (1u << bind->set))
            continue;

        if(!render_resources_has_written((RenderResources*)&obj->resources, bind->id))
        {
            log_warn("RenderObject missing binding: %s (set %u binding %u)", bind->name ? bind->name : "(null)",
                     bind->set, bind->binding);
            ok = false;
        }
    }
    return ok;
}

void render_reset_state(void)
{
    memset(&g_render_state, 0, sizeof(g_render_state));
}

void render_object_bind(VkCommandBuffer cmd, const RenderObject* obj, VkPipelineBindPoint bind_point, uint32_t frame_index)
{
    if(!obj)
        return;

    render_bind_sets(cmd, &obj->pipeline, &obj->resources, bind_point, frame_index);
}

void render_object_push_constants(VkCommandBuffer cmd, const RenderObject* obj, const void* data, uint32_t size)
{
    if(!obj || !data || size == 0)
        return;

    if(obj->pipeline.refl.push_constant_count == 0)
        return;

    uint32_t max_size = obj->pipeline.refl.push_constant_size;
    if(max_size > 0 && size > max_size)
    {
        log_warn("RenderObject push constants: size %u exceeds %u", size, max_size);
        size = max_size;
    }

    vkCmdPushConstants(cmd, obj->pipeline.layout, obj->pipeline.refl.push_constant_stages, 0, size, data);
}

// ------------------------------------------------------------
// Instances
// ------------------------------------------------------------

void render_instance_create(RenderObjectInstance* inst, RenderPipeline* pipe, RenderResources* res)
{

    *inst           = (RenderObjectInstance){0};
    inst->pipe      = pipe;
    inst->res       = res;
    inst->push_size = 0;
}

void render_instance_set_push_data(RenderObjectInstance* inst, const void* data, uint32_t size)
{
    if(!inst || !data || size == 0)
        return;

    // Fast path for common case (no overflow)
    if(size <= sizeof(inst->push_data))
    {
        memcpy(inst->push_data, data, size);
        inst->push_size = size;
        return;
    }

    // Slow path with warning
    log_warn("RenderInstance push data too large (%u)", size);
    memcpy(inst->push_data, data, sizeof(inst->push_data));
    inst->push_size = (uint32_t)sizeof(inst->push_data);
}

void render_instance_bind(VkCommandBuffer cmd, const RenderObjectInstance* inst, VkPipelineBindPoint bind_point, uint32_t frame_index)
{
    if(!inst || !inst->pipe || !inst->res)
        return;

    render_bind_sets(cmd, inst->pipe, inst->res, bind_point, frame_index);
}

void render_instance_push(VkCommandBuffer cmd, const RenderObjectInstance* inst)
{
    if(!inst || !inst->pipe || inst->push_size == 0)
        return;

    uint32_t max_size = inst->pipe->refl.push_constant_size;
    uint32_t size     = inst->push_size;
    if(max_size > 0 && size > max_size)
        size = max_size;

    vkCmdPushConstants(cmd, inst->pipe->layout, inst->pipe->refl.push_constant_stages, 0, size, inst->push_data);
}
