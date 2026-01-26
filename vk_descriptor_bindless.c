/*
 * Bindless Descriptor System Implementation
 * See vk_descriptor_bindless.h for design documentation
 */

#include "vk_descriptor_bindless.h"
#include "stb/stb_ds.h"

/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 */

static VkDescriptorPool create_bindless_pool(VkDevice device)
{
    /*
     * This pool uses UPDATE_AFTER_BIND flag for the bindless resources.
     * This allows us to update descriptors even after they're bound.
     */
    VkDescriptorPoolSize sizes[] = {
        // Bindless textures
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BINDLESS_MAX_TEXTURES},
        // Bindless storage images
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BINDLESS_MAX_STORAGE_IMAGES},
        // Bindless samplers
        {VK_DESCRIPTOR_TYPE_SAMPLER, BINDLESS_MAX_SAMPLERS},
        // Bindless storage buffers
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BINDLESS_MAX_BUFFERS},
    };

    VkDescriptorPoolCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets       = 4,  // Only need a few sets total
        .poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0])),
        .pPoolSizes    = sizes,
    };

    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &info, NULL, &pool));
    return pool;
}

static VkDescriptorPool create_frame_pool(VkDevice device)
{
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BINDLESS_MAX_FRAMES_IN_FLIGHT * 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BINDLESS_MAX_FRAMES_IN_FLIGHT * 4},
    };

    VkDescriptorPoolCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = 0,
        .maxSets       = BINDLESS_MAX_FRAMES_IN_FLIGHT * 2,
        .poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0])),
        .pPoolSizes    = sizes,
    };

    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &info, NULL, &pool));
    return pool;
}

static void create_set0_layout(VkDevice device, BindlessSet0Layout* out)
{
    /*
     * Set 0 contains all bindless resources with special flags:
     * - PARTIALLY_BOUND: Not all descriptors need to be valid
     * - UPDATE_AFTER_BIND: Can update descriptors after binding
     * - VARIABLE_DESCRIPTOR_COUNT: Can have fewer descriptors than max
     */

    VkDescriptorBindingFlags binding_flags[4] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,  // textures

        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,  // storage images

        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,  // samplers

        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,  // storage buffers
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount  = 4,
        .pBindingFlags = binding_flags,
    };

    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: Sampled images (textures)
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = BINDLESS_MAX_TEXTURES,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        },
        // Binding 1: Storage images
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = BINDLESS_MAX_STORAGE_IMAGES,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        },
        // Binding 2: Samplers
        {
            .binding         = 2,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = BINDLESS_MAX_SAMPLERS,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        },
        // Binding 3: Storage buffers
        {
            .binding         = 3,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = BINDLESS_MAX_BUFFERS,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        },
    };

    VkDescriptorSetLayoutCreateInfo info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = &binding_flags_info,
        .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 4,
        .pBindings    = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, NULL, &out->layout));
}

static void create_set1_layout(VkDevice device, BindlessSet1Layout* out)
{
    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: Global UBO
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        },
        // Binding 1: Draw data SSBO
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 2: Material SSBO
        {
            .binding         = 2,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 3: Transform SSBO
        {
            .binding         = 3,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings    = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, NULL, &out->layout));
}

static VkDescriptorSet allocate_set(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout,
    };

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &info, &set));
    return set;
}

static void create_frame_resources(BindlessDescriptorSystem* sys, uint32_t frame_idx)
{
    BindlessFrameResources* frame = &sys->frames[frame_idx];

    // Global UBO
    res_create_buffer(sys->allocator, sizeof(BindlessGlobalData), VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 256, &frame->global_buffer);

    // Draw data SSBO
    size_t draw_size = BINDLESS_MAX_DRAWS_PER_FRAME * sizeof(BindlessDrawData);
    res_create_buffer(sys->allocator, draw_size, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &frame->draw_data_buffer);

    frame->draw_buffer_capacity = BINDLESS_MAX_DRAWS_PER_FRAME;

    // Indirect command buffer
    size_t indirect_size = BINDLESS_MAX_DRAWS_PER_FRAME * sizeof(BindlessIndirectCommand);
    res_create_buffer(sys->allocator, indirect_size, VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &frame->indirect_buffer);

    // Draw count buffer (for vkCmdDrawIndirectCount)
    res_create_buffer(sys->allocator, sizeof(uint32_t), VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 4, &frame->draw_count_buffer);

    // Allocate Set 1
    frame->set1 = allocate_set(sys->device, sys->frame_pool, sys->set1_layout.layout);

    // Update Set 1 descriptors (global + draw data bound, material + transform from main buffers)
    VkDescriptorBufferInfo buffer_infos[4] = {
        {
            .buffer = frame->global_buffer.buffer,
            .offset = 0,
            .range  = sizeof(BindlessGlobalData),
        },
        {
            .buffer = frame->draw_data_buffer.buffer,
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
        {
            .buffer = sys->material_buffer.buffer,
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
        {
            .buffer = sys->transform_buffer.buffer,
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
    };

    VkWriteDescriptorSet writes[4] = {
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set1,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &buffer_infos[0],
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set1,
            .dstBinding      = 1,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_infos[1],
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set1,
            .dstBinding      = 2,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_infos[2],
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set1,
            .dstBinding      = 3,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_infos[3],
        },
    };

    vkUpdateDescriptorSets(sys->device, 4, writes, 0, NULL);

    frame->draw_count = 0;
}

static void destroy_frame_resources(BindlessDescriptorSystem* sys, BindlessFrameResources* frame)
{
    res_destroy_buffer(sys->allocator, &frame->global_buffer);
    res_destroy_buffer(sys->allocator, &frame->draw_data_buffer);
    res_destroy_buffer(sys->allocator, &frame->indirect_buffer);
    res_destroy_buffer(sys->allocator, &frame->draw_count_buffer);
}


/* =============================================================================
 * PUBLIC API - FEATURE DETECTION
 * =============================================================================
 */

bool bindless_check_support(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
    };

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &indexing_features,
    };

    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &bda_features,
    };

    vkGetPhysicalDeviceFeatures2(physical_device, &features);

    bool has_indexing = indexing_features.descriptorBindingPartiallyBound && indexing_features.descriptorBindingSampledImageUpdateAfterBind
                        && indexing_features.descriptorBindingStorageBufferUpdateAfterBind && indexing_features.runtimeDescriptorArray
                        && indexing_features.shaderSampledImageArrayNonUniformIndexing;

    bool has_bda = bda_features.bufferDeviceAddress;

    return has_indexing && has_bda;
}


/* =============================================================================
 * PUBLIC API - INITIALIZATION
 * =============================================================================
 */

void bindless_init(BindlessDescriptorSystem* sys, VkDevice device, VkPhysicalDevice physical_device, ResourceAllocator* allocator)
{
    memset(sys, 0, sizeof(*sys));
    sys->device    = device;
    sys->allocator = allocator;

    // Check feature support
    sys->supports_descriptor_indexing   = true;  // Assume checked before calling
    sys->supports_buffer_device_address = true;
    sys->supports_draw_indirect_count   = true;

    // Create pools
    sys->bindless_pool = create_bindless_pool(device);
    sys->frame_pool    = create_frame_pool(device);

    // Create layouts
    create_set0_layout(device, &sys->set0_layout);
    create_set1_layout(device, &sys->set1_layout);

    // Allocate the ONE bindless set (Set 0)
    sys->set0 = allocate_set(device, sys->bindless_pool, sys->set0_layout.layout);

    // Create material storage buffer
    size_t material_buffer_size = 1024 * sizeof(BindlessMaterial);  // Start with 1024 materials
    res_create_buffer(allocator,  material_buffer_size,
                      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &sys->material_buffer);

    // Create transform storage buffer
    size_t transform_buffer_size = 16384 * sizeof(BindlessTransform);  // Start with 16K transforms
    res_create_buffer(allocator, transform_buffer_size,
                      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &sys->transform_buffer);

    // Create vertex storage buffer (for manual vertex fetching)
    size_t vertex_buffer_size = 64 * 1024 * 1024;  // 64MB vertex buffer
    res_create_buffer(allocator,  vertex_buffer_size,
                      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR
                          | VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,  // Consider GPU_ONLY + staging for production
                      VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &sys->vertex_buffer);

    sys->vertex_buffer_address  = sys->vertex_buffer.address;
    sys->vertex_buffer_capacity = vertex_buffer_size;
    sys->vertex_buffer_offset   = 0;

    // Create index buffer
    size_t index_buffer_size = 32 * 1024 * 1024;  // 32MB index buffer
    res_create_buffer(allocator,  index_buffer_size,
                      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR
                          | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT, 16, &sys->index_buffer);

    sys->index_buffer_address  = sys->index_buffer.address;
    sys->index_buffer_capacity = index_buffer_size;
    sys->index_buffer_offset   = 0;

    // Initialize per-frame resources
    for(uint32_t i = 0; i < BINDLESS_MAX_FRAMES_IN_FLIGHT; i++)
    {
        create_frame_resources(sys, i);
    }

    sys->current_frame = 0;

    // Initialize registries
    sys->next_texture_idx       = 0;
    sys->next_storage_image_idx = 0;
    sys->next_sampler_idx       = 0;
    sys->next_buffer_idx        = 0;

    sys->materials        = NULL;
    sys->transforms       = NULL;
    sys->material_count   = 0;
    sys->transform_count  = 0;
    sys->materials_dirty  = false;
    sys->transforms_dirty = false;

    (void)physical_device;
}

void bindless_destroy(BindlessDescriptorSystem* sys)
{
    // Destroy frame resources
    for(uint32_t i = 0; i < BINDLESS_MAX_FRAMES_IN_FLIGHT; i++)
    {
        destroy_frame_resources(sys, &sys->frames[i]);
    }

    // Destroy storage buffers
    res_destroy_buffer(sys->allocator, &sys->material_buffer);
    res_destroy_buffer(sys->allocator, &sys->transform_buffer);
    res_destroy_buffer(sys->allocator, &sys->vertex_buffer);
    res_destroy_buffer(sys->allocator, &sys->index_buffer);

    // Free CPU arrays
    arrfree(sys->materials);
    arrfree(sys->transforms);

    // Destroy layouts
    vkDestroyDescriptorSetLayout(sys->device, sys->set0_layout.layout, NULL);
    vkDestroyDescriptorSetLayout(sys->device, sys->set1_layout.layout, NULL);

    // Destroy pools
    vkDestroyDescriptorPool(sys->device, sys->bindless_pool, NULL);
    vkDestroyDescriptorPool(sys->device, sys->frame_pool, NULL);

    // Destroy pipeline layout if created
    if(sys->pipeline_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(sys->device, sys->pipeline_layout, NULL);
    }
}

void bindless_create_defaults(BindlessDescriptorSystem* sys, VkCommandBuffer cmd)
{
    // Create default samplers
    VkSamplerCreateInfo linear_info = {
        .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter        = VK_FILTER_LINEAR,
        .minFilter        = VK_FILTER_LINEAR,
        .mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy    = 16.0f,
        .anisotropyEnable = VK_TRUE,
        .minLod           = 0.0f,
        .maxLod           = VK_LOD_CLAMP_NONE,
    };

    VkSampler linear_sampler;
    VK_CHECK(vkCreateSampler(sys->device, &linear_info, NULL, &linear_sampler));
    sys->default_sampler_linear = bindless_register_sampler(sys, linear_sampler);

    VkSamplerCreateInfo nearest_info = linear_info;
    nearest_info.magFilter           = VK_FILTER_NEAREST;
    nearest_info.minFilter           = VK_FILTER_NEAREST;
    nearest_info.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    nearest_info.anisotropyEnable    = VK_FALSE;

    VkSampler nearest_sampler;
    VK_CHECK(vkCreateSampler(sys->device, &nearest_info, NULL, &nearest_sampler));
    sys->default_sampler_nearest = bindless_register_sampler(sys, nearest_sampler);

    // Note: Default textures would need to be created using your image creation system
    // and then registered via bindless_register_texture
    (void)cmd;
}


/* =============================================================================
 * PUBLIC API - RESOURCE REGISTRATION
 * =============================================================================
 */

BindlessTextureHandle bindless_register_texture(BindlessDescriptorSystem* sys, VkImageView view, VkImageLayout layout, VkFormat format)
{
    if(sys->next_texture_idx >= BINDLESS_MAX_TEXTURES)
    {
        // Out of texture slots
        return (BindlessTextureHandle){BINDLESS_INVALID_INDEX, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED};
    }

    uint32_t idx = sys->next_texture_idx++;

    VkDescriptorImageInfo image_info = {
        .sampler     = VK_NULL_HANDLE,  // Using separate samplers
        .imageView   = view,
        .imageLayout = layout,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = sys->set0,
        .dstBinding      = 0,  // Texture array binding
        .dstArrayElement = idx,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo      = &image_info,
    };

    vkUpdateDescriptorSets(sys->device, 1, &write, 0, NULL);

    return (BindlessTextureHandle){idx, view, format};
}

void bindless_unregister_texture(BindlessDescriptorSystem* sys, BindlessTextureHandle handle)
{
    // In a production system, you'd track free indices for reuse
    // For now, just leave a hole (PARTIALLY_BOUND allows this)
    (void)sys;
    (void)handle;
}

BindlessTextureHandle bindless_register_storage_image(BindlessDescriptorSystem* sys, VkImageView view, VkFormat format)
{
    if(sys->next_storage_image_idx >= BINDLESS_MAX_STORAGE_IMAGES)
    {
        return (BindlessTextureHandle){BINDLESS_INVALID_INDEX, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED};
    }

    uint32_t idx = sys->next_storage_image_idx++;

    VkDescriptorImageInfo image_info = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = sys->set0,
        .dstBinding      = 1,  // Storage image array binding
        .dstArrayElement = idx,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &image_info,
    };

    vkUpdateDescriptorSets(sys->device, 1, &write, 0, NULL);

    return (BindlessTextureHandle){idx, view, format};
}

BindlessSamplerHandle bindless_register_sampler(BindlessDescriptorSystem* sys, VkSampler sampler)
{
    if(sys->next_sampler_idx >= BINDLESS_MAX_SAMPLERS)
    {
        return (BindlessSamplerHandle){BINDLESS_INVALID_INDEX, VK_NULL_HANDLE};
    }

    uint32_t idx = sys->next_sampler_idx++;

    VkDescriptorImageInfo sampler_info = {
        .sampler = sampler,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = sys->set0,
        .dstBinding      = 2,  // Sampler array binding
        .dstArrayElement = idx,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo      = &sampler_info,
    };

    vkUpdateDescriptorSets(sys->device, 1, &write, 0, NULL);

    return (BindlessSamplerHandle){idx, sampler};
}

BindlessBufferHandle bindless_register_buffer(BindlessDescriptorSystem* sys, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
    if(sys->next_buffer_idx >= BINDLESS_MAX_BUFFERS)
    {
        return (BindlessBufferHandle){BINDLESS_INVALID_INDEX, VK_NULL_HANDLE, 0, 0};
    }

    uint32_t idx = sys->next_buffer_idx++;

    VkDescriptorBufferInfo buffer_info = {
        .buffer = buffer,
        .offset = offset,
        .range  = range == 0 ? VK_WHOLE_SIZE : range,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = sys->set0,
        .dstBinding      = 3,  // Storage buffer array binding
        .dstArrayElement = idx,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo     = &buffer_info,
    };

    vkUpdateDescriptorSets(sys->device, 1, &write, 0, NULL);

    // Get buffer device address
    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    VkDeviceAddress address = vkGetBufferDeviceAddress(sys->device, &addr_info);

    return (BindlessBufferHandle){idx, buffer, address, range};
}


/* =============================================================================
 * PUBLIC API - MATERIALS
 * =============================================================================
 */

uint32_t bindless_material_create(BindlessDescriptorSystem* sys, const BindlessMaterial* material)
{
    uint32_t idx = sys->material_count++;

    // Ensure CPU array is large enough
    while(arrlen(sys->materials) <= (int)idx)
    {
        BindlessMaterial empty = {0};
        arrpush(sys->materials, empty);
    }

    sys->materials[idx]  = *material;
    sys->materials_dirty = true;

    return idx;
}

void bindless_material_update(BindlessDescriptorSystem* sys, uint32_t idx, const BindlessMaterial* material)
{
    if(idx >= sys->material_count)
        return;

    sys->materials[idx]  = *material;
    sys->materials_dirty = true;
}

BindlessMaterial* bindless_material_get(BindlessDescriptorSystem* sys, uint32_t idx)
{
    if(idx >= sys->material_count)
        return NULL;

    sys->materials_dirty = true;
    return &sys->materials[idx];
}


/* =============================================================================
 * PUBLIC API - TRANSFORMS
 * =============================================================================
 */

uint32_t bindless_transform_alloc(BindlessDescriptorSystem* sys)
{
    uint32_t idx = sys->transform_count++;

    while(arrlen(sys->transforms) <= (int)idx)
    {
        BindlessTransform empty = {0};
        arrpush(sys->transforms, empty);
    }

    sys->transforms_dirty = true;
    return idx;
}

void bindless_transform_update(BindlessDescriptorSystem* sys, uint32_t idx, const BindlessTransform* transform)
{
    if(idx >= sys->transform_count)
        return;

    sys->transforms[idx]  = *transform;
    sys->transforms_dirty = true;
}

BindlessTransform* bindless_transform_get(BindlessDescriptorSystem* sys, uint32_t idx)
{
    if(idx >= sys->transform_count)
        return NULL;

    sys->transforms_dirty = true;
    return &sys->transforms[idx];
}


/* =============================================================================
 * PUBLIC API - MESH DATA
 * =============================================================================
 */

uint32_t bindless_upload_vertices(BindlessDescriptorSystem* sys, const void* vertices, uint32_t vertex_count, uint32_t vertex_stride)
{
    size_t size = vertex_count * vertex_stride;

    // Check capacity
    if(sys->vertex_buffer_offset + size > sys->vertex_buffer_capacity)
    {
        // Buffer full - in production, resize or use multiple buffers
        return BINDLESS_INVALID_INDEX;
    }

    uint32_t offset = (uint32_t)sys->vertex_buffer_offset;
    memcpy(sys->vertex_buffer.mapping + offset, vertices, size);
    sys->vertex_buffer_offset += size;

    // Align to 16 bytes for next upload
    sys->vertex_buffer_offset = (sys->vertex_buffer_offset + 15) & ~15;

    return offset;
}

uint32_t bindless_upload_indices(BindlessDescriptorSystem* sys, const void* indices, uint32_t index_count, VkIndexType index_type)
{
    size_t element_size = (index_type == VK_INDEX_TYPE_UINT32) ? 4 : 2;
    size_t size         = index_count * element_size;

    if(sys->index_buffer_offset + size > sys->index_buffer_capacity)
    {
        return BINDLESS_INVALID_INDEX;
    }

    uint32_t offset = (uint32_t)sys->index_buffer_offset;
    memcpy(sys->index_buffer.mapping + offset, indices, size);
    sys->index_buffer_offset += size;

    // Align to 4 bytes
    sys->index_buffer_offset = (sys->index_buffer_offset + 3) & ~3;

    return offset;
}


/* =============================================================================
 * PUBLIC API - PER-FRAME OPERATIONS
 * =============================================================================
 */

void bindless_begin_frame(BindlessDescriptorSystem* sys)
{
    sys->current_frame = (sys->current_frame + 1) % BINDLESS_MAX_FRAMES_IN_FLIGHT;

    BindlessFrameResources* frame = &sys->frames[sys->current_frame];
    frame->draw_count             = 0;
}

void bindless_update_global(BindlessDescriptorSystem* sys, const BindlessGlobalData* global)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];
    memcpy(frame->global_buffer.mapping, global, sizeof(BindlessGlobalData));
}

void bindless_flush_resources(BindlessDescriptorSystem* sys, VkCommandBuffer cmd)
{
    (void)cmd;  // No GPU commands needed for persistently mapped buffers

    if(sys->materials_dirty && sys->material_count > 0)
    {
        size_t size = sys->material_count * sizeof(BindlessMaterial);
        memcpy(sys->material_buffer.mapping, sys->materials, size);
        sys->materials_dirty = false;
    }

    if(sys->transforms_dirty && sys->transform_count > 0)
    {
        size_t size = sys->transform_count * sizeof(BindlessTransform);
        memcpy(sys->transform_buffer.mapping, sys->transforms, size);
        sys->transforms_dirty = false;
    }
}

uint32_t bindless_alloc_draw(BindlessDescriptorSystem* sys, BindlessDrawData** out_data)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];

    if(frame->draw_count >= frame->draw_buffer_capacity)
    {
        *out_data = NULL;
        return BINDLESS_INVALID_INDEX;
    }

    uint32_t idx = frame->draw_count++;
    *out_data    = (BindlessDrawData*)(frame->draw_data_buffer.mapping) + idx;

    return idx;
}

uint32_t bindless_alloc_indirect(BindlessDescriptorSystem* sys, BindlessIndirectCommand** out_cmd)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];

    // draw_count was already incremented in bindless_alloc_draw
    uint32_t idx = frame->draw_count - 1;
    *out_cmd     = (BindlessIndirectCommand*)(frame->indirect_buffer.mapping) + idx;

    return idx;
}


/* =============================================================================
 * PUBLIC API - RENDERING
 * =============================================================================
 */

void bindless_bind(BindlessDescriptorSystem* sys, VkCommandBuffer cmd, VkPipelineBindPoint bind_point, VkPipelineLayout layout)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];

    VkDescriptorSet sets[2] = {sys->set0, frame->set1};

    vkCmdBindDescriptorSets(cmd, bind_point, layout, 0, 2, sets, 0, NULL);
}

VkDeviceAddress bindless_get_vertex_buffer_address(BindlessDescriptorSystem* sys)
{
    return sys->vertex_buffer_address;
}

VkDeviceAddress bindless_get_index_buffer_address(BindlessDescriptorSystem* sys)
{
    return sys->index_buffer_address;
}

void bindless_draw_indirect(BindlessDescriptorSystem* sys, VkCommandBuffer cmd)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];

    if(frame->draw_count == 0)
        return;

    // Write draw count to buffer
    *(uint32_t*)frame->draw_count_buffer.mapping = frame->draw_count;

    vkCmdDrawIndexedIndirect(cmd, frame->indirect_buffer.buffer, 0, frame->draw_count, sizeof(BindlessIndirectCommand));
}

void bindless_draw_indirect_count(BindlessDescriptorSystem* sys, VkCommandBuffer cmd, uint32_t max_draws)
{
    BindlessFrameResources* frame = &sys->frames[sys->current_frame];

    if(frame->draw_count == 0)
        return;

    // Write draw count to buffer
    *(uint32_t*)frame->draw_count_buffer.mapping = frame->draw_count;

    vkCmdDrawIndexedIndirectCount(cmd, frame->indirect_buffer.buffer, 0, frame->draw_count_buffer.buffer, 0,
                                  max_draws > 0 ? max_draws : BINDLESS_MAX_DRAWS_PER_FRAME, sizeof(BindlessIndirectCommand));
}

void bindless_get_layouts(BindlessDescriptorSystem* sys, VkDescriptorSetLayout* out_layouts)
{
    out_layouts[0] = sys->set0_layout.layout;
    out_layouts[1] = sys->set1_layout.layout;
}

VkPipelineLayout bindless_get_pipeline_layout(BindlessDescriptorSystem* sys)
{
    if(sys->pipeline_layout != VK_NULL_HANDLE)
        return sys->pipeline_layout;

    VkDescriptorSetLayout layouts[2];
    bindless_get_layouts(sys, layouts);

    // Push constants for buffer device addresses
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(BindlessPushConstants),
    };

    VkPipelineLayoutCreateInfo info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 2,
        .pSetLayouts            = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_range,
    };

    VK_CHECK(vkCreatePipelineLayout(sys->device, &info, NULL, &sys->pipeline_layout));
    return sys->pipeline_layout;
}
