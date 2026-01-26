#include "desc_write.h"
// ============================================================
// Descriptor Writer
// ============================================================

void desc_writer_begin(DescriptorWriter* w)
{
    memset(w, 0, sizeof(*w));
}

bool desc_writer_write_buffer(DescriptorWriter* w, VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
    if(w->buffer_info_count >= 64 || w->write_count >= 64)
        return false;

    VkDescriptorBufferInfo* bi = &w->buffer_infos[w->buffer_info_count++];
    *bi                        = (VkDescriptorBufferInfo){
                               .buffer = buffer,
                               .offset = offset,
                               .range  = range,
    };

    w->writes[w->write_count++] = (VkWriteDescriptorSet){
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set,
        .dstBinding      = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = type,
        .pBufferInfo     = bi,
    };

    return true;
}

bool desc_writer_write_image(DescriptorWriter* w, VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkImageView view, VkSampler sampler, VkImageLayout layout)
{
    if(w->image_info_count >= 64 || w->write_count >= 64)
        return false;

    VkDescriptorImageInfo* ii = &w->image_infos[w->image_info_count++];
    *ii                       = (VkDescriptorImageInfo){
                              .sampler     = sampler,
                              .imageView   = view,
                              .imageLayout = layout,
    };

    w->writes[w->write_count++] = (VkWriteDescriptorSet){
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set,
        .dstBinding      = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = type,
        .pImageInfo      = ii,
    };

    return true;
}

void desc_writer_commit(VkDevice device, DescriptorWriter* w)
{
    if(w->write_count == 0)
        return;

    vkUpdateDescriptorSets(device, w->write_count, w->writes, 0, NULL);
}

// ============================================================
// Allocate + Write helper
// ============================================================

VkResult descriptor_build_set(DescriptorAllocator*                   alloc,
                              DescriptorLayoutCache*                 cache,
                              const VkDescriptorSetLayoutCreateInfo* layout_info,
                              const DescriptorWriter*                writer_template,
                              VkDescriptorSet*                       out_set)
{
    VkDescriptorSetLayout layout = descriptor_layout_cache_get(cache, layout_info);

    VkResult r = descriptor_allocator_allocate(alloc, layout, out_set);
    if(r != VK_SUCCESS)
        return r;

    DescriptorWriter w = *writer_template;
    for(uint32_t i = 0; i < w.write_count; i++)
        w.writes[i].dstSet = *out_set;

    desc_writer_commit(alloc->device, &w);
    return VK_SUCCESS;
}
