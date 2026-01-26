#include "vk_defaults.h"


#ifndef VK_DESCRIPTOR_H_
#define VK_DESCRIPTOR_H_

#ifndef VK_DESC_MAX_BINDINGS
#define VK_DESC_MAX_BINDINGS 32
#endif
#define MAX_BINDLESS_TEXTURES 2048
// ------------------------------------------------------------
// Layout Cache
// ------------------------------------------------------------

typedef struct DescriptorLayoutKey
{
    uint32_t binding_count;

    VkDescriptorSetLayoutBinding bindings[VK_DESC_MAX_BINDINGS];

    VkDescriptorSetLayoutCreateFlags create_flags;

    // Optional, comes from VkDescriptorSetLayoutBindingFlagsCreateInfo
    VkDescriptorBindingFlags binding_flags[VK_DESC_MAX_BINDINGS];

    uint32_t hash;
} DescriptorLayoutKey;

// cache entry
typedef struct DescriptorLayoutEntry
{
    DescriptorLayoutKey   key;
    VkDescriptorSetLayout layout;
} DescriptorLayoutEntry;

typedef struct DescriptorLayoutCache
{
    VkDevice               device;
    DescriptorLayoutEntry* entries;  // stretchy buffer
} DescriptorLayoutCache;

// layout cache API
void descriptor_layout_cache_init(DescriptorLayoutCache* cache, VkDevice device);
void descriptor_layout_cache_destroy(DescriptorLayoutCache* cache);

VkDescriptorSetLayout descriptor_layout_cache_get(DescriptorLayoutCache* cache, const VkDescriptorSetLayoutCreateInfo* info);

// helpers
VkDescriptorSetLayout get_or_create_set_layout(DescriptorLayoutCache*              cache,
                                               const VkDescriptorSetLayoutBinding* bindings,
                                               uint32_t                            binding_count,
                                               VkDescriptorSetLayoutCreateFlags    create_flags,
                                               const VkDescriptorBindingFlags*     binding_flags /* can be NULL */);

// ------------------------------------------------------------
// Descriptor Allocator
// ------------------------------------------------------------

typedef struct DescriptorPoolChunk
{
    VkDescriptorPool pool;
    float            scale;
} DescriptorPoolChunk;

typedef struct DescriptorAllocator
{
    VkDevice device;

    // if true, pools are created with VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
    bool update_after_bind;

    DescriptorPoolChunk* pools;  // stretchy buffer
} DescriptorAllocator;

void descriptor_allocator_init(DescriptorAllocator* alloc, VkDevice device, bool update_after_bind);
void descriptor_allocator_destroy(DescriptorAllocator* alloc);
void descriptor_allocator_reset(DescriptorAllocator* alloc);

VkResult descriptor_allocator_allocate(DescriptorAllocator* alloc, VkDescriptorSetLayout layout, VkDescriptorSet* out_set);

// For bindless (variable descriptor count)
VkResult descriptor_allocator_allocate_variable(DescriptorAllocator*  alloc,
                                                VkDescriptorSetLayout layout,
                                                uint32_t              variable_descriptor_count,
                                                VkDescriptorSet*      out_set);

#endif  // VK_DESCRIPTOR_H_
