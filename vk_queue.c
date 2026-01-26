
#include "vk_queue.h"
#include <stdlib.h>

void find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface, queue_families* out)
{
    // initialize everything
    *out = (queue_families){.graphics_queue = VK_NULL_HANDLE,
                            .present_queue  = VK_NULL_HANDLE,
                            .compute_queue  = VK_NULL_HANDLE,
                            .transfer_queue = VK_NULL_HANDLE,

                            .graphics_family = 0,
                            .present_family  = 0,
                            .compute_family  = 0,
                            .transfer_family = 0,

                            .has_graphics = 0,
                            .has_present  = 0,
                            .has_compute  = 0,
                            .has_transfer = 0};

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if(count == 0)
        return;

    VkQueueFamilyProperties* families = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * count);

    if(!families)
        return;

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);

    for(uint32_t i = 0; i < count; i++)
    {
        const VkQueueFamilyProperties* f = &families[i];

        if(!out->has_graphics && (f->queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            out->graphics_family = i;
            out->has_graphics    = 1;
        }

        if(!out->has_compute && (f->queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            out->compute_family = i;
            out->has_compute    = 1;
        }

        if(!out->has_transfer && (f->queueFlags & VK_QUEUE_TRANSFER_BIT))
        {
            out->transfer_family = i;
            out->has_transfer    = 1;
        }

        if(!out->has_present)
        {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if(presentSupport)
            {
                out->present_family = i;
                out->has_present    = 1;
            }
        }

        if(out->has_graphics && out->has_present && out->has_compute && out->has_transfer)
        {
            break;
        }
    }

    free(families);
}

void init_device_queues(VkDevice device, queue_families* q)
{
    if(q->has_graphics)
        vkGetDeviceQueue(device, q->graphics_family, 0, &q->graphics_queue);

    if(q->has_present)
        vkGetDeviceQueue(device, q->present_family, 0, &q->present_queue);

    if(q->has_compute)
        vkGetDeviceQueue(device, q->compute_family, 0, &q->compute_queue);

    if(q->has_transfer)
        vkGetDeviceQueue(device, q->transfer_family, 0, &q->transfer_queue);
}
