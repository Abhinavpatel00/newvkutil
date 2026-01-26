#pragma once

#include "desc_write.h"
#include "vk_defaults.h"
#include "vk_descriptor.h"
#include "vk_pipeline_layout.h"
#include "vk_pipelines.h"
#include "vk_shader_reflect.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t BindingId;

typedef struct RenderBinding
{
    BindingId        id;
    uint32_t         set;
    uint32_t         binding;
    VkDescriptorType descriptor_type;
} RenderBinding;

typedef struct BindingWriteState
{
    BindingId key;
    uint8_t   value;
} BindingWriteState;

// ------------------------------------------------------------
// Writes
// ------------------------------------------------------------

typedef enum RenderWriteType
{
    RENDER_WRITE_BUFFER,
    RENDER_WRITE_IMAGE,
} RenderWriteType;

typedef struct RenderWrite
{
    const char*     name;  // shader binding name (optional)
    RenderWriteType type;
    union
    {
        struct
        {
            VkBuffer     buffer;
            VkDeviceSize offset;
            VkDeviceSize range;
        } buf;
        struct
        {
            VkImageView   view;
            VkSampler     sampler;
            VkImageLayout layout;
        } img;
    } data;
} RenderWrite;

#define RW_BUF(bind_name, buffer_handle, range) \
    (RenderWrite){ .type = RENDER_WRITE_BUFFER, .name = (bind_name), .data.buf = { (buffer_handle), 0, (range) } }

#define RW_BUF_O(bind_name, buffer_handle, offset_bytes, range) \
    (RenderWrite){ .type = RENDER_WRITE_BUFFER, .name = (bind_name), .data.buf = { (buffer_handle), (offset_bytes), (range) } }

#define RW_IMG(bind_name, image_view, image_sampler, image_layout) \
    (RenderWrite){ .type = RENDER_WRITE_IMAGE, .name = (bind_name), .data.img = { (image_view), (image_sampler), (image_layout) } }

typedef struct RenderWriteId
{
    BindingId       id;
    RenderWriteType type;
    union
    {
        struct
        {
            VkBuffer     buffer;
            VkDeviceSize offset;
            VkDeviceSize range;
        } buf;
        struct
        {
            VkImageView   view;
            VkSampler     sampler;
            VkImageLayout layout;
        } img;
    } data;
} RenderWriteId;

typedef struct RenderWriteTable
{
    uint32_t         count;
    BindingId*       ids;     // size: count
    RenderWriteType* types;   // size: count

    VkBuffer*     buffers;  // size: count
    VkDeviceSize* offsets;  // size: count
    VkDeviceSize* ranges;   // size: count

    VkImageView*   views;    // size: count
    VkSampler*     samplers; // size: count
    VkImageLayout* layouts;  // size: count
} RenderWriteTable;

typedef struct RenderWriteList
{
    RenderWriteId* writes; // stb_ds dynamic array
} RenderWriteList;

// ------------------------------------------------------------
// Spec
// ------------------------------------------------------------

typedef struct RenderObjectSpec
{
    // Shaders (SPIR-V paths)
    const char* vert_spv;
    const char* frag_spv; // optional for graphics
    const char* comp_spv; // optional for compute

    // Fixed-function hints
    VkPrimitiveTopology topology;
    VkCullModeFlags     cull_mode;
    VkFrontFace         front_face;
    VkCompareOp         depth_compare;
    VkBool32            depth_test;
    VkBool32            depth_write;
    VkPolygonMode       polygon_mode;
    VkBool32            blend_enable;
    VkBool32            use_vertex_input;

    // Dynamic rendering formats
    uint32_t        color_attachment_count;
    const VkFormat* color_formats;
    VkFormat        depth_format;
    VkFormat        stencil_format;

    // Descriptor behavior
    VkBool32 allow_update_after_bind;
    VkBool32 use_bindless_if_available;
    VkBool32 per_frame_sets;
    uint32_t bindless_descriptor_count;
    VkBool32 reloadable;

    // Dynamic states (optional)
    uint32_t              dynamic_state_count;
    const VkDynamicState* dynamic_states;

    // Specialization constants (optional)
    uint32_t                       spec_constant_count;
    const VkSpecializationMapEntry* spec_map;
    const void*                     spec_data;
    uint32_t                         spec_data_size;
} RenderObjectSpec;

static inline RenderObjectSpec render_object_spec_default(void)
{
    return (RenderObjectSpec){
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .cull_mode              = VK_CULL_MODE_NONE,
        .front_face             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depth_compare          = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .depth_test             = VK_FALSE,
        .depth_write            = VK_FALSE,
        .polygon_mode           = VK_POLYGON_MODE_FILL,
        .blend_enable           = VK_TRUE,
        .use_vertex_input       = true,
        .color_attachment_count = 1,
        .color_formats          = NULL,
        .depth_format           = VK_FORMAT_UNDEFINED,
        .stencil_format         = VK_FORMAT_UNDEFINED,
        .allow_update_after_bind    = VK_FALSE,
        .use_bindless_if_available  = VK_FALSE,
        .per_frame_sets             = VK_FALSE,
        .bindless_descriptor_count  = 0,
        .reloadable                 = VK_FALSE,
        .dynamic_state_count        = 0,
        .dynamic_states             = NULL,
        .spec_constant_count        = 0,
        .spec_map                   = NULL,
        .spec_data                  = NULL,
        .spec_data_size             = 0,
    };
}

RenderObjectSpec render_object_spec_from_config(const GraphicsPipelineConfig* cfg);

// ------------------------------------------------------------
// Reflection data
// ------------------------------------------------------------

typedef struct RenderBindingInfo
{
    const char*               name;
    BindingId                 id;
    uint32_t                  set;
    uint32_t                  binding;
    VkDescriptorType          descriptor_type;
    uint32_t                  descriptor_count;
    VkShaderStageFlags        stage_flags;
    VkDescriptorBindingFlags  binding_flags;
} RenderBindingInfo;

typedef struct RenderObjectReflection
{
    uint32_t           set_count;
    RenderBindingInfo* bindings; // stb_ds dynamic array
    uint32_t           binding_count;

    uint32_t            push_constant_count;
    VkPushConstantRange push_constants[SHADER_REFLECT_MAX_PUSH];
    uint32_t            push_constant_size;
    VkShaderStageFlags  push_constant_stages; // cached OR of all push constant stage flags

    VkBool32 per_frame_hint;
} RenderObjectReflection;

// ------------------------------------------------------------
// Pipeline + Resources
// ------------------------------------------------------------

typedef struct RenderPipeline
{
    VkDevice          device;
    VkPipeline        pipeline;
    VkPipelineLayout  layout;
    VkDescriptorSetLayout* set_layouts;
    uint32_t          set_count;
    VkPipelineBindPoint bind_point;

    VkDescriptorSetLayoutCreateFlags set_create_flags[SHADER_REFLECT_MAX_SETS];
    uint32_t          variable_descriptor_counts[SHADER_REFLECT_MAX_SETS];
    RenderObjectReflection refl;
} RenderPipeline;

typedef struct RenderResources
{
    VkDescriptorSet* sets;
    uint32_t         set_count;
    uint32_t         frames_in_flight;
    VkBool32         per_frame_sets;
    bool             owns_sets;
    uint32_t         external_set_mask;
    BindingWriteState* written; // stb_ds hash set by BindingId
    DescriptorAllocator* allocator;
    VkDevice             device;
    VkBool32             allocated;
} RenderResources;

typedef struct RenderObjectInstance
{
    RenderPipeline*  pipe;
    RenderResources* res;
    uint8_t          push_data[256];
    uint32_t         push_size;
} RenderObjectInstance;

typedef struct RenderObject
{
    RenderPipeline  pipeline;
    RenderResources resources;
} RenderObject;

// ------------------------------------------------------------
// API
// ------------------------------------------------------------

BindingId render_bind_id(const char* name);

RenderBinding render_object_get_binding(const RenderObject* obj, const char* name);

RenderWriteList render_write_list_begin(void);
void render_write_list_reset(RenderWriteList* list);
void render_write_list_free(RenderWriteList* list);
uint32_t render_write_list_count(const RenderWriteList* list);
void render_write_list_buffer(RenderWriteList* list, RenderBinding binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
void render_write_list_image(RenderWriteList* list, RenderBinding binding, VkImageView view, VkSampler sampler, VkImageLayout layout);

RenderPipeline render_pipeline_create(VkDevice               device,
                                     VkPipelineCache        pipeline_cache,
                                     DescriptorLayoutCache* desc_cache,
                                     PipelineLayoutCache*   pipe_cache,
                                     const RenderObjectSpec* spec);

// Shader hot reload (no-op unless any reloadable pipelines are registered)
void render_pipeline_hot_reload_update(void);

void render_pipeline_destroy(VkDevice device, RenderPipeline* pipe);

RenderResources render_resources_alloc(VkDevice                   device,
                                       const RenderPipeline*      pipe,
                                       DescriptorAllocator*       alloc,
                                       uint32_t                   frames_in_flight,
                                       VkBool32                   per_frame_sets);

RenderResources render_resources_external(uint32_t set_count, const VkDescriptorSet* sets);

void render_resources_set_external(RenderResources* res, uint32_t set_index, VkDescriptorSet set);

void render_object_set_external_set(RenderObject* obj, const char* binding_name, VkDescriptorSet set);

void render_resources_destroy(RenderResources* res);

void render_resources_write_all(RenderResources* res,
                                const RenderPipeline* pipe,
                                const RenderWriteTable* table,
                                uint32_t frame_index);

RenderObject render_object_create(VkDevice               device,
                                  VkPipelineCache        pipeline_cache,
                                  DescriptorLayoutCache* desc_cache,
                                  PipelineLayoutCache*   pipe_cache,
                                  DescriptorAllocator*   alloc,
                                  const RenderObjectSpec* spec,
                                  uint32_t                frames_in_flight);

void render_object_destroy(VkDevice device, RenderObject* obj);

void render_object_write_buffer(RenderObject*   obj,
                                const char*     name,
                                uint32_t        set,
                                uint32_t        binding,
                                VkBuffer        buffer,
                                VkDeviceSize    offset,
                                VkDeviceSize    range,
                                uint32_t        frame_index);

void render_object_write_buffer_id(RenderObject*   obj,
                                   BindingId       id,
                                   VkBuffer        buffer,
                                   VkDeviceSize    offset,
                                   VkDeviceSize    range,
                                   uint32_t        frame_index);

void render_object_write_buffer_binding(RenderObject*   obj,
                                        RenderBinding   binding,
                                        VkBuffer        buffer,
                                        VkDeviceSize    offset,
                                        VkDeviceSize    range,
                                        uint32_t        frame_index);

void render_object_write_image(RenderObject*   obj,
                               const char*     name,
                               uint32_t        set,
                               uint32_t        binding,
                               VkImageView     view,
                               VkSampler       sampler,
                               VkImageLayout   layout,
                               uint32_t        frame_index);

void render_object_write_image_id(RenderObject*   obj,
                                  BindingId       id,
                                  VkImageView     view,
                                  VkSampler       sampler,
                                  VkImageLayout   layout,
                                  uint32_t        frame_index);

void render_object_write_image_binding(RenderObject*   obj,
                                       RenderBinding   binding,
                                       VkImageView     view,
                                       VkSampler       sampler,
                                       VkImageLayout   layout,
                                       uint32_t        frame_index);

void render_object_write_all(RenderObject* obj, const RenderWrite* writes, uint32_t write_count, uint32_t frame_index);

void render_object_write_all_ids(RenderObject* obj, const RenderWriteId* writes, uint32_t write_count, uint32_t frame_index);

void render_object_write_static_writes(RenderObject* obj, const RenderWrite* writes, uint32_t write_count);

void render_object_write_frame_writes(RenderObject* obj, uint32_t frame_index, const RenderWrite* writes, uint32_t write_count);

void render_object_write_list(RenderObject* obj, const RenderWriteList* list, uint32_t frame_index);

void render_object_write_static_list(RenderObject* obj, const RenderWriteList* list);

void render_object_write_frame_list(RenderObject* obj, uint32_t frame_index, const RenderWriteList* list);

void render_object_write_static(RenderObject* obj, const RenderWrite* writes, uint32_t write_count);

void render_object_write_static_ids(RenderObject* obj, const RenderWriteId* writes, uint32_t write_count);

void render_object_write_frame(RenderObject* obj, uint32_t frame_index, const RenderWrite* writes, uint32_t write_count);

void render_object_write_frame_ids(RenderObject* obj, uint32_t frame_index, const RenderWriteId* writes, uint32_t write_count);

bool render_object_validate_ready(const RenderObject* obj);

// Performance: Call at start of command buffer to reset state tracking cache
void render_reset_state(void);

void render_object_bind(VkCommandBuffer cmd,
                        const RenderObject* obj,
                        VkPipelineBindPoint bind_point,
                        uint32_t frame_index);

void render_object_push_constants(VkCommandBuffer cmd,
                                  const RenderObject* obj,
                                  const void* data,
                                  uint32_t size);

RenderObjectInstance render_instance_create(RenderPipeline* pipe, RenderResources* res);
void render_instance_set_push_data(RenderObjectInstance* inst, const void* data, uint32_t size);
void render_instance_bind(VkCommandBuffer cmd, const RenderObjectInstance* inst, VkPipelineBindPoint bind_point, uint32_t frame_index);
void render_instance_push(VkCommandBuffer cmd, const RenderObjectInstance* inst);

#ifdef __cplusplus
}
#endif
