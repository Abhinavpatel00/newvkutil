#ifndef VK_DEFAULTS_H_
#define VK_DEFAULTS_H_

#include "tinytypes.h"

#include "external/xxHash/xxhash.h"

typedef uint32_t Hash32;
typedef uint64_t Hash64;

uint32_t hash32_bytes(const void* data, size_t size);
uint64_t hash64_bytes(const void* data, size_t size);
size_t c99_strnlen(const char* s, size_t maxlen);


#define VK_IMAGE_VIEW_DEFAULT(img, fmt)                                                                          \
    (VkImageViewCreateInfo)                                                                                            \
    {                                                                                                                  \
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,                                                          \
        .pNext    = NULL,                                                                                              \
        .flags    = 0,                                                                                                 \
        .image    = (img),                                                                                             \
        .viewType = VK_IMAGE_VIEW_TYPE_2D,                                                                             \
        .format   = (fmt),                                                                                             \
        .components = {                                                                                                \
            VK_COMPONENT_SWIZZLE_IDENTITY,                                                                             \
            VK_COMPONENT_SWIZZLE_IDENTITY,                                                                             \
            VK_COMPONENT_SWIZZLE_IDENTITY,                                                                             \
            VK_COMPONENT_SWIZZLE_IDENTITY,                                                                             \
        },                                                                                                             \
        .subresourceRange = {                                                                                          \
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,                                                               \
            .baseMipLevel   = 0,                                                                                       \
            .levelCount     = VK_REMAINING_MIP_LEVELS,                                                                 \
            .baseArrayLayer = 0,                                                                                       \
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,                                                               \
        },                                                                                                             \
    }



#define VK_IMAGE_DEFAULT_2D(w, h, fmt, usageFlags)                                                                     \
    (VkImageCreateInfo){                                                                                               \
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,                                                          \
        .pNext         = NULL,                                                                                         \
        .flags         = 0,                                                                                            \
        .imageType     = VK_IMAGE_TYPE_2D,                                                                             \
        .format        = (fmt),                                                                                        \
        .extent        = { (uint32_t)(w), (uint32_t)(h), 1 },                                                          \
        .mipLevels     = 1,                                                                                            \
        .arrayLayers   = 1,                                                                                            \
        .samples       = VK_SAMPLE_COUNT_1_BIT,                                                                        \
        .tiling        = VK_IMAGE_TILING_OPTIMAL,                                                                      \
        .usage         = (usageFlags),                                                                                 \
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,                                                                    \
        .queueFamilyIndexCount = 0,                                                                                    \
        .pQueueFamilyIndices   = NULL,                                                                                 \
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,                                                                    \
    }

#endif // VK_DEFAULTS_H_
