#ifndef VK_PIPELINE_LAYOUT_H_
#define VK_PIPELINE_LAYOUT_H_

#include "vk_defaults.h"
#include "vk_descriptor.h"


#ifndef VK_MAX_PIPELINE_SETS
#define VK_MAX_PIPELINE_SETS 8
#endif

#ifndef VK_MAX_PUSH_RANGES
#define VK_MAX_PUSH_RANGES 4
#endif

typedef uint64_t Hash64;
Hash64           hash64_bytes(const void* data, size_t size);

typedef struct PipelineLayoutKey
{
    VkDescriptorSetLayout set_layouts[VK_MAX_PIPELINE_SETS];
    uint32_t              set_layout_count;

    VkPushConstantRange push_constants[VK_MAX_PUSH_RANGES];
    uint32_t            push_constant_count;

    Hash64 hash;
} PipelineLayoutKey;

typedef struct PipelineLayoutEntry
{
    PipelineLayoutKey key;
    VkPipelineLayout  layout;
} PipelineLayoutEntry;

typedef struct PipelineLayoutCache
{
    PipelineLayoutEntry* entries;  // stretchy buffer
} PipelineLayoutCache;

void pipeline_layout_cache_init(PipelineLayoutCache* cache);

VkPipelineLayout pipeline_layout_cache_get(VkDevice                     device,
                                           PipelineLayoutCache*         cache,
                                           const VkDescriptorSetLayout* set_layouts,
                                           uint32_t                     set_layout_count,
                                           const VkPushConstantRange*   push_ranges,
                                           uint32_t                     push_range_count);

void pipeline_layout_cache_destroy(VkDevice device, PipelineLayoutCache* cache);

// bindless-capable builder
VkPipelineLayout pipeline_layout_cache_build(VkDevice                                   device,
                                                DescriptorLayoutCache*                     desc_cache,
                                                PipelineLayoutCache*                       pipe_cache,
                                                const VkDescriptorSetLayoutBinding* const* set_bindings,
                                                const uint32_t*                            binding_counts,
                                                const VkDescriptorSetLayoutCreateFlags* set_create_flags,  // optional, can be NULL
                                                const VkDescriptorBindingFlags* const* set_binding_flags,  // optional, can be NULL
                                                uint32_t                   set_count,
                                                const VkPushConstantRange* push_ranges,
                                                uint32_t                   push_count);


#endif  // VK_PIPELINE_LAYOUT_H_
