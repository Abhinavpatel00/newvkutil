#include "vk_defaults.h"

#ifndef VK_CMD_H
#define VK_CMD_H

#include <vulkan/vulkan.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* single pool */
void vk_cmd_create_pool(VkDevice device, uint32_t queue_family_index, bool transient, bool resettable, VkCommandPool* out_pool);

void vk_cmd_destroy_pool(VkDevice device, VkCommandPool pool);

/* multiple pools (frames-in-flight style) */
void vk_cmd_create_many_pools(VkDevice device, uint32_t queue_family_index, bool transient, bool resettable, uint32_t count, VkCommandPool* out_pools);

void vk_cmd_destroy_many_pools(VkDevice device, uint32_t count, VkCommandPool* pools);

/* allocation */
void vk_cmd_alloc(VkDevice device, VkCommandPool pool, bool primary, VkCommandBuffer* out_cmd);


/* recording */
void vk_cmd_begin(VkCommandBuffer cmd, bool one_time);
void vk_cmd_end(VkCommandBuffer cmd);

/* submit and wait */
void vk_cmd_submit_once(VkDevice device, VkQueue queue, VkCommandBuffer cmd);

/* resets */
void vk_cmd_reset(VkCommandBuffer cmd);
void vk_cmd_reset_pool(VkDevice device, VkCommandPool pool);

VkCommandBuffer begin_one_time_cmd(VkDevice device, VkCommandPool pool);


void end_one_time_cmd(VkDevice device, VkQueue queue, VkCommandPool pool, VkCommandBuffer cmd);
#ifdef __cplusplus
}
#endif

#endif /* VK_CMD_H */
