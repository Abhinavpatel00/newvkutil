#ifndef VK_SYNC_H_
#define VK_SYNC_H_


#include "vk_defaults.h"
/* ------------------ Fence helpers ------------------ */

void vk_create_fence(VkDevice device, bool signaled, VkFence* out_fence);
void vk_create_fences(VkDevice device, uint32_t count, bool signaled, VkFence* out_fences);

void vk_wait_fence(VkDevice device, VkFence fence, uint64_t timeout_ns);
void vk_wait_fences(VkDevice device, uint32_t count, const VkFence* fences, bool wait_all, uint64_t timeout_ns);

void vk_reset_fence(VkDevice device, VkFence fence);
void vk_reset_fences(VkDevice device, uint32_t count, const VkFence* fences);

bool vk_fence_is_signaled(VkDevice device, VkFence fence);

void vk_destroy_fences(VkDevice device, uint32_t count, VkFence* fences);

/* ------------------ Semaphore helpers ------------------ */

void vk_create_semaphore(VkDevice device, VkSemaphore* out_semaphore);
void vk_create_semaphores(VkDevice device, uint32_t count, VkSemaphore* out_semaphores);

void vk_destroy_semaphores(VkDevice device, uint32_t count, VkSemaphore* semaphores);

#endif /* VK_SYNC_H_ */
