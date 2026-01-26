#include "vk_swapchain.h"
#include "vk_sync.h"
#include "vk_barrier.h"
#include "vk_cmd.h"
#include <vulkan/vulkan_core.h>
VkSurfaceCapabilities2KHR query_surface_capabilities(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    VkPhysicalDeviceSurfaceInfo2KHR info = {
        .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = surface,
    };

    VkSurfaceCapabilities2KHR caps = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
    };

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilities2KHR(gpu, &info, &caps));
    return caps;
}


VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR* caps, uint32_t desired_w, uint32_t desired_h)
{
    if(caps->currentExtent.width != 0xFFFFFFFF)
        return caps->currentExtent;
    VkExtent2D extent = {.width = desired_w, .height = desired_h};
    extent.width      = CLAMP(extent.width, caps->minImageExtent.width, caps->maxImageExtent.width);
    extent.height     = CLAMP(extent.height, caps->minImageExtent.height, caps->maxImageExtent.height);
    return extent;
}


VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice gpu, VkSurfaceKHR surface, VkFormat preferred, VkColorSpaceKHR preferred_cs)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, NULL);

    VkSurfaceFormatKHR formats[32];
    if(count > 32)
        count = 32;

    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats);

    for(uint32_t i = 0; i < count; ++i)
        if(formats[i].format == preferred && formats[i].colorSpace == preferred_cs)
            return formats[i];

    return formats[0];
}

// Choose minImageCount given a user hint, but always respect Vulkan caps.
static uint32_t choose_min_image_count(const VkSurfaceCapabilities2KHR* caps, uint32_t preferred_hint)
{
    const uint32_t min_cap = caps->surfaceCapabilities.minImageCount;

    // Never go below Vulkan's minimum, even if the hint is silly.
    uint32_t preferred = (preferred_hint > min_cap) ? preferred_hint : min_cap;

    // maxImageCount == 0 means "no upper bound"
    const uint32_t raw_max = caps->surfaceCapabilities.maxImageCount;
    const uint32_t max_cap = (raw_max == 0) ? preferred : raw_max;

    // Clamp to [min_cap, max_cap]
    if(preferred < min_cap)
        preferred = min_cap;
    if(preferred > max_cap)
        preferred = max_cap;

    return preferred;
}
void vk_create_swapchain(VkDevice                       device,
                         VkPhysicalDevice               gpu,
                         FlowSwapchain*                 out_swapchain,
                         const FlowSwapchainCreateInfo* info,
                         VkQueue                        graphics_queue,
                         VkCommandPool                  one_time_pool)
{
    VkSurfaceCapabilities2KHR caps = query_surface_capabilities(gpu, info->surface);

    // Query formats and present modes up-front to satisfy validation and pick supported values.
    VkSurfaceFormatKHR surface_format = select_surface_format(gpu, info->surface, info->preferred_format,
                                                              info->preferred_color_space);
    VkPresentModeKHR present_mode = vk_swapchain_select_present_mode(gpu, info->surface, false);
    if(info->preferred_present_mode != VK_PRESENT_MODE_MAX_ENUM_KHR)
        present_mode = info->preferred_present_mode;

    VkExtent2D extent = choose_extent(&caps.surfaceCapabilities, info->width, info->height);

    if(extent.width == 0 || extent.height == 0)
        return;  // minimized, wait later


    VkImageUsageFlags usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | info->extra_usage) & caps.surfaceCapabilities.supportedUsageFlags;
    VkSwapchainCreateInfoKHR ci = {.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                   .surface          = info->surface,
                                   .minImageCount    = choose_min_image_count(&caps, info->min_image_count),
                                   .imageFormat      = surface_format.format,
                                   .imageColorSpace  = surface_format.colorSpace,
                                   .imageExtent      = extent,
                                   .imageArrayLayers = 1,
                                   .imageUsage       = usage,
                                   .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                   .preTransform     = caps.surfaceCapabilities.currentTransform,
                                   .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                   .presentMode      = present_mode,
                                   .clipped          = VK_TRUE,
                                   .oldSwapchain     = info->old_swapchain};

    log_info("[swapchain] create: extent=%ux%u minImageCount=%u format=%u colorSpace=%u presentMode=%u usage=0x%x", 
             extent.width, extent.height, ci.minImageCount, ci.imageFormat, ci.imageColorSpace, ci.presentMode, usage);

    VK_CHECK(vkCreateSwapchainKHR(device, &ci, NULL, &out_swapchain->swapchain));

    out_swapchain->surface       = info->surface;
    out_swapchain->extent        = extent;
    out_swapchain->format        = surface_format.format;
    out_swapchain->color_space   = surface_format.colorSpace;
    out_swapchain->present_mode  = present_mode;
    out_swapchain->current_image = 0;
    out_swapchain->image_usage   = usage;
    // Query swapchain images
    VK_CHECK(vkGetSwapchainImagesKHR(device, out_swapchain->swapchain, &out_swapchain->image_count, NULL));
    log_info("[swapchain] images: %u", out_swapchain->image_count);

    if(out_swapchain->image_count > MAX_SWAPCHAIN_IMAGES)
        out_swapchain->image_count = MAX_SWAPCHAIN_IMAGES;  // don’t blow the stack

    VK_CHECK(vkGetSwapchainImagesKHR(device, out_swapchain->swapchain, &out_swapchain->image_count, out_swapchain->images));

    // Create image views
    forEach(i, out_swapchain->image_count)
    {
        VkImageViewCreateInfo view_ci = VK_IMAGE_VIEW_DEFAULT(out_swapchain->images[i], out_swapchain->format);
        VK_CHECK(vkCreateImageView(device, &view_ci, NULL, &out_swapchain->image_views[i]));
    }

    // Optional: transition all swapchain images UNDEFINED -> PRESENT (cosmetic)
    {
        VkCommandBuffer cmd = begin_one_time_cmd(device, one_time_pool);

        forEach(i, out_swapchain->image_count)
        {
            IMAGE_BARRIER_IMMEDIATE(cmd, out_swapchain->images[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        end_one_time_cmd(device, graphics_queue, one_time_pool, cmd);
    }

    vk_create_semaphores(device, out_swapchain->image_count, out_swapchain->render_finished);
}


void vk_swapchain_destroy(VkDevice device, FlowSwapchain* swapchain)
{
    if(!swapchain)
        return;

    forEach(i, swapchain->image_count)
    {
        if(swapchain->image_views[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, swapchain->image_views[i], NULL);
        }
    }

    vk_destroy_semaphores(device, swapchain->image_count, swapchain->render_finished);
    if(swapchain->swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapchain->swapchain, NULL);
    }

    memset(swapchain, 0, sizeof(*swapchain));
}

bool vk_swapchain_acquire(VkDevice device, FlowSwapchain* sc, VkSemaphore image_available, VkFence fence, uint64_t timeout, bool* needs_recreate)
{
    *needs_recreate = false;
    VkResult r      = vkAcquireNextImageKHR(device, sc->swapchain, timeout, image_available, fence, &sc->current_image);

    if(r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        *needs_recreate = true;
        return false;
    }

    if(r == VK_SUBOPTIMAL_KHR)
    {
        *needs_recreate = true;
        return true;
    }

    VK_CHECK(r);
    return true;
}


bool vk_swapchain_present(VkQueue present_queue, FlowSwapchain* sc, const VkSemaphore* waits, uint32_t wait_count, bool* needs_recreate)
{
    *needs_recreate = false;

    VkPresentInfoKHR info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = wait_count,
        .pWaitSemaphores    = waits,
        .swapchainCount     = 1,
        .pSwapchains        = &sc->swapchain,
        .pImageIndices      = &sc->current_image,
    };

    VkResult r = vkQueuePresentKHR(present_queue, &info);

    if(r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
    {
        *needs_recreate = true;
        return false;
    }

    VK_CHECK(r);
    return true;
}


VkPresentModeKHR vk_swapchain_select_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, bool vsync)
{
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, NULL);

    VkPresentModeKHR modes[16];
    if(mode_count > 16)
        mode_count = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, modes);

    if(vsync)
    {
        /* Prefer FIFO (always available) */
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    /* Prefer mailbox for low-latency without tearing */
    for(uint32_t i = 0; i < mode_count; i++)
    {
        if(modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }

    /* Fall back to immediate */
    for(uint32_t i = 0; i < mode_count; i++)
    {
        if(modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}


void vk_swapchain_recreate(VkDevice device, VkPhysicalDevice gpu, FlowSwapchain* sc, uint32_t new_w, uint32_t new_h, VkQueue graphics_queue, VkCommandPool one_time_pool)


{
    if(new_w == 0 || new_h == 0)
        return;
    vkDeviceWaitIdle(device);


    forEach(i, sc->image_count)
    {
        if(sc->image_views[i])
            vkDestroyImageView(device, sc->image_views[i], NULL);
    }

    vk_destroy_semaphores(device, sc->image_count, sc->render_finished);
    FlowSwapchainCreateInfo info = {0};
    info.surface                 = sc->surface;
    info.width                   = new_w;
    info.height                  = new_h;
    info.min_image_count         = MAX(3u, sc->image_count);
    info.preferred_format        = sc->format;
    info.preferred_color_space   = sc->color_space;
    info.preferred_present_mode  = sc->present_mode;
    info.extra_usage             = sc->image_usage & ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.old_swapchain           = sc->swapchain;

    VkSwapchainKHR old = sc->swapchain;

    vk_create_swapchain(device, gpu, sc, &info, graphics_queue, one_time_pool);

    if(old)
        vkDestroySwapchainKHR(device, old, NULL);
}
