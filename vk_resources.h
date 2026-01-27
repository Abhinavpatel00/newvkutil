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
    Buffer       buffer;
    OA_Allocator allocator;
    VkDeviceSize alignment;
} BufferArena;
typedef struct GpuMeshBuffers
{
    Buffer vertex;
    Buffer index;

    uint32_t index_count;
    uint32_t vertex_count;
} GpuMeshBuffers;

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
// ImageState answers:
//
// “How is this image currently used?”
//
// ImageResource answers:
//
// “What memory and object back this image?”


#define MAX_IMAGES       1024
#define MAX_IMAGE_VIEWS  8192

typedef struct ImageState
{
    VkImageLayout          layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2        access;
} ImageState;

typedef struct Image
{
    VkImage        image;
    VkExtent3D     extent;
    VkFormat       format;
    uint32_t       mipLevels;
    uint32_t       arrayLayers;
    VmaAllocation  allocation;

    VkImageView    view;
    VkSampler      sampler;

    ImageState     state;
} Image;

static inline void image_state_reset(Image* img)
{
    if(!img)
        return;
    img->state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img->state.stage  = VK_PIPELINE_STAGE_2_NONE;
    img->state.access = 0;
}

static inline VkDescriptorImageInfo image_descriptor(const Image* img)
{
    return (VkDescriptorImageInfo){
        .imageView   = img->view,
        .sampler     = img->sampler,
        .imageLayout = img->state.layout,
    };
}

static inline void image_transition(VkCommandBuffer cmd,
                                    Image*          img,
                                    VkImageLayout   newLayout,
                                    VkPipelineStageFlags2 dstStage,
                                    VkAccessFlags2  dstAccess)
{
    if(!img)
        return;

    if(img->state.layout == newLayout)
        return;

    VkImageMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .oldLayout     = img->state.layout,
        .newLayout     = newLayout,
        .srcStageMask  = img->state.stage,
        .srcAccessMask = img->state.access,
        .dstStageMask  = dstStage,
        .dstAccessMask = dstAccess,
        .image         = img->image,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount     = VK_REMAINING_MIP_LEVELS,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,
        },
    };

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    img->state.layout = newLayout;
    img->state.stage  = dstStage;
    img->state.access = dstAccess;
}

static inline void image_to_color(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

static inline void image_to_sampled(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

static inline void image_to_present(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, 0);
}

static inline void image_to_transfer_dst(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

static inline void image_to_transfer_src(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
}

static inline void image_to_general_compute_rw(VkCommandBuffer cmd, Image* img)
{
    image_transition(cmd, img, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}


typedef struct ImageResource
{
    VkImage        image;
    VkExtent3D     extent;
    VkFormat       format;
    uint16_t       mipLevels;
    uint16_t       arrayLayers;

    VmaAllocation allocation;

    uint16_t       viewBase;   // index into global view pool
    uint16_t       viewCount;  // how many views belong to this image
} ImageResource;

typedef struct ImageViewPool
{
    VkImageView views[MAX_IMAGE_VIEWS];
    uint32_t    count;
} ImageViewPool;
// Views are just handles
//
// Handles are cheap
//
// Indirection via index is faster than pointer chasing
//
// Lifetime is explicit
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

void res_create_image(ResourceAllocator*       ra,
                      const VkImageCreateInfo* image_info,
                      VmaMemoryUsage           memory_usage,
                      VmaAllocationCreateFlags flags,
                      VkImage*                 out_image,
                      VmaAllocation*           out_alloc);

void res_destroy_image(ResourceAllocator* ra, VkImage image, VmaAllocation allocation);

void        buffer_arena_init(ResourceAllocator*       ra,
                              VkDeviceSize             size,
                              VkBufferUsageFlags2KHR   usageflags,
                              VmaMemoryUsage           memory_usage,
                              VmaAllocationCreateFlags flags,
                              VkDeviceSize             alignment,
                              BufferArena*             out_arena);
void        buffer_arena_destroy(ResourceAllocator* ra, BufferArena* arena);
BufferSlice buffer_arena_alloc(BufferArena* arena, VkDeviceSize size, VkDeviceSize alignment);
void        buffer_arena_free(BufferArena* arena, BufferSlice* slice);


// NOTE: This is a simple version: it waits for the queue to finish (vkQueueWaitIdle).
// Good for startup uploads. For per-frame streaming, you'll want a ring-buffer staging system.

void upload_to_gpu_buffer(ResourceAllocator* allocator,
                          VkQueue            queue,
                          VkCommandPool      pool,
                          VkBuffer           dst_buffer,
                          VkDeviceSize       dst_offset,
                          const void*        src_data,
                          VkDeviceSize       size);
