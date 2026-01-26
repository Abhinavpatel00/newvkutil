#include "bindlesstextures.h"

#include "vk_barrier.h"
#include "vk_cmd.h"

#include <string.h>

static uint32_t calc_mip_count(uint32_t w, uint32_t h)
{
    uint32_t levels = 1;
    while(w > 1 || h > 1)
    {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        levels++;
    }
    return levels;
}
void bindless_textures_init(BindlessTextures* bt, VkDevice device, DescriptorAllocator* alloc, DescriptorLayoutCache* cache, uint32_t max_textures)
{
    memset(bt, 0, sizeof(*bt));

    bt->max_textures = max_textures;
    bt->next_free    = 1;  // slot 0 reserved for dummy

    VkDescriptorSetLayoutBinding binding = {
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = max_textures,
        .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = NULL,
    };

    VkDescriptorBindingFlags flags[1] = {VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
                                         | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};

    bt->layout = get_or_create_set_layout(cache, &binding, 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, flags);

    // allocate descriptor set with variable descriptor count
    VK_CHECK(descriptor_allocator_allocate_variable(alloc, bt->layout, max_textures, &bt->set));
}

void bindless_textures_destroy(BindlessTextures* bt, ResourceAllocator* allocator, VkDevice device)
{
    if(!bt)
        return;

    for(uint32_t i = 0; i < bt->max_textures; i++)
    {
        if(bt->textures[i].image)
            bindless_textures_destroy_texture(allocator, device, &bt->textures[i]);
    }

    *bt = (BindlessTextures){0};
}

void bindless_textures_write(BindlessTextures* bt, VkDevice device, uint32_t slot, VkImageView view, VkSampler sampler, VkImageLayout layout)
{
    VkDescriptorImageInfo img = {
        .sampler     = sampler,
        .imageView   = view,
        .imageLayout = layout,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = bt->set,
        .dstBinding      = 0,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &img,
    };

    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

uint32_t bindless_textures_alloc_slot(BindlessTextures* bt)
{
    if(bt->free_count > 0)
        return bt->free_list[--bt->free_count];

    if(bt->next_free >= bt->max_textures)
        return 0;

    return bt->next_free++;
}

static void cmd_generate_mips(VkCommandBuffer cmd, VkImage image, uint32_t w, uint32_t h, uint32_t mipCount)
{
    uint32_t mipW = w;
    uint32_t mipH = h;

    for(uint32_t i = 1; i < mipCount; i++)
    {
        // transition level i-1: DST -> SRC
        VkImageMemoryBarrier2 b1 = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image         = image,
            .subresourceRange =
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = i - 1,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
        };

        VkDependencyInfo dep1 = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &b1,
        };
        vkCmdPipelineBarrier2(cmd, &dep1);

        VkImageBlit blit = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1},
            .srcOffsets     = {{0, 0, 0}, {(int32_t)mipW, (int32_t)mipH, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1},
            .dstOffsets = {{0, 0, 0}, {(int32_t)((mipW > 1) ? (mipW >> 1) : 1), (int32_t)((mipH > 1) ? (mipH >> 1) : 1), 1}}};

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &blit, VK_FILTER_LINEAR);

        // transition level i-1: SRC -> SHADER_READ
        VkImageMemoryBarrier2 b2 = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image         = image,
            .subresourceRange =
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = i - 1,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
        };

        VkDependencyInfo dep2 = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &b2,
        };
        vkCmdPipelineBarrier2(cmd, &dep2);

        mipW = (mipW > 1) ? (mipW >> 1) : 1;
        mipH = (mipH > 1) ? (mipH >> 1) : 1;
    }

    // last mip: DST -> SHADER_READ
    VkImageMemoryBarrier2 last = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image         = image,
        .subresourceRange =
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = mipCount - 1,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };

    VkDependencyInfo depLast = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &last,
    };
    vkCmdPipelineBarrier2(cmd, &depLast);
}
bool bindless_textures_create_rgba8(ResourceAllocator* allocator,
                                    VkDevice           device,
                                    VkQueue            queue,
                                    VkCommandPool      pool,
                                    uint32_t           w,
                                    uint32_t           h,
                                    const uint8_t*     pixels,
                                    TextureResource*   out_tex)
{
    VkDeviceSize      size     = (VkDeviceSize)w * (VkDeviceSize)h * 4u;
    uint32_t          mipCount = calc_mip_count(w, h);
    VkImageCreateInfo img_info = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = VK_FORMAT_R8G8B8A8_UNORM,
        .extent      = {w, h, 1},
        .mipLevels   = mipCount,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |  // REQUIRED for blit mipgen
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    res_create_image(allocator, &img_info, alloc_info.usage, alloc_info.flags, &out_tex->image, &out_tex->allocation);

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out_tex->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange =
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = mipCount,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &out_tex->view));


    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(allocator->physical_device, &props);  // you need physical device stored somewhere

    float maxAniso = props.limits.maxSamplerAnisotropy;
    if(maxAniso > 16.0f)
        maxAniso = 16.0f;

    VkSamplerCreateInfo sampler_info = {
        .sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter  = VK_FILTER_LINEAR,
        .minFilter  = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,

        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,

        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy    = maxAniso,

        .minLod     = 0.0f,
        .maxLod     = VK_LOD_CLAMP_NONE,  // important
        .mipLodBias = 0.0f,

        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &out_tex->sampler));
    Buffer staging = {0};
    res_create_buffer(allocator, size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &staging);

    memcpy(staging.mapping, pixels, (size_t)size);

    VkCommandBuffer cmd = begin_one_time_cmd(device, pool);
    IMAGE_BARRIER_IMMEDIATE(cmd, out_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {w, h, 1},
    };

    vkCmdCopyBufferToImage(cmd, staging.buffer, out_tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    cmd_generate_mips(cmd, out_tex->image, w, h, mipCount);
    end_one_time_cmd(device, queue, pool, cmd);

    res_destroy_buffer(allocator, &staging);

    out_tex->width  = w;
    out_tex->height = h;
    out_tex->format = VK_FORMAT_R8G8B8A8_UNORM;
    return true;
}

void bindless_textures_destroy_texture(ResourceAllocator* allocator, VkDevice device, TextureResource* tex)
{
    if(tex->sampler)
        vkDestroySampler(device, tex->sampler, NULL);
    if(tex->view)
        vkDestroyImageView(device, tex->view, NULL);
    if(tex->image)
        vmaDestroyImage(allocator->allocator, tex->image, tex->allocation);

    *tex = (TextureResource){0};
}
