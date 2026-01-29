#ifndef VK_PIPELINE_CACHE_H_
#define VK_PIPELINE_CACHE_H_

// Single-file Vulkan pipeline cache utility.
// Loads a pipeline cache from disk (safely), validates it,
// and falls back to empty cache if anything smells wrong.
//
#ifndef PIPELINE_CACHE_MAGIC
#define PIPELINE_CACHE_MAGIC 0xCAFEBABE
#endif

#include "vk_defaults.h"
typedef struct PipelineCachePrefixHeader
{
    uint32_t magic;
    uint32_t dataSize;
    uint64_t dataHash;

    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t driverVersion;
    uint32_t driverABI;

    uint8_t uuid[VK_UUID_SIZE];
} PipelineCachePrefixHeader;

// very boring but reliable hash (FNV-1a)


static int write_all(FILE* f, const void* data, size_t size)
{
    return fwrite(data, 1, size, f) == size;
}

static int read_all(FILE* f, void* data, size_t size)
{
    return fread(data, 1, size, f) == size;
}

static void get_device_props(VkPhysicalDevice phys, VkPhysicalDeviceProperties* out)
{
    vkGetPhysicalDeviceProperties(phys, out);
}

static int validate_header(const PipelineCachePrefixHeader* h, const VkPhysicalDeviceProperties* props)
{
    if(h->magic != PIPELINE_CACHE_MAGIC)
        return 0;
    if(h->driverABI != sizeof(void*))
        return 0;
    if(h->vendorID != props->vendorID)
        return 0;
    if(h->deviceID != props->deviceID)
        return 0;
    if(h->driverVersion != props->driverVersion)
        return 0;
    if(memcmp(h->uuid, props->pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return 0;
    return 1;
}

VkPipelineCache pipeline_cache_load_or_create(VkDevice device, VkPhysicalDevice phys, const char* path)
{
    VkPhysicalDeviceProperties props;
    get_device_props(phys, &props);

    VkPipelineCache cache = VK_NULL_HANDLE;

    FILE* f = fopen(path, "rb");
    if(!f)
    {
        // file missing, build empty cache
        VkPipelineCacheCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        vkCreatePipelineCache(device, &ci, NULL, &cache);
        return cache;
    }

    PipelineCachePrefixHeader hdr;
    if(!read_all(f, &hdr, sizeof(hdr)))
    {
        fclose(f);
        goto fallback;
    }

    if(!validate_header(&hdr, &props))
    {
        fclose(f);.
        goto fallback;
    }

    void* blob = malloc(hdr.dataSize);
    if(!blob)
    {
        fclose(f);
        goto fallback;
    }

    if(!read_all(f, blob, hdr.dataSize))
    {
        free(blob);
        fclose(f);
        goto fallback;
    }

    fclose(f);

    if(hash64_bytes(blob, hdr.dataSize) != hdr.dataHash)
    {
        free(blob);
        goto fallback;
    }

    VkPipelineCacheCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, .initialDataSize = hdr.dataSize, .pInitialData = blob};

    VkResult res = vkCreatePipelineCache(device, &ci, NULL, &cache);
    free(blob);

    if(res != VK_SUCCESS)
    {
    fallback:
        // sigh… drivers
        VkPipelineCacheCreateInfo empty = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        vkCreatePipelineCache(device, &empty, NULL, &cache);
    }

    return cache;
}

void pipeline_cache_save(VkDevice device, VkPhysicalDevice phys, VkPipelineCache cache, const char* path)
{
    size_t size = 0;
    vkGetPipelineCacheData(device, cache, &size, NULL);
    if(size == 0)
        return;

    void* blob = malloc(size);
    if(!blob)
        return;

    vkGetPipelineCacheData(device, cache, &size, blob);

    VkPhysicalDeviceProperties props;
    get_device_props(phys, &props);

    PipelineCachePrefixHeader hdr = {.magic         = PIPELINE_CACHE_MAGIC,
                                     .dataSize      = (uint32_t)size,
                                     .dataHash      = hash64_bytes(blob, size),
                                     .vendorID      = props.vendorID,
                                     .deviceID      = props.deviceID,
                                     .driverVersion = props.driverVersion,
                                     .driverABI     = sizeof(void*)};
    memcpy(hdr.uuid, props.pipelineCacheUUID, VK_UUID_SIZE);

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if(!f)
    {
        free(blob);
        return;
    }

    write_all(f, &hdr, sizeof(hdr));
    write_all(f, blob, size);
    fclose(f);

    rename(tmp, path);
    free(blob);
}

#endif // VK_PIPELINE_CACHE_H_
