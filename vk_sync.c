
#include "vk_sync.h"
#include <string.h>


/* ============================================================================
 * Fence Helpers
 * ============================================================================ */

void vk_create_fence(VkDevice device, bool signaled, VkFence* out_fence)
{
    VkFenceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
    };

    VK_CHECK(vkCreateFence(device, &info, NULL, out_fence));
}

void vk_create_fences(VkDevice device, uint32_t count, bool signaled, VkFence* out_fences)
{
    for (uint32_t i = 0; i < count; i++)
        vk_create_fence(device, signaled, &out_fences[i]);
}

void vk_wait_fence(VkDevice device, VkFence fence, uint64_t timeout_ns)
{
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, timeout_ns));
}

void vk_wait_fences(VkDevice device, uint32_t count,
                    const VkFence* fences, bool wait_all, uint64_t timeout_ns)
{
    VK_CHECK(vkWaitForFences(device, count, fences,
                             wait_all ? VK_TRUE : VK_FALSE, timeout_ns));
}

void vk_reset_fence(VkDevice device, VkFence fence)
{
    VK_CHECK(vkResetFences(device, 1, &fence));
}

void vk_reset_fences(VkDevice device, uint32_t count, const VkFence* fences)
{
    VK_CHECK(vkResetFences(device, count, fences));
}

bool vk_fence_is_signaled(VkDevice device, VkFence fence)
{
    return vkGetFenceStatus(device, fence) == VK_SUCCESS;
}

void vk_destroy_fences(VkDevice device, uint32_t count, VkFence* fences)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (fences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, fences[i], NULL);
            fences[i] = VK_NULL_HANDLE;
        }
    }
}

/* ============================================================================
 * Semaphore Helpers
 * ============================================================================ */

void vk_create_semaphore(VkDevice device, VkSemaphore* out_semaphore)
{
    VkSemaphoreCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VK_CHECK(vkCreateSemaphore(device, &info, NULL, out_semaphore));
}

void vk_create_semaphores(VkDevice device, uint32_t count, VkSemaphore* out_semaphores)
{
    for (uint32_t i = 0; i < count; i++)
        vk_create_semaphore(device, &out_semaphores[i]);
}

void vk_destroy_semaphores(VkDevice device, uint32_t count, VkSemaphore* semaphores)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (semaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, semaphores[i], NULL);
            semaphores[i] = VK_NULL_HANDLE;
        }
    }
}
