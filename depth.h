#include "vk_defaults.h"
#include "vk_resources.h"
typedef struct DepthTarget
{
    VkImage       image[MAX_FRAME_IN_FLIGHT];
    VkImageView   view[MAX_FRAME_IN_FLIGHT];
    VmaAllocation alloc[MAX_FRAME_IN_FLIGHT];
    VkImageLayout layout[MAX_FRAME_IN_FLIGHT];

    VkFormat format;
    uint32_t width;
    uint32_t height;
} DepthTarget;

static void create_depth_target(ResourceAllocator* allocator, DepthTarget* depth, uint32_t width, uint32_t height, VkFormat format)
{
    memset(depth, 0, sizeof(*depth));

    depth->format = format;
    depth->width  = width;
    depth->height = height;

    VkImageCreateInfo imgInfo =
        VK_IMAGE_DEFAULT_2D(width, height, format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    VmaAllocationCreateInfo allocInfo = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        depth->layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;

        res_create_image(allocator, &imgInfo, allocInfo.usage, allocInfo.flags, &depth->image[i], &depth->alloc[i]);

        VkImageViewCreateInfo viewInfo       = VK_IMAGE_VIEW_DEFAULT(depth->image[i], format);
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        VK_CHECK(vkCreateImageView(allocator->device, &viewInfo, NULL, &depth->view[i]));
    }
}

static void destroy_depth_target(ResourceAllocator* allocator, DepthTarget* depth)
{
    for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        if(depth->view[i])
            vkDestroyImageView(allocator->device, depth->view[i], NULL);

        if(depth->image[i])
            res_destroy_image(allocator, depth->image[i], depth->alloc[i]);

        depth->view[i]   = VK_NULL_HANDLE;
        depth->image[i]  = VK_NULL_HANDLE;
        depth->alloc[i]  = NULL;
        depth->layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    depth->format = VK_FORMAT_UNDEFINED;
    depth->width  = 0;
    depth->height = 0;
}

static VkFormat pick_depth_format(VkPhysicalDevice gpu)
{
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };

    for(uint32_t i = 0; i < ARRAY_COUNT(candidates); i++)
    {
        VkFormat           fmt = candidates[i];
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(gpu, fmt, &props);

        if(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }

    return VK_FORMAT_UNDEFINED;
}
