#include "debugtext.h"
#include "vk_defaults.h"
#include <string.h>
static void text_pack_ascii(TextDataPC* pc, const char* s)
{
    memset(pc->data, 0, sizeof(pc->data));
    for(int i = 0; i < DEBUG_TEXT_MAX_CHARS; i++)
    {
        if(!s[i])
            break;
        int word = i / 4;
        int byte = i % 4;
        pc->data[word] |= ((uint32_t)(uint8_t)s[i]) << (byte * 8);
    }
}

uint32_t pack_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static void debug_text_create_layout(VkDebugText* dt, VkDevice device)
{
    VkDescriptorSetLayoutBinding img_binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &img_binding,
    };

    vkCreateDescriptorSetLayout(device, &dslci, NULL, &dt->set_layout);
}

static void debug_text_create_pipeline(VkDebugText*           dt,
                                       VkDevice               device,
                                       DescriptorLayoutCache* desc_cache,
                                       PipelineLayoutCache*   pipe_cache,
                                       const char*            comp_spv_path)
{
    dt->pipeline = create_compute_pipeline(device, VK_NULL_HANDLE, desc_cache, pipe_cache, comp_spv_path, &dt->layout);
}

static void debug_text_build_sets(VkDebugText* dt, DescriptorAllocator* persistent_desc, DescriptorLayoutCache* layout_cache, const FlowSwapchain* swap)
{
    if(dt->sets)
    {
        free(dt->sets);
        dt->sets = NULL;
    }

    dt->set_count = swap->image_count;
    dt->sets      = (VkDescriptorSet*)calloc(dt->set_count, sizeof(VkDescriptorSet));

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
    };

    for(uint32_t i = 0; i < dt->set_count; i++)
    {
        DescriptorWriter w;
        desc_writer_begin(&w);

        desc_writer_write_image(&w, VK_NULL_HANDLE, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, swap->image_views[i],
                                VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

        // descriptor_build_set creates layout via cache and allocates the set
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult        r   = descriptor_build_set(persistent_desc, layout_cache, &layout_info, &w, &set);
        if(r != VK_SUCCESS)
        {
            // if you want, replace with your own logging/assert
            // but don't silently ignore it
            abort();
        }
        dt->sets[i] = set;
    }
}

void vk_debug_text_init(VkDebugText*           dt,
                        VkDevice               device,
                        DescriptorAllocator*   persistent_desc,
                        DescriptorLayoutCache* desc_cache,
                        PipelineLayoutCache*   pipe_cache,
                        const FlowSwapchain*   swap,
                        const char*            comp_spv_path)
{
    memset(dt, 0, sizeof(*dt));
    dt->device = device;

    debug_text_create_layout(dt, device);
    debug_text_create_pipeline(dt, device, desc_cache, pipe_cache, comp_spv_path);
    debug_text_build_sets(dt, persistent_desc, desc_cache, swap);
}

void vk_debug_text_destroy(VkDebugText* dt)
{
    if(!dt || dt->device == VK_NULL_HANDLE)
        return;

    if(dt->sets)
    {
        free(dt->sets);
        dt->sets = NULL;
    }

    if(dt->pipeline)
        vkDestroyPipeline(dt->device, dt->pipeline, NULL);
    if(dt->set_layout)
        vkDestroyDescriptorSetLayout(dt->device, dt->set_layout, NULL);

    memset(dt, 0, sizeof(*dt));
}

void vk_debug_text_on_swapchain_recreated(VkDebugText*           dt,
                                          DescriptorAllocator*   persistent_desc,
                                          DescriptorLayoutCache* layout_cache,
                                          const FlowSwapchain*   swap)
{
    // rebuild descriptor sets because image views changed
    debug_text_build_sets(dt, persistent_desc, layout_cache, swap);
}

void vk_debug_text_begin_frame(VkDebugText* dt)
{
    dt->queued_count = 0;
}

void vk_debug_text_printf(VkDebugText* dt, int x, int y, int scale, uint32_t rgba, const char* fmt, ...)
{
    if(dt->queued_count >= 256)
        return;

    char buf[DEBUG_TEXT_MAX_CHARS + 1];
    memset(buf, 0, sizeof(buf));

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    TextDataPC pc = {0};
    pc.offset[0]  = x;
    pc.offset[1]  = y;
    pc.scale      = scale;
    pc.color      = rgba;
    text_pack_ascii(&pc, buf);

    uint32_t len = (uint32_t)c99_strnlen(buf, DEBUG_TEXT_MAX_CHARS);

    dt->queued[dt->queued_count].pc  = pc;
    dt->queued[dt->queued_count].len = len;
    dt->queued_count++;
}

void vk_debug_text_flush(VkDebugText* dt, VkCommandBuffer cmd, VkImage target_image, uint32_t swapchain_image_index)
{
    if(dt->queued_count == 0)
        return;

    // You MUST transition to GENERAL because shader uses storage image writes.
    // Your barrier macro might be wrong depending on stage/access masks,
    // but we'll use your existing macro to match your codebase.
    IMAGE_BARRIER_IMMEDIATE(cmd, target_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dt->pipeline);

    VkDescriptorSet set = dt->sets[swapchain_image_index];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dt->layout, 0, 1, &set, 0, NULL);

    for(uint32_t i = 0; i < dt->queued_count; i++)
    {
        const TextDataPC* pc       = &dt->queued[i].pc;
        uint32_t          groups_x = dt->queued[i].len;
        if(groups_x == 0)
            continue;

        vkCmdPushConstants(cmd, dt->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TextDataPC), pc);
        vkCmdDispatch(cmd, groups_x, 1, 1);
    }

    // Back to COLOR_ATTACHMENT so the caller can transition to PRESENT
    IMAGE_BARRIER_IMMEDIATE(cmd, target_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}
