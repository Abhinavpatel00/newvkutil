#ifndef VK_SWAPCHAIN_H_
#define VK_SWAPCHAIN_H_

#include "vk_defaults.h"
#include "vk_sync.h"
#include <vulkan/vulkan_core.h>

#define MAX_SWAPCHAIN_IMAGES 8


typedef struct ALIGNAS(64) FlowSwapchain
{
    VkSwapchainKHR   swapchain;
    VkSurfaceKHR     surface;
    VkFormat         format;
    VkColorSpaceKHR  color_space;
    VkPresentModeKHR present_mode;
    VkExtent2D       extent;

    uint32_t    image_count;
    VkImage     images[MAX_SWAPCHAIN_IMAGES];
    VkImageView image_views[MAX_SWAPCHAIN_IMAGES];

    VkSemaphore render_finished[MAX_SWAPCHAIN_IMAGES];

    VkImageUsageFlags image_usage;

    uint32_t current_image;
    bool     vsync;

} FlowSwapchain;

typedef struct FlowSwapchainCreateInfo
{
    VkSurfaceKHR      surface;
    uint32_t          width;
    uint32_t          height;
    uint32_t          min_image_count;
    VkPresentModeKHR  preferred_present_mode;
    VkFormat          preferred_format;
    VkColorSpaceKHR   preferred_color_space; /* VK_COLOR_SPACE_SRGB_NONLINEAR_KHR default */
    VkImageUsageFlags extra_usage;           /* Additional usage flags */
    VkSwapchainKHR    old_swapchain;         /* For recreation */
} FlowSwapchainCreateInfo;


void vk_create_swapchain(VkDevice                       device,
                         VkPhysicalDevice               gpu,
                         FlowSwapchain*                 out_swapchain,
                         const FlowSwapchainCreateInfo* info,
                         VkQueue                        graphics_queue,
                         VkCommandPool                  one_time_pool);
void vk_swapchain_destroy(VkDevice device, FlowSwapchain* swapchain);
bool vk_swapchain_acquire(VkDevice device, FlowSwapchain* sc, VkSemaphore image_available, VkFence fence, uint64_t timeout, bool* needs_recreate);

bool vk_swapchain_present(VkQueue present_queue, FlowSwapchain* sc, const VkSemaphore* waits, uint32_t wait_count, bool* needs_recreate);

void vk_swapchain_recreate(VkDevice device, VkPhysicalDevice gpu, FlowSwapchain* sc, uint32_t new_w, uint32_t new_h, VkQueue graphics_queue, VkCommandPool one_time_pool);
VkPresentModeKHR vk_swapchain_select_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, bool vsync);

#endif /* VK_SWAPCHAIN_H_ */
