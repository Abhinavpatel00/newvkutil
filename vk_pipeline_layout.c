#include "vk_pipeline_layout.h"

static Hash64 hash_pipeline_layout_key(const PipelineLayoutKey* k)
{
    Hash64 h = 0;
    h ^= hash64_bytes(k->set_layouts, k->set_layout_count * sizeof(VkDescriptorSetLayout));
    h ^= hash64_bytes(k->push_constants, k->push_constant_count * sizeof(VkPushConstantRange));
    h ^= (Hash64)k->set_layout_count;
    h ^= ((Hash64)k->push_constant_count) << 32;
    return h;
}

void pipeline_layout_cache_init(PipelineLayoutCache* cache)
{
    cache->entries = NULL;
}

VkPipelineLayout pipeline_layout_cache_get(VkDevice                     device,
                                           PipelineLayoutCache*         cache,
                                           const VkDescriptorSetLayout* set_layouts,
                                           uint32_t                     set_layout_count,
                                           const VkPushConstantRange*   push_ranges,
                                           uint32_t                     push_range_count)
{
    PipelineLayoutKey key = {0};

    key.set_layout_count = set_layout_count;
    memcpy(key.set_layouts, set_layouts, set_layout_count * sizeof(VkDescriptorSetLayout));

    key.push_constant_count = push_range_count;
    if(push_range_count)
        memcpy(key.push_constants, push_ranges, push_range_count * sizeof(VkPushConstantRange));

    key.hash = hash_pipeline_layout_key(&key);

    for(int i = 0; i < arrlen(cache->entries); i++)
    {
        PipelineLayoutEntry* e = &cache->entries[i];

        if(e->key.hash == key.hash &&
           e->key.set_layout_count == key.set_layout_count &&
           e->key.push_constant_count == key.push_constant_count &&
           memcmp(e->key.set_layouts, key.set_layouts, key.set_layout_count * sizeof(VkDescriptorSetLayout)) == 0 &&
           memcmp(e->key.push_constants, key.push_constants, key.push_constant_count * sizeof(VkPushConstantRange)) == 0)
        {
            return e->layout;
        }
    }

    VkPipelineLayoutCreateInfo info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = set_layout_count,
        .pSetLayouts            = set_layouts,
        .pushConstantRangeCount = push_range_count,
        .pPushConstantRanges    = push_ranges,
    };

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &info, NULL, &layout));

    PipelineLayoutEntry entry = {
        .key = key,
        .layout = layout,
    };
    arrpush(cache->entries, entry);

    return layout;
}

void pipeline_layout_cache_destroy(VkDevice device, PipelineLayoutCache* cache)
{
    for(int i = 0; i < arrlen(cache->entries); i++)
        vkDestroyPipelineLayout(device, cache->entries[i].layout, NULL);

    arrfree(cache->entries);
}

// bindless-capable builder
VkPipelineLayout pipeline_layout_cache_build(
    VkDevice                                   device,
    DescriptorLayoutCache*                     desc_cache,
    PipelineLayoutCache*                       pipe_cache,
    const VkDescriptorSetLayoutBinding* const* set_bindings,
    const uint32_t*                            binding_counts,
    const VkDescriptorSetLayoutCreateFlags*    set_create_flags,
    const VkDescriptorBindingFlags* const*     set_binding_flags,
    uint32_t                                   set_count,
    const VkPushConstantRange*                 push_ranges,
    uint32_t                                   push_count)
{
    VkDescriptorSetLayout layouts[VK_MAX_PIPELINE_SETS] = {0};

    for(uint32_t i = 0; i < set_count; i++)
    {
        VkDescriptorSetLayoutCreateFlags flags = 0;
        if(set_create_flags)
            flags = set_create_flags[i];

        const VkDescriptorBindingFlags* bflags = NULL;
        if(set_binding_flags)
            bflags = set_binding_flags[i];

        layouts[i] = get_or_create_set_layout(
            desc_cache,
            set_bindings[i],
            binding_counts[i],
            flags,
            bflags);
    }

    return pipeline_layout_cache_get(device, pipe_cache, layouts, set_count, push_ranges, push_count);
}

