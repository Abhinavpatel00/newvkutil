#pragma once

#include "vk_defaults.h"
#include "vk_descriptor.h"
#include "vk_resources.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct TextureResource
{
    VkImage     image;
    VkImageView view;
    VkSampler   sampler;

    VmaAllocation allocation;

    uint32_t width, height;
    VkFormat format;

    // bindless slot index == TextureID
    uint32_t bindless_index;
} TextureResource;

typedef struct BindlessTextures
{
    VkDescriptorSetLayout layout;
    VkDescriptorSet       set;

    uint32_t max_textures;
    uint32_t next_free;

    TextureResource textures[MAX_BINDLESS_TEXTURES];
    uint32_t        free_list[MAX_BINDLESS_TEXTURES];
    uint32_t        free_count;
} BindlessTextures;

#define TEX_SLOT_AUTO UINT32_MAX

void bindless_textures_init(BindlessTextures* bt, VkDevice device, DescriptorAllocator* alloc, DescriptorLayoutCache* cache,
                            uint32_t max_textures);
void bindless_textures_destroy(BindlessTextures* bt, ResourceAllocator* allocator, VkDevice device);

uint32_t bindless_textures_alloc_slot(BindlessTextures* bt);
void bindless_textures_write(BindlessTextures* bt, VkDevice device, uint32_t slot, VkImageView view, VkSampler sampler,
                             VkImageLayout layout);

bool bindless_textures_create_rgba8(ResourceAllocator* allocator, VkDevice device, VkQueue queue, VkCommandPool pool,
                                    uint32_t w, uint32_t h, const uint8_t* pixels, TextureResource* out_tex);
void bindless_textures_destroy_texture(ResourceAllocator* allocator, VkDevice device, TextureResource* tex);

bool tex_create_from_rgba8_cpu(BindlessTextures* bindless,
                               ResourceAllocator* allocator,
                               VkDevice device,
                               VkQueue queue,
                               VkCommandPool pool,
                               uint32_t w,
                               uint32_t h,
                               const uint8_t* pixels,
                               uint32_t slot_hint,
                               uint32_t* out_slot);

bool tex_create_from_file_rgba8(BindlessTextures* bindless,
                                ResourceAllocator* allocator,
                                VkDevice device,
                                VkQueue queue,
                                VkCommandPool pool,
                                const char* path,
                                uint32_t slot_hint,
                                uint32_t* out_slot);

bool tex_destroy(BindlessTextures* bindless, ResourceAllocator* allocator, VkDevice device, uint32_t slot);
