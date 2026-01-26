
#include "vk_descriptor.h"

// ------------------------------------------------------------
// pNext scan
// ------------------------------------------------------------

static const VkDescriptorSetLayoutBindingFlagsCreateInfo* find_binding_flags_info(const void* pNext)
{
    const VkBaseInStructure* it = (const VkBaseInStructure*)pNext;
    while(it)
    {
        if(it->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO)
            return (const VkDescriptorSetLayoutBindingFlagsCreateInfo*)it;
        it = it->pNext;
    }
    return NULL;
}

// ------------------------------------------------------------
// stable sort (binding order)
// ------------------------------------------------------------

static void sort_bindings_in_place(VkDescriptorSetLayoutBinding* bindings, VkDescriptorBindingFlags* flags, uint32_t count)
{
    // bubble sort is fine for <= 32
    for(uint32_t i = 0; i + 1 < count; i++)
    {
        for(uint32_t j = 0; j + 1 < count - i; j++)
        {
            if(bindings[j].binding > bindings[j + 1].binding)
            {
                VkDescriptorSetLayoutBinding tb = bindings[j];
                bindings[j]                     = bindings[j + 1];
                bindings[j + 1]                 = tb;

                VkDescriptorBindingFlags tf = flags[j];
                flags[j]                    = flags[j + 1];
                flags[j + 1]                = tf;
            }
        }
    }
}

// ------------------------------------------------------------
// hashing + equality
// ------------------------------------------------------------

static uint32_t hash_layout_key(const DescriptorLayoutKey* k)
{
    uint32_t h = 0;
    h ^= hash32_bytes(&k->binding_count, sizeof(k->binding_count));
    h ^= hash32_bytes(&k->create_flags, sizeof(k->create_flags));

    if(k->binding_count)
    {
        h ^= hash32_bytes(k->bindings, k->binding_count * sizeof(VkDescriptorSetLayoutBinding));
        h ^= hash32_bytes(k->binding_flags, k->binding_count * sizeof(VkDescriptorBindingFlags));
    }

    return h;
}

static bool layout_key_equals(const DescriptorLayoutKey* a, const DescriptorLayoutKey* b)
{
    if(a->hash != b->hash)
        return false;
    if(a->binding_count != b->binding_count)
        return false;
    if(a->create_flags != b->create_flags)
        return false;

    if(a->binding_count == 0)
        return true;

    if(memcmp(a->bindings, b->bindings, a->binding_count * sizeof(VkDescriptorSetLayoutBinding)) != 0)
        return false;

    if(memcmp(a->binding_flags, b->binding_flags, a->binding_count * sizeof(VkDescriptorBindingFlags)) != 0)
        return false;

    return true;
}

// ------------------------------------------------------------
// Layout Cache
// ------------------------------------------------------------

void descriptor_layout_cache_init(DescriptorLayoutCache* cache, VkDevice device)
{
    assert(cache);
    *cache = (DescriptorLayoutCache){
        .device  = device,
        .entries = NULL,
    };
}

void descriptor_layout_cache_destroy(DescriptorLayoutCache* cache)
{
    assert(cache);

    for(int i = 0; i < arrlen(cache->entries); i++)
        vkDestroyDescriptorSetLayout(cache->device, cache->entries[i].layout, NULL);

    arrfree(cache->entries);
    *cache = (DescriptorLayoutCache){0};
}

VkDescriptorSetLayout descriptor_layout_cache_get(DescriptorLayoutCache* cache, const VkDescriptorSetLayoutCreateInfo* info)
{
    assert(cache);
    assert(info);
    assert(info->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    assert(info->bindingCount <= VK_DESC_MAX_BINDINGS);

    if(info->bindingCount > 0)
        assert(info->pBindings != NULL);

    DescriptorLayoutKey key = {0};
    key.binding_count       = info->bindingCount;
    key.create_flags        = info->flags;

    if(key.binding_count > 0)
    {
        memcpy(key.bindings, info->pBindings, key.binding_count * sizeof(VkDescriptorSetLayoutBinding));
    }

    // default flags = 0
    for(uint32_t i = 0; i < key.binding_count; i++)
        key.binding_flags[i] = 0;

    const VkDescriptorSetLayoutBindingFlagsCreateInfo* flags_info = find_binding_flags_info(info->pNext);

    if(flags_info)
    {
        assert(flags_info->bindingCount == info->bindingCount);
        assert(flags_info->pBindingFlags != NULL);

        if(key.binding_count > 0)
        {
            memcpy(key.binding_flags, flags_info->pBindingFlags, key.binding_count * sizeof(VkDescriptorBindingFlags));
        }
    }

    // canonical ordering so "same layout, different order" hits cache
    sort_bindings_in_place(key.bindings, key.binding_flags, key.binding_count);

    key.hash = hash_layout_key(&key);

    for(int i = 0; i < arrlen(cache->entries); i++)
    {
        if(layout_key_equals(&cache->entries[i].key, &key))
            return cache->entries[i].layout;
    }

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(cache->device, info, NULL, &layout));

    DescriptorLayoutEntry entry = {
        .key    = key,
        .layout = layout,
    };
    arrpush(cache->entries, entry);

    return layout;
}

VkDescriptorSetLayout get_or_create_set_layout(DescriptorLayoutCache*              cache,
                                               const VkDescriptorSetLayoutBinding* bindings,
                                               uint32_t                            binding_count,
                                               VkDescriptorSetLayoutCreateFlags    create_flags,
                                               const VkDescriptorBindingFlags*     binding_flags)
{
    assert(cache);
    assert(binding_count <= VK_DESC_MAX_BINDINGS);
    if(binding_count > 0)
        assert(bindings != NULL);

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext         = NULL,
        .bindingCount  = binding_count,
        .pBindingFlags = binding_flags,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = (binding_flags != NULL) ? (const void*)&flags_info : NULL,
        .flags        = create_flags,
        .bindingCount = binding_count,
        .pBindings    = bindings,
    };

    return descriptor_layout_cache_get(cache, &ci);
}

// ------------------------------------------------------------
// Descriptor Allocator
// ------------------------------------------------------------

static VkDescriptorPool create_pool(VkDevice device, float scale, bool update_after_bind)
{
    VkDescriptorPoolSize sizes[] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = (uint32_t)(128 * scale)},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = (uint32_t)(128 * scale)},
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = (uint32_t)(64 * scale)},
        // 🔥 IMPORTANT: bindless needs huge counts
        {.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = (uint32_t)((update_after_bind ? MAX_BINDLESS_TEXTURES : 256) * scale)},
        {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = (uint32_t)(256 * scale)},
        {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = (uint32_t)(64 * scale)},

        {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = (uint32_t)(64 * scale)},
    };

    VkDescriptorPoolCreateFlags flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if(update_after_bind)
        flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    VkDescriptorPoolCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = flags,
        .maxSets       = (uint32_t)(256 * scale),
        .poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0])),
        .pPoolSizes    = sizes,
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &info, NULL, &pool));
    return pool;
}

void descriptor_allocator_init(DescriptorAllocator* alloc, VkDevice device, bool update_after_bind)
{
    assert(alloc);
    *alloc = (DescriptorAllocator){
        .device            = device,
        .update_after_bind = update_after_bind,
        .pools             = NULL,
    };
}

void descriptor_allocator_destroy(DescriptorAllocator* alloc)
{
    assert(alloc);

    for(int i = 0; i < arrlen(alloc->pools); i++)
        vkDestroyDescriptorPool(alloc->device, alloc->pools[i].pool, NULL);

    arrfree(alloc->pools);
    *alloc = (DescriptorAllocator){0};
}

void descriptor_allocator_reset(DescriptorAllocator* alloc)
{
    assert(alloc);

    for(int i = 0; i < arrlen(alloc->pools); i++)
        vkResetDescriptorPool(alloc->device, alloc->pools[i].pool, 0);
}

static VkDescriptorPool current_pool(DescriptorAllocator* a)
{
    assert(a);

    if(arrlen(a->pools) == 0)
    {
        DescriptorPoolChunk chunk = {
            .pool  = create_pool(a->device, 1.0f, a->update_after_bind),
            .scale = 1.0f,
        };
        arrpush(a->pools, chunk);
    }

    return a->pools[arrlen(a->pools) - 1].pool;
}

static VkResult allocate_from_pool(DescriptorAllocator* a, VkDescriptorSetLayout layout, const void* pNext, VkDescriptorSet* out_set)
{
    VkDescriptorSetAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = pNext,
        .descriptorPool     = current_pool(a),
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout,
    };

    VkResult r = vkAllocateDescriptorSets(a->device, &info, out_set);
    if(r == VK_SUCCESS)
        return VK_SUCCESS;

    if(r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL)
    {
        float new_scale = a->pools[arrlen(a->pools) - 1].scale * 2.0f;

        DescriptorPoolChunk chunk = {
            .pool  = create_pool(a->device, new_scale, a->update_after_bind),
            .scale = new_scale,
        };
        arrpush(a->pools, chunk);

        info.descriptorPool = chunk.pool;
        return vkAllocateDescriptorSets(a->device, &info, out_set);
    }

    return r;
}

VkResult descriptor_allocator_allocate(DescriptorAllocator* alloc, VkDescriptorSetLayout layout, VkDescriptorSet* out_set)
{
    return allocate_from_pool(alloc, layout, NULL, out_set);
}

VkResult descriptor_allocator_allocate_variable(DescriptorAllocator*  alloc,
                                                VkDescriptorSetLayout layout,
                                                uint32_t              variable_descriptor_count,
                                                VkDescriptorSet*      out_set)
{
    VkDescriptorSetVariableDescriptorCountAllocateInfo count_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .pNext              = NULL,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &variable_descriptor_count,
    };

    return allocate_from_pool(alloc, layout, &count_info, out_set);
}
