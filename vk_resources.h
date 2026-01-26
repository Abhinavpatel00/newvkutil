#pragma once
#include "tinytypes.h"
#include "vk_defaults.h"
#include "offset_allocator.h"
#include <vulkan/vulkan_core.h>

// buffer is a region of memory used to store vertex data, index data, uniform data, and other types of data.
typedef struct Buffer
{
    VkBuffer        buffer;  // vulkan buffer
    VkDeviceSize    buffer_size;
    VkDeviceAddress address;     // addr of the buffer in the shader
    uint8_t*        mapping;     //this is a CPU pointer directly into GPU-visible memory.
    VmaAllocation   allocation;  // Memory associated with the buffer
} Buffer;

typedef struct BufferSlice
{
    VkBuffer        buffer;
    VkDeviceSize    offset;
    VkDeviceSize    size;
    VkDeviceAddress address;
    uint8_t*        mapping;
    OA_Allocation   allocation;
} BufferSlice;

typedef struct BufferArena
{
    Buffer      buffer;
    OA_Allocator allocator;
    VkDeviceSize alignment;
} BufferArena;


// the buffer exists logically as one big thing.
//
// physically, it is backed by many allocations
//
//so
//
// struct largeBuffer
// {
// 	           *VmaAllocation // many alloc
// };
//
//

typedef struct Image
{
    VkImage               image;
    VkExtent3D            extent;
    uint32_t              mipLevels;
    uint32_t              arrayLayers;
    VkFormat              format;
    VmaAllocation         allocation;
    VkDescriptorImageInfo descriptor;
} Image;


typedef struct ResourceAllocator
{
    VkDevice         device;
    VkPhysicalDevice physical_device;
    VmaAllocator     allocator;

    uint64_t     leak_id;
    uint64_t     allocation_counter;
    VkDeviceSize max_alloc_size;

    VmaPool      small_buffer_pools[VK_MAX_MEMORY_TYPES];
    VkDeviceSize small_buffer_threshold;
    VkDeviceSize small_buffer_pool_block_size;

    VmaPool      small_image_pools[VK_MAX_MEMORY_TYPES];
    VkDeviceSize small_image_pool_block_size;

} ResourceAllocator;


void res_init(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device, ResourceAllocator* ra, VmaAllocatorCreateInfo info);
void res_deinit(ResourceAllocator* ra);
void vk_create_buffer(ResourceAllocator*             ra,
                      const VkBufferCreateInfo*      bufferInfo,
                      const VmaAllocationCreateInfo* allocInfo,
                      VkDeviceSize                   minalignment,
                      Buffer*                        outbuffer);


void res_create_buffer(ResourceAllocator* ra,

                       VkDeviceSize             size,
                       VkBufferUsageFlags2KHR   usageflags,
                       VmaMemoryUsage           memory_usage,
                       VmaAllocationCreateFlags flags,
                       VkDeviceSize             min_alignment,
                       Buffer*                  out);


void res_destroy_buffer(ResourceAllocator* ra, Buffer* buf);

void res_create_image(ResourceAllocator* ra,
                      const VkImageCreateInfo* image_info,
                      VmaMemoryUsage            memory_usage,
                      VmaAllocationCreateFlags  flags,
                      VkImage*                  out_image,
                      VmaAllocation*            out_alloc);

void res_destroy_image(ResourceAllocator* ra, VkImage image, VmaAllocation allocation);

void buffer_arena_init(ResourceAllocator* ra,
                       VkDeviceSize             size,
                       VkBufferUsageFlags2KHR   usageflags,
                       VmaMemoryUsage           memory_usage,
                       VmaAllocationCreateFlags flags,
                       VkDeviceSize             alignment,
                       BufferArena*             out_arena);
void buffer_arena_destroy(ResourceAllocator* ra, BufferArena* arena);
BufferSlice buffer_arena_alloc(BufferArena* arena, VkDeviceSize size, VkDeviceSize alignment);
void buffer_arena_free(BufferArena* arena, BufferSlice* slice);


// NOTE: This is a simple version: it waits for the queue to finish (vkQueueWaitIdle).
// Good for startup uploads. For per-frame streaming, you'll want a ring-buffer staging system.

void upload_to_gpu_buffer(ResourceAllocator* allocator,
                          VkQueue            queue,
                          VkCommandPool      pool,
                          VkBuffer           dst_buffer,
                          VkDeviceSize       dst_offset,
                          const void*        src_data,
                          VkDeviceSize       size);
