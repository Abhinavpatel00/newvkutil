
#ifndef VK_QUEUE_H_
#define VK_QUEUE_H_

#include "vk_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct queue_families
{
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue compute_queue;
    VkQueue transfer_queue;

    uint32_t graphics_family;
    uint32_t present_family;
    uint32_t compute_family;
    uint32_t transfer_family;

    int has_graphics;
    int has_present;
    int has_compute;
    int has_transfer;
} queue_families;
//  pick GPU → choose queue families → create VkDevice → get queues
// Fills `out` with available queue families.
// Must be called BEFORE logical device creation.
void find_queue_families(VkPhysicalDevice device,
                         VkSurfaceKHR surface,
                         queue_families* out);
// Call AFTER vkCreateDevice.
// Uses the family indices already stored in queue_families.
void init_device_queues(VkDevice device, queue_families* q);

//
//
// queue_families q;
//
// find_queue_families(physical, surface, &q);
//
// create_device(physical, &q, ...);
//
// init_device_queues(device, &q);
//
#ifdef __cplusplus
}
#endif

#endif /* VK_QUEUE_H_ */
