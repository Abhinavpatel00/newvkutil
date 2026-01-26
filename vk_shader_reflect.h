#ifndef VK_SHADER_REFLECT_H_
#define VK_SHADER_REFLECT_H_

#include "vk_defaults.h"
#include "vk_descriptor.h"
#include "vk_pipeline_layout.h"

#include "external/SPIRV-Reflect/spirv_reflect.h"

// Maximum limits for reflection
#define SHADER_REFLECT_MAX_SETS      8
#define SHADER_REFLECT_MAX_BINDINGS  32
#define SHADER_REFLECT_MAX_PUSH      4
#define SHADER_REFLECT_MAX_INPUTS    16

// -------- Reflected shader data --------

typedef struct ReflectedBinding
{
    uint32_t             binding;
    VkDescriptorType     descriptor_type;
    uint32_t             descriptor_count;
    VkShaderStageFlags   stage_flags;
    const char*          name;
} ReflectedBinding;

typedef struct ReflectedDescriptorSet
{
    uint32_t         set_index;
    uint32_t         binding_count;
    ReflectedBinding bindings[SHADER_REFLECT_MAX_BINDINGS];
} ReflectedDescriptorSet;

typedef struct ReflectedPushConstant
{
    uint32_t           offset;
    uint32_t           size;
    VkShaderStageFlags stage_flags;
    const char*        name;
} ReflectedPushConstant;

typedef struct ReflectedVertexInput
{
    uint32_t   location;
    VkFormat   format;
    uint32_t   offset;
    const char* name;
} ReflectedVertexInput;

typedef struct ShaderReflection
{
    SpvReflectShaderModule  module;
    VkShaderStageFlagBits   stage;

    uint32_t                set_count;
    ReflectedDescriptorSet  sets[SHADER_REFLECT_MAX_SETS];

    uint32_t                push_constant_count;
    ReflectedPushConstant   push_constants[SHADER_REFLECT_MAX_PUSH];

    uint32_t                vertex_input_count;
    ReflectedVertexInput    vertex_inputs[SHADER_REFLECT_MAX_INPUTS];

    // Entry point info
    const char*             entry_point;

    // Compute shader local size
    uint32_t                local_size_x;
    uint32_t                local_size_y;
    uint32_t                local_size_z;
} ShaderReflection;


// -------- Merged reflection for multiple shaders --------

typedef struct MergedReflection
{
    uint32_t                set_count;
    ReflectedDescriptorSet  sets[SHADER_REFLECT_MAX_SETS];

    uint32_t                push_constant_count;
    VkPushConstantRange     push_constants[SHADER_REFLECT_MAX_PUSH];
} MergedReflection;


// -------- API Functions --------

// Create shader reflection from SPIR-V bytecode
// Returns true on success
bool shader_reflect_create(ShaderReflection* reflection,
                           const void*       spirv_code,
                           size_t            spirv_size);

// Destroy shader reflection and free resources
void shader_reflect_destroy(ShaderReflection* reflection);

// Merge multiple shader reflections (e.g., vertex + fragment)
// This combines descriptor sets and push constants with proper stage flags
void shader_reflect_merge(MergedReflection*       merged,
                          const ShaderReflection* reflections,
                          uint32_t                reflection_count);

// Create VkDescriptorSetLayoutBinding array from reflected set
// Returns the number of bindings written
uint32_t shader_reflect_get_set_layout_bindings(const ReflectedDescriptorSet*    set,
                                                 VkDescriptorSetLayoutBinding*   out_bindings,
                                                 uint32_t                        max_bindings);

// Create descriptor set layouts from merged reflection using cache
void shader_reflect_create_set_layouts(VkDevice                device,
                                       DescriptorLayoutCache*  cache,
                                       const MergedReflection* merged,
                                       VkDescriptorSetLayout*  out_layouts,
                                       uint32_t*               out_layout_count);

// Create pipeline layout from merged reflection using caches
VkPipelineLayout shader_reflect_create_pipeline_layout(VkDevice                device,
                                                       DescriptorLayoutCache*  desc_cache,
                                                       PipelineLayoutCache*    pipe_cache,
                                                       const MergedReflection* merged);

// Convenience: create pipeline layout directly from shader SPIRVs
VkPipelineLayout shader_reflect_build_pipeline_layout(VkDevice               device,
                                                      DescriptorLayoutCache* desc_cache,
                                                      PipelineLayoutCache*   pipe_cache,
                                                      const void* const*     spirv_codes,
                                                      const size_t*          spirv_sizes,
                                                      uint32_t               shader_count);

// Get vertex input attribute descriptions from reflection
uint32_t shader_reflect_get_vertex_attributes(const ShaderReflection*             reflection,
                                               VkVertexInputAttributeDescription* out_attrs,
                                               uint32_t                           max_attrs,
                                               uint32_t                           binding);

// Print reflection info for debugging
void shader_reflect_print(const ShaderReflection* reflection);

#endif // VK_SHADER_REFLECT_H_
