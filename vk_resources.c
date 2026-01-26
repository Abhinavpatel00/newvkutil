#include "vk_resources.h"
#include "external/logger-c/logger/logger.h"
#include "vk_cmd.h"

static VkDeviceSize res_align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    if(alignment == 0)
        return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

static VmaPool res_get_small_buffer_pool(ResourceAllocator*                ra,
                                         const VkBufferCreateInfo*         buffer_info,
                                         const VmaAllocationCreateInfo*    alloc_info)
{
    if(!ra)
        return VK_NULL_HANDLE;

    uint32_t memory_type_index = 0;
    if(vmaFindMemoryTypeIndexForBufferInfo(ra->allocator, buffer_info, alloc_info, &memory_type_index) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    if(ra->small_buffer_pools[memory_type_index] == VK_NULL_HANDLE)
    {
        VmaPoolCreateInfo pool_info = {
            .memoryTypeIndex = memory_type_index,
            .blockSize       = ra->small_buffer_pool_block_size,
            .minBlockCount   = 1,
            .maxBlockCount   = 0,
            .flags           = 0,
        };
        VK_CHECK(vmaCreatePool(ra->allocator, &pool_info, &ra->small_buffer_pools[memory_type_index]));
    }

    return ra->small_buffer_pools[memory_type_index];
}

static VmaPool res_get_small_image_pool(ResourceAllocator*                ra,
                                        const VkImageCreateInfo*          image_info,
                                        const VmaAllocationCreateInfo*    alloc_info)
{
    if(!ra)
        return VK_NULL_HANDLE;

    uint32_t memory_type_index = 0;
    if(vmaFindMemoryTypeIndexForImageInfo(ra->allocator, image_info, alloc_info, &memory_type_index) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    if(ra->small_image_pools[memory_type_index] == VK_NULL_HANDLE)
    {
        VmaPoolCreateInfo pool_info = {
            .memoryTypeIndex = memory_type_index,
            .blockSize       = ra->small_image_pool_block_size,
            .minBlockCount   = 1,
            .maxBlockCount   = 0,
            .flags           = 0,
        };
        VK_CHECK(vmaCreatePool(ra->allocator, &pool_info, &ra->small_image_pools[memory_type_index]));
    }

    return ra->small_image_pools[memory_type_index];
}
void res_init(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device, ResourceAllocator* ra, VmaAllocatorCreateInfo info)
{

    ra->device = device;
    info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    info.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
    info.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
    ra->physical_device = physical_device;

    VkPhysicalDeviceVulkan11Properties props11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};

    VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &props11};

    vkGetPhysicalDeviceProperties2(info.physicalDevice, &props);
    ra->max_alloc_size = props11.maxMemoryAllocationSize;

    for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
    {
        ra->small_buffer_pools[i] = VK_NULL_HANDLE;
        ra->small_image_pools[i] = VK_NULL_HANDLE;
    }
    ra->small_buffer_threshold = 1024 * 1024; // 1MB
    ra->small_buffer_pool_block_size = 256 * 1024 * 1024; // 256MB
    ra->small_image_pool_block_size = 256 * 1024 * 1024; // 256MB
    //  use VMA_DYNAMIC_VULKAN_FUNCTIONS
    VmaVulkanFunctions vulkanFunctions = {
        .vkGetInstanceProcAddr                   = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr                     = vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties           = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties     = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory                        = vkAllocateMemory,
        .vkFreeMemory                            = vkFreeMemory,
        .vkMapMemory                             = vkMapMemory,
        .vkUnmapMemory                           = vkUnmapMemory,
        .vkFlushMappedMemoryRanges               = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges          = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory                      = vkBindBufferMemory,
        .vkBindImageMemory                       = vkBindImageMemory,
        .vkGetBufferMemoryRequirements           = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements            = vkGetImageMemoryRequirements,
        .vkCreateBuffer                          = vkCreateBuffer,
        .vkDestroyBuffer                         = vkDestroyBuffer,
        .vkCreateImage                           = vkCreateImage,
        .vkDestroyImage                          = vkDestroyImage,
        .vkCmdCopyBuffer                         = vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR       = vkGetBufferMemoryRequirements2,
        .vkGetImageMemoryRequirements2KHR        = vkGetImageMemoryRequirements2,
        .vkBindBufferMemory2KHR                  = vkBindBufferMemory2,
        .vkBindImageMemory2KHR                   = vkBindImageMemory2,
        .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
        .vkGetDeviceBufferMemoryRequirements     = vkGetDeviceBufferMemoryRequirements,
        .vkGetDeviceImageMemoryRequirements      = vkGetDeviceImageMemoryRequirements,
    };
    info.pVulkanFunctions = &vulkanFunctions;

    VK_CHECK(vmaCreateAllocator(&info, &ra->allocator));
}
void res_deinit(ResourceAllocator* ra)
{

    if(!ra->allocator)
        return;

    {
        char* stats = NULL;
        vmaBuildStatsString(ra->allocator, &stats, VK_TRUE);
        if(stats)
        {
            log_info("[vma] stats:\n%s", stats);
            vmaFreeStatsString(ra->allocator, stats);
        }

        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        memset(budgets, 0, sizeof(budgets));
        vmaGetHeapBudgets(ra->allocator, budgets);

        VkPhysicalDeviceMemoryProperties mem_props = {0};
        vkGetPhysicalDeviceMemoryProperties(ra->physical_device, &mem_props);

        for(uint32_t i = 0; i < mem_props.memoryHeapCount; i++)
        {
            if(budgets[i].budget > 0 || budgets[i].usage > 0)
            {
                log_info("[vma] heap %u: budget=%llu usage=%llu blockBytes=%llu allocationBytes=%llu",
                         i,
                         (unsigned long long)budgets[i].budget,
                         (unsigned long long)budgets[i].usage,
                         (unsigned long long)budgets[i].statistics.blockBytes,
                         (unsigned long long)budgets[i].statistics.allocationBytes);
            }
        }
    }

    for(uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
    {
        if(ra->small_buffer_pools[i])
        {
            vmaDestroyPool(ra->allocator, ra->small_buffer_pools[i]);
            ra->small_buffer_pools[i] = VK_NULL_HANDLE;
        }
        if(ra->small_image_pools[i])
        {
            vmaDestroyPool(ra->allocator, ra->small_image_pools[i]);
            ra->small_image_pools[i] = VK_NULL_HANDLE;
        }
    }

    vmaDestroyAllocator(ra->allocator);
}


static uint64_t res_next_allocation_id(ResourceAllocator* ra)
{
    if(!ra)
        return 0;

    uint64_t id = ra->allocation_counter++;
    if(ra->leak_id == id)
    {
#if defined(_WIN32)
        if(IsDebuggerPresent())
            DebugBreak();
#elif defined(__unix__)
//	log_info(leak at )
#endif
    }

    return id;
}

static void res_set_allocation_name(ResourceAllocator* ra, VmaAllocation allocation, const char* name)
{
    if(!ra || !name)
        return;

    vmaSetAllocationName(ra->allocator, allocation, name);
}

static void res_add_leak_detection(ResourceAllocator* ra, VmaAllocation allocation)
{
    (void)ra;
    (void)allocation;
}

void vk_create_buffer(ResourceAllocator*             ra,
                      const VkBufferCreateInfo*      bufferInfo,
                      const VmaAllocationCreateInfo* allocInfo,
                      VkDeviceSize                   minalignment,
                      Buffer*                        outbuffer)
{
    VmaAllocationInfo outinfo = {0};
    VK_CHECK(vmaCreateBufferWithAlignment(ra->allocator, bufferInfo, allocInfo, minalignment, &outbuffer->buffer,
                                          &outbuffer->allocation, &outinfo));

    VkBufferUsageFlags2 usage2 = 0;
    const VkBufferUsageFlags2CreateInfo* usage2_info = (const VkBufferUsageFlags2CreateInfo*)bufferInfo->pNext;
    if(usage2_info && usage2_info->sType == VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO)
        usage2 = usage2_info->usage;

    log_info("[alloc] buffer create: size=%llu alignment=%llu flags=0x%x vma_usage=%u usage2=0x%llx pool=%p mapped=%s",
             (unsigned long long)bufferInfo->size,
             (unsigned long long)minalignment,
             (unsigned)allocInfo->flags,
             (unsigned)allocInfo->usage,
             (unsigned long long)usage2,
             (void*)allocInfo->pool,
             outinfo.pMappedData ? "yes" : "no");

    {
        uint64_t id = res_next_allocation_id(ra);
        char name[64];
        snprintf(name, sizeof(name), "buf_%llu_%llu", (unsigned long long)id, (unsigned long long)bufferInfo->size);
        res_set_allocation_name(ra, outbuffer->allocation, name);
    }

    outbuffer->buffer_size = bufferInfo->size;
    outbuffer->mapping     = (uint8_t*)outinfo.pMappedData;


    //  NEED the device to fetch device address
    VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = outbuffer->buffer};


    outbuffer->address = vkGetBufferDeviceAddress(ra->device, &addrInfo);

    res_add_leak_detection(ra, outbuffer->allocation);
}


void res_create_buffer(ResourceAllocator* ra,

                       VkDeviceSize             size,
                       VkBufferUsageFlags2KHR   usageflags,
                       VmaMemoryUsage           memory_usage,
                       VmaAllocationCreateFlags flags,
                       VkDeviceSize             min_alignment,
                       Buffer*                  out)
{
    VkBufferUsageFlags2CreateInfo usage2 = {.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
                                            .usage = usageflags | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
                                                     | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT};

    VkBufferCreateInfo buffer_info = {.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .pNext                 = &usage2,
                                      .size                  = size,
                                      .usage                 = 0,
                                      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
                                      .queueFamilyIndexCount = 0,
                                      .pQueueFamilyIndices   = NULL};

    VmaAllocationCreateInfo alloc_info = {
        .flags = flags,
        .usage = memory_usage,

    };

    VmaAllocationCreateInfo alloc_info_final = alloc_info;
    if(size <= ra->small_buffer_threshold && !(flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT))
    {
        VmaPool pool = res_get_small_buffer_pool(ra, &buffer_info, &alloc_info);
        if(pool != VK_NULL_HANDLE)
            alloc_info_final.pool = pool;
    }

    vk_create_buffer(ra, &buffer_info, &alloc_info_final, min_alignment, out);
}


void res_destroy_buffer(ResourceAllocator* ra, Buffer* buf)
{
    if(!ra || !buf)
        return;

    if(buf->buffer != VK_NULL_HANDLE)
    {
        log_info("[alloc] buffer destroy: buffer=%p size=%llu", (void*)buf->buffer, (unsigned long long)buf->buffer_size);
        vmaDestroyBuffer(ra->allocator, buf->buffer, buf->allocation);
    }

    buf->buffer      = VK_NULL_HANDLE;
    buf->allocation  = VK_NULL_HANDLE;
    buf->mapping     = NULL;
    buf->address     = 0;
    buf->buffer_size = 0;
}

void res_create_image(ResourceAllocator* ra,
                      const VkImageCreateInfo* image_info,
                      VmaMemoryUsage            memory_usage,
                      VmaAllocationCreateFlags  flags,
                      VkImage*                  out_image,
                      VmaAllocation*            out_alloc)
{
    if(!ra || !image_info || !out_image || !out_alloc)
        return;

    VmaAllocationCreateInfo alloc_info = {
        .flags = flags,
        .usage = memory_usage,
    };

    VmaAllocationCreateInfo alloc_info_final = alloc_info;
    VkDeviceSize image_size = image_info->extent.width * image_info->extent.height * image_info->extent.depth * 4; // rough estimate
    if(image_size <= ra->small_buffer_threshold && !(flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT))
    {
        VmaPool pool = res_get_small_image_pool(ra, image_info, &alloc_info);
        if(pool != VK_NULL_HANDLE)
            alloc_info_final.pool = pool;
    }

    VK_CHECK(vmaCreateImage(ra->allocator, image_info, &alloc_info_final, out_image, out_alloc, NULL));
    log_info("[alloc] image create: extent=%ux%ux%u mip=%u layers=%u format=%u flags=0x%x vma_usage=%u usage=0x%x pool=%p",
             image_info->extent.width,
             image_info->extent.height,
             image_info->extent.depth,
             image_info->mipLevels,
             image_info->arrayLayers,
             (unsigned)image_info->format,
             (unsigned)alloc_info_final.flags,
             (unsigned)alloc_info_final.usage,
             (unsigned)image_info->usage,
             (void*)alloc_info_final.pool);

    {
        uint64_t id = res_next_allocation_id(ra);
        char name[64];
        snprintf(name, sizeof(name), "img_%llu_%ux%u", (unsigned long long)id, image_info->extent.width,
                 image_info->extent.height);
        res_set_allocation_name(ra, *out_alloc, name);
    }
}

void res_destroy_image(ResourceAllocator* ra, VkImage image, VmaAllocation allocation)
{
    if(!ra || image == VK_NULL_HANDLE)
        return;
    log_info("[alloc] image destroy: image=%p", (void*)image);
    vmaDestroyImage(ra->allocator, image, allocation);
}

void buffer_arena_init(ResourceAllocator* ra,
                       VkDeviceSize             size,
                       VkBufferUsageFlags2KHR   usageflags,
                       VmaMemoryUsage           memory_usage,
                       VmaAllocationCreateFlags flags,
                       VkDeviceSize             alignment,
                       BufferArena*             out_arena)
{
    if(!ra || !out_arena)
        return;

    *out_arena = (BufferArena){0};
    out_arena->alignment = alignment ? alignment : 1;

    res_create_buffer(ra, size, usageflags, memory_usage, flags, out_arena->alignment, &out_arena->buffer);

    oa_uint32 arena_size = (oa_uint32)size;
    oa_uint32 max_nodes = (oa_uint32)(size / out_arena->alignment);
    if(max_nodes < 1024)
        max_nodes = 1024;
    if(max_nodes > 128 * 1024)
        max_nodes = 128 * 1024;

    oa_init(&out_arena->allocator, arena_size, max_nodes);
}

void buffer_arena_destroy(ResourceAllocator* ra, BufferArena* arena)
{
    if(!arena)
        return;

    oa_destroy(&arena->allocator);
    res_destroy_buffer(ra, &arena->buffer);
    *arena = (BufferArena){0};
}

BufferSlice buffer_arena_alloc(BufferArena* arena, VkDeviceSize size, VkDeviceSize alignment)
{
    if(!arena)
        return (BufferSlice){0};

    VkDeviceSize align = arena->alignment;
    if(alignment > align)
        align = alignment;

    VkDeviceSize aligned_size = res_align_up(size, align);
    OA_Allocation alloc = oa_allocate(&arena->allocator, (oa_uint32)aligned_size);
    if(alloc.offset == OA_NO_SPACE)
    {
        log_info("[alloc] arena alloc failed: size=%llu alignment=%llu", (unsigned long long)size,
                 (unsigned long long)align);
        return (BufferSlice){0};
    }

    BufferSlice slice = {0};
    slice.buffer = arena->buffer.buffer;
    slice.offset = alloc.offset;
    slice.size = aligned_size;
    slice.allocation = alloc;
    slice.mapping = arena->buffer.mapping ? arena->buffer.mapping + slice.offset : NULL;
    slice.address = arena->buffer.address + slice.offset;
    log_info("[alloc] arena alloc: size=%llu aligned=%llu offset=%llu", (unsigned long long)size,
             (unsigned long long)aligned_size, (unsigned long long)slice.offset);
    return slice;
}

void buffer_arena_free(BufferArena* arena, BufferSlice* slice)
{
    if(!arena || !slice)
        return;
    if(slice->allocation.metadata == OA_NODE_UNUSED)
        return;

    log_info("[alloc] arena free: offset=%llu size=%llu", (unsigned long long)slice->offset,
             (unsigned long long)slice->size);
    oa_free(&arena->allocator, slice->allocation);
    *slice = (BufferSlice){0};
}
void upload_to_gpu_buffer(ResourceAllocator* allocator,
                          VkQueue            queue,
                          VkCommandPool      pool,
                          VkBuffer           dst_buffer,
                          VkDeviceSize       dst_offset,
                          const void*        src_data,
                          VkDeviceSize       size)
{
    assert(src_data && size > 0);

    Buffer staging = {0};
    res_create_buffer(allocator, (uint64_t)size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,  // <-- use classic unless maintenance5 enabled
                      VMA_MEMORY_USAGE_AUTO,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &staging);

    memcpy(staging.mapping, src_data, (size_t)size);

    VkCommandBuffer cmd = begin_one_time_cmd(allocator->device, pool);

    VkBufferCopy copy = {.srcOffset = 0, .dstOffset = dst_offset, .size = size};
    vkCmdCopyBuffer(cmd, staging.buffer, dst_buffer, 1, &copy);

    end_one_time_cmd(allocator->device, queue, pool, cmd);

    res_destroy_buffer(allocator, &staging);
}
