/*
 * Frequency-Based Descriptor System Implementation
 * See vk_descriptor_freq.h for design documentation
 */

#include "vk_descriptor_freq.h"
#include "stb/stb_ds.h"

/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 */

static VkDescriptorPool create_freq_pool(VkDevice device)
{
    // Pool sizes for frequency-based allocation
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FREQ_MAX_MATERIALS * 5 + 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32},
    };

    VkDescriptorPoolCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = FREQ_MAX_FRAMES_IN_FLIGHT + FREQ_MAX_MATERIALS + 16,
        .poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0])),
        .pPoolSizes    = sizes,
    };

    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &info, NULL, &pool));
    return pool;
}

static void create_set0_layout(VkDevice device, FreqSet0Layout* out)
{
    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: Global UBO
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
        },
        // Binding 1: Light UBO
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
        // Binding 2: Shadow map (optional)
        {
            .binding         = 2,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 3: Environment map (optional)
        {
            .binding         = 3,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)(sizeof(bindings) / sizeof(bindings[0])),
        .pBindings    = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, NULL, &out->layout));
}

static void create_set1_layout(VkDevice device, FreqSet1Layout* out)
{
    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: Material params UBO
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 1: Albedo texture
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 2: Normal texture
        {
            .binding         = 2,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 3: Metallic-roughness texture
        {
            .binding         = 3,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 4: Occlusion texture
        {
            .binding         = 4,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        // Binding 5: Emissive texture
        {
            .binding         = 5,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)(sizeof(bindings) / sizeof(bindings[0])),
        .pBindings    = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, NULL, &out->layout));
}

static void create_set2_layout(VkDevice device, FreqSet2Layout* out)
{
    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: Dynamic UBO for per-draw data
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)(sizeof(bindings) / sizeof(bindings[0])),
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

static void create_frame_resources(FreqDescriptorSystem* sys, uint32_t frame_idx)
{
    FreqFrameResources* frame = &sys->frames[frame_idx];

    // Global UBO
    res_create_buffer(sys->allocator,
                      
                      sizeof(FreqGlobalData),
                      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      FREQ_MIN_UBO_ALIGNMENT,
                      &frame->global_buffer);

    // Light UBO
    res_create_buffer(sys->allocator,
                      
                      sizeof(FreqLightData),
                      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      FREQ_MIN_UBO_ALIGNMENT,
                      &frame->light_buffer);

    // Per-draw dynamic UBO
    size_t draw_buffer_size = FREQ_MAX_DRAWS_PER_FRAME * FREQ_MIN_UBO_ALIGNMENT;  // Aligned to 256 bytes each
    res_create_buffer(sys->allocator,
                
                      draw_buffer_size,
                      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      FREQ_MIN_UBO_ALIGNMENT,
                      &frame->draw_buffer);

    // Allocate Set 0 for this frame
    frame->set0 = allocate_set(sys->device, sys->pool, sys->set0_layout.layout);

    // Allocate Set 2 for this frame
    frame->set2 = allocate_set(sys->device, sys->pool, sys->set2_layout.layout);

    // Write Set 0 descriptors
    VkDescriptorBufferInfo global_info = {
        .buffer = frame->global_buffer.buffer,
        .offset = 0,
        .range  = sizeof(FreqGlobalData),
    };

    VkDescriptorBufferInfo light_info = {
        .buffer = frame->light_buffer.buffer,
        .offset = 0,
        .range  = sizeof(FreqLightData),
    };

    VkWriteDescriptorSet writes[] = {
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set0,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &global_info,
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = frame->set0,
            .dstBinding      = 1,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &light_info,
        },
    };

    vkUpdateDescriptorSets(sys->device, 2, writes, 0, NULL);

    // Write Set 2 descriptor (dynamic UBO covers entire buffer)
    VkDescriptorBufferInfo draw_info = {
        .buffer = frame->draw_buffer.buffer,
        .offset = 0,
        .range  = FREQ_MIN_UBO_ALIGNMENT,  // Size of one draw data (dynamic offset handles the rest)
    };

    VkWriteDescriptorSet draw_write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = frame->set2,
        .dstBinding      = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pBufferInfo     = &draw_info,
    };

    vkUpdateDescriptorSets(sys->device, 1, &draw_write, 0, NULL);

    frame->draw_count        = 0;
    frame->draw_buffer_offset = 0;
}

static void destroy_frame_resources(FreqDescriptorSystem* sys, FreqFrameResources* frame)
{
    res_destroy_buffer(sys->allocator, &frame->global_buffer);
    res_destroy_buffer(sys->allocator, &frame->light_buffer);
    res_destroy_buffer(sys->allocator, &frame->draw_buffer);
}


/* =============================================================================
 * PUBLIC API - INITIALIZATION
 * =============================================================================
 */

void freq_init(FreqDescriptorSystem* sys, VkDevice device, ResourceAllocator* allocator)
{
    memset(sys, 0, sizeof(*sys));
    sys->device    = device;
    sys->allocator = allocator;

    // Create descriptor pool
    sys->pool = create_freq_pool(device);

    // Create layouts
    create_set0_layout(device, &sys->set0_layout);
    create_set1_layout(device, &sys->set1_layout);
    create_set2_layout(device, &sys->set2_layout);

    // Initialize per-frame resources
    for (uint32_t i = 0; i < FREQ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        create_frame_resources(sys, i);
    }

    sys->current_frame = 0;
    sys->materials     = NULL;  // stb_ds array
}

void freq_destroy(FreqDescriptorSystem* sys)
{
    // Destroy materials
    for (int i = 0; i < arrlen(sys->materials); i++)
    {
        res_destroy_buffer(sys->allocator, &sys->materials[i].param_buffer);
    }
    arrfree(sys->materials);

    // Destroy per-frame resources
    for (uint32_t i = 0; i < FREQ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        destroy_frame_resources(sys, &sys->frames[i]);
    }

    // Destroy default resources
    if (sys->default_sampler != VK_NULL_HANDLE)
        vkDestroySampler(sys->device, sys->default_sampler, NULL);

    // Destroy layouts
    vkDestroyDescriptorSetLayout(sys->device, sys->set0_layout.layout, NULL);
    vkDestroyDescriptorSetLayout(sys->device, sys->set1_layout.layout, NULL);
    vkDestroyDescriptorSetLayout(sys->device, sys->set2_layout.layout, NULL);

    // Destroy pool
    vkDestroyDescriptorPool(sys->device, sys->pool, NULL);
}


void freq_create_defaults(FreqDescriptorSystem* sys, VkCommandBuffer cmd)
{
    // Create default sampler
    VkSamplerCreateInfo sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias   = 0.0f,
        .maxAnisotropy = 16.0f,
        .anisotropyEnable = VK_TRUE,
        .compareEnable = VK_FALSE,
        .minLod       = 0.0f,
        .maxLod       = VK_LOD_CLAMP_NONE,
    };

    VK_CHECK(vkCreateSampler(sys->device, &sampler_info, NULL, &sys->default_sampler));

    // Note: Actual default texture creation would require Image creation
    // which depends on your staging buffer system. This is a placeholder.
    // In practice, create 1x1 white, 1x1 normal (0.5, 0.5, 1.0), 1x1 black textures
    // and store their VkImageViews in sys->default_white, etc.
    (void)cmd;
}


/* =============================================================================
 * PUBLIC API - PER-FRAME
 * =============================================================================
 */

void freq_begin_frame(FreqDescriptorSystem* sys)
{
    sys->current_frame = (sys->current_frame + 1) % FREQ_MAX_FRAMES_IN_FLIGHT;
    
    FreqFrameResources* frame = &sys->frames[sys->current_frame];
    frame->draw_count         = 0;
    frame->draw_buffer_offset = 0;
}

void freq_update_global(FreqDescriptorSystem* sys, const FreqGlobalData* global, const FreqLightData* lights)
{
    FreqFrameResources* frame = &sys->frames[sys->current_frame];

    memcpy(frame->global_buffer.mapping, global, sizeof(FreqGlobalData));
    memcpy(frame->light_buffer.mapping, lights, sizeof(FreqLightData));
}

VkDescriptorSet freq_get_set0(FreqDescriptorSystem* sys)
{
    return sys->frames[sys->current_frame].set0;
}


/* =============================================================================
 * PUBLIC API - MATERIALS
 * =============================================================================
 */

uint32_t freq_material_create(FreqDescriptorSystem* sys, const FreqMaterialParams* params)
{
    FreqMaterial mat = {0};
    mat.params = *params;
    mat.material_id = (uint32_t)arrlen(sys->materials);
    mat.dirty = true;

    // Create parameter buffer
    res_create_buffer(sys->allocator,
                      
                      sizeof(FreqMaterialParams),
                      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      256,
                      &mat.param_buffer);

    // Allocate descriptor set
    mat.set = allocate_set(sys->device, sys->pool, sys->set1_layout.layout);

    // Initialize descriptor with default sampler info
    VkDescriptorImageInfo default_image = {
        .sampler     = sys->default_sampler,
        .imageView   = sys->default_white,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    mat.albedo            = default_image;
    mat.normal            = default_image;
    mat.metallic_roughness = default_image;
    mat.occlusion         = default_image;
    mat.emissive          = default_image;

    arrpush(sys->materials, mat);

    return mat.material_id;
}

void freq_material_set_textures(FreqDescriptorSystem* sys, uint32_t material_id,
                                  VkImageView albedo, VkImageView normal,
                                  VkImageView metallic_roughness, VkImageView occlusion,
                                  VkImageView emissive, VkSampler sampler)
{
    if (material_id >= (uint32_t)arrlen(sys->materials)) return;
    
    FreqMaterial* mat = &sys->materials[material_id];
    VkSampler s = sampler ? sampler : sys->default_sampler;

    if (albedo)
    {
        mat->albedo.sampler   = s;
        mat->albedo.imageView = albedo;
        mat->albedo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (normal)
    {
        mat->normal.sampler   = s;
        mat->normal.imageView = normal;
        mat->normal.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (metallic_roughness)
    {
        mat->metallic_roughness.sampler   = s;
        mat->metallic_roughness.imageView = metallic_roughness;
        mat->metallic_roughness.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (occlusion)
    {
        mat->occlusion.sampler   = s;
        mat->occlusion.imageView = occlusion;
        mat->occlusion.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (emissive)
    {
        mat->emissive.sampler   = s;
        mat->emissive.imageView = emissive;
        mat->emissive.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    mat->dirty = true;
}

void freq_material_set_params(FreqDescriptorSystem* sys, uint32_t material_id, const FreqMaterialParams* params)
{
    if (material_id >= (uint32_t)arrlen(sys->materials)) return;
    
    sys->materials[material_id].params = *params;
    sys->materials[material_id].dirty = true;
}

void freq_material_flush(FreqDescriptorSystem* sys)
{
    for (int i = 0; i < arrlen(sys->materials); i++)
    {
        FreqMaterial* mat = &sys->materials[i];
        if (!mat->dirty) continue;

        // Upload params to GPU
        memcpy(mat->param_buffer.mapping, &mat->params, sizeof(FreqMaterialParams));

        // Update descriptor set
        VkDescriptorBufferInfo buffer_info = {
            .buffer = mat->param_buffer.buffer,
            .offset = 0,
            .range  = sizeof(FreqMaterialParams),
        };

        VkWriteDescriptorSet writes[] = {
            // Binding 0: Material params UBO
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &buffer_info,
            },
            // Binding 1: Albedo
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 1,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &mat->albedo,
            },
            // Binding 2: Normal
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 2,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &mat->normal,
            },
            // Binding 3: Metallic-roughness
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 3,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &mat->metallic_roughness,
            },
            // Binding 4: Occlusion
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 4,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &mat->occlusion,
            },
            // Binding 5: Emissive
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->set,
                .dstBinding      = 5,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &mat->emissive,
            },
        };

        vkUpdateDescriptorSets(sys->device, 6, writes, 0, NULL);
        mat->dirty = false;
    }
}

VkDescriptorSet freq_material_get_set(FreqDescriptorSystem* sys, uint32_t material_id)
{
    if (material_id >= (uint32_t)arrlen(sys->materials)) return VK_NULL_HANDLE;
    return sys->materials[material_id].set;
}

void freq_material_destroy(FreqDescriptorSystem* sys, uint32_t material_id)
{
    if (material_id >= (uint32_t)arrlen(sys->materials)) return;
    
    FreqMaterial* mat = &sys->materials[material_id];
    res_destroy_buffer(sys->allocator, &mat->param_buffer);
    
    // Mark as invalid (don't actually remove to keep indices stable)
    mat->set = VK_NULL_HANDLE;
}


/* =============================================================================
 * PUBLIC API - PER-DRAW
 * =============================================================================
 */

uint32_t freq_alloc_draw(FreqDescriptorSystem* sys, FreqDrawData** out_data)
{
    FreqFrameResources* frame = &sys->frames[sys->current_frame];

    if (frame->draw_count >= FREQ_MAX_DRAWS_PER_FRAME)
    {
        // Overflow - return last slot
        *out_data = (FreqDrawData*)(frame->draw_buffer.mapping + 
                                     (FREQ_MAX_DRAWS_PER_FRAME - 1) * FREQ_MIN_UBO_ALIGNMENT);
        return (FREQ_MAX_DRAWS_PER_FRAME - 1) * FREQ_MIN_UBO_ALIGNMENT;
    }

    uint32_t offset = frame->draw_count * FREQ_MIN_UBO_ALIGNMENT;
    *out_data = (FreqDrawData*)(frame->draw_buffer.mapping + offset);
    
    frame->draw_count++;

    return offset;
}

VkDescriptorSet freq_get_set2(FreqDescriptorSystem* sys)
{
    return sys->frames[sys->current_frame].set2;
}

void freq_get_layouts(FreqDescriptorSystem* sys, VkDescriptorSetLayout* out_layouts)
{
    out_layouts[0] = sys->set0_layout.layout;
    out_layouts[1] = sys->set1_layout.layout;
    out_layouts[2] = sys->set2_layout.layout;
}

static VkPipelineLayout s_freq_pipeline_layout = VK_NULL_HANDLE;

VkPipelineLayout freq_get_pipeline_layout(FreqDescriptorSystem* sys, VkPushConstantRange* push_ranges, uint32_t push_range_count)
{
    if (s_freq_pipeline_layout != VK_NULL_HANDLE)
        return s_freq_pipeline_layout;

    VkDescriptorSetLayout layouts[3];
    freq_get_layouts(sys, layouts);

    VkPipelineLayoutCreateInfo info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 3,
        .pSetLayouts            = layouts,
        .pushConstantRangeCount = push_range_count,
        .pPushConstantRanges    = push_ranges,
    };

    VK_CHECK(vkCreatePipelineLayout(sys->device, &info, NULL, &s_freq_pipeline_layout));
    return s_freq_pipeline_layout;
}


/* =============================================================================
 * PUBLIC API - RENDERING HELPERS
 * =============================================================================
 */

void freq_bind_for_draw(FreqDescriptorSystem* sys,
                        VkCommandBuffer cmd,
                        VkPipelineBindPoint bind_point,
                        VkPipelineLayout layout,
                        uint32_t material_id,
                        uint32_t draw_offset)
{
    VkDescriptorSet sets[3] = {
        freq_get_set0(sys),
        freq_material_get_set(sys, material_id),
        freq_get_set2(sys),
    };

    uint32_t dynamic_offsets[] = {draw_offset};

    vkCmdBindDescriptorSets(cmd, bind_point, layout, 0, 3, sets, 1, dynamic_offsets);
}

void freq_bind_global(FreqDescriptorSystem* sys,
                      VkCommandBuffer cmd,
                      VkPipelineBindPoint bind_point,
                      VkPipelineLayout layout)
{
    VkDescriptorSet set0 = freq_get_set0(sys);
    vkCmdBindDescriptorSets(cmd, bind_point, layout, 0, 1, &set0, 0, NULL);
}

void freq_bind_material_draw(FreqDescriptorSystem* sys,
                             VkCommandBuffer cmd,
                             VkPipelineBindPoint bind_point,
                             VkPipelineLayout layout,
                             uint32_t material_id,
                             uint32_t draw_offset)
{
    VkDescriptorSet sets[2] = {
        freq_material_get_set(sys, material_id),
        freq_get_set2(sys),
    };

    uint32_t dynamic_offsets[] = {draw_offset};

    // Bind sets 1 and 2 only (set 0 already bound)
    vkCmdBindDescriptorSets(cmd, bind_point, layout, 1, 2, sets, 1, dynamic_offsets);
}
