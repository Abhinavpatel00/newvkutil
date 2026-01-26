#pragma once
#include "vk_defaults.h"
#include "vk_descriptor.h"
// ------------------------------------------------------------
// Writer
// ------------------------------------------------------------
typedef struct DescriptorWriter
{
    VkWriteDescriptorSet writes[64];
    uint32_t             write_count;

    VkDescriptorBufferInfo buffer_infos[64];
    uint32_t               buffer_info_count;

    VkDescriptorImageInfo image_infos[64];
    uint32_t              image_info_count;
} DescriptorWriter;

void desc_writer_begin(DescriptorWriter* w);

bool desc_writer_write_buffer(DescriptorWriter* w,
                              VkDescriptorSet   set,
                              uint32_t          binding,
                              VkDescriptorType  type,
                              VkBuffer          buffer,
                              VkDeviceSize      offset,
                              VkDeviceSize      range);

bool desc_writer_write_image(DescriptorWriter* w,
                             VkDescriptorSet   set,
                             uint32_t          binding,
                             VkDescriptorType  type,
                             VkImageView       view,
                             VkSampler         sampler,
                             VkImageLayout     layout);

void desc_writer_commit(VkDevice device, DescriptorWriter* w);

// ------------------------------------------------------------
// Allocate + write helper
// ------------------------------------------------------------
VkResult descriptor_build_set(DescriptorAllocator*                   alloc,
                              DescriptorLayoutCache*                 cache,
                              const VkDescriptorSetLayoutCreateInfo* layout_info,
                              const DescriptorWriter*                writer_template,
                              VkDescriptorSet*                       out_set);
