#ifndef VK_BARRIER_H_
#define VK_BARRIER_H_

#include "vk_defaults.h"
/* -----------------------------------------------------------------------------
   Layout → SRC stage
----------------------------------------------------------------------------- */
#define VK_SRC_STAGE_FROM_LAYOUT(layout)                                                                                                      \
    ((layout) == VK_IMAGE_LAYOUT_UNDEFINED                        ? VK_PIPELINE_STAGE_2_NONE :                                                \
     (layout) == VK_IMAGE_LAYOUT_PREINITIALIZED                   ? VK_PIPELINE_STAGE_2_HOST_BIT :                                            \
     (layout) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL         ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT :                         \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT                              \
                                                                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                       \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT                                     \
                                                                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                          \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT                               \
                                                                       | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                        \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT                                      \
                                                          | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                               \
     (layout) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? (VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) : \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_PIPELINE_STAGE_2_TRANSFER_BIT :                                                    \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_PIPELINE_STAGE_2_TRANSFER_BIT :                                                    \
     (layout) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR      ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT :                                              \
                                                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)

/* -----------------------------------------------------------------------------
   Layout → SRC access
----------------------------------------------------------------------------- */
#define VK_SRC_ACCESS_FROM_LAYOUT(layout)                                                                              \
    ((layout) == VK_IMAGE_LAYOUT_UNDEFINED                        ? 0 :                                                \
     (layout) == VK_IMAGE_LAYOUT_PREINITIALIZED                   ? VK_ACCESS_2_HOST_WRITE_BIT :                       \
     (layout) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL         ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT :           \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :   \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL         ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :   \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL  ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT :    \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL          ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT :    \
     (layout) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL         ? VK_ACCESS_2_SHADER_READ_BIT :                      \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL             ? VK_ACCESS_2_TRANSFER_READ_BIT :                    \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL             ? VK_ACCESS_2_TRANSFER_WRITE_BIT :                   \
     (layout) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                  ? 0 :                                                \
                                                                    VK_ACCESS_2_MEMORY_WRITE_BIT)

/* -----------------------------------------------------------------------------
   Layout → DST stage
----------------------------------------------------------------------------- */
#define VK_DST_STAGE_FROM_LAYOUT(layout)                                                                               \
    ((layout) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ?                                                            \
         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT :                                                             \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ?                                                    \
         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                  \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ?                                                            \
         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                  \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ?                                                     \
         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                  \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ?                                                             \
         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :                  \
     (layout) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ?                                                            \
         (VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) :                             \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_PIPELINE_STAGE_2_TRANSFER_BIT :                             \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_PIPELINE_STAGE_2_TRANSFER_BIT :                             \
     (layout) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR      ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT :                       \
                                                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)

/* -----------------------------------------------------------------------------
   Layout → DST access
----------------------------------------------------------------------------- */
#define VK_DST_ACCESS_FROM_LAYOUT(layout)                                                                              \
    ((layout) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL         ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT :           \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :   \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL         ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :   \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL  ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT :    \
     (layout) == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL          ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT :    \
     (layout) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL         ? VK_ACCESS_2_SHADER_READ_BIT :                      \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL             ? VK_ACCESS_2_TRANSFER_READ_BIT :                    \
     (layout) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL             ? VK_ACCESS_2_TRANSFER_WRITE_BIT :                   \
     (layout) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                  ? 0 :                                                \
                                                   (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT))

/* -----------------------------------------------------------------------------
   Descriptor
----------------------------------------------------------------------------- */
typedef struct ImageBarrierDesc
{
    VkImage       image;
    VkImageLayout old_layout;
    VkImageLayout new_layout;

    VkPipelineStageFlags2 src_stage;
    VkPipelineStageFlags2 dst_stage;
    VkAccessFlags2        src_access;
    VkAccessFlags2        dst_access;

    VkImageAspectFlags aspect;
    uint32_t           base_mip;
    uint32_t           mip_count;
    uint32_t           base_layer;
    uint32_t           layer_count;
} ImageBarrierDesc;

/* -----------------------------------------------------------------------------
   Default descriptor (compile-time resolved)
----------------------------------------------------------------------------- */
#define IMAGE_BARRIER_IMMEDIATE_DEFAULT_FIELDS(img, oldL, newL)                                                                  \
    .image = (img), .old_layout = (oldL), .new_layout = (newL), .src_stage = VK_SRC_STAGE_FROM_LAYOUT(oldL),           \
    .dst_stage = VK_DST_STAGE_FROM_LAYOUT(newL), .src_access = VK_SRC_ACCESS_FROM_LAYOUT(oldL),                        \
    .dst_access = VK_DST_ACCESS_FROM_LAYOUT(newL), .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .base_mip = 0,                 \
    .mip_count = VK_REMAINING_MIP_LEVELS, .base_layer = 0, .layer_count = VK_REMAINING_ARRAY_LAYERS

/* -----------------------------------------------------------------------------
   Command emission (tiny, predictable, no logic)
----------------------------------------------------------------------------- */
static inline void cmd_image_barrier(VkCommandBuffer cmd, const ImageBarrierDesc* d)
{
    VkImageMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = d->src_stage,
        .dstStageMask  = d->dst_stage,
        .srcAccessMask = d->src_access,
        .dstAccessMask = d->dst_access,
        .oldLayout     = d->old_layout,
        .newLayout     = d->new_layout,
        .image         = d->image,
        .subresourceRange =
            {
                .aspectMask     = d->aspect,
                .baseMipLevel   = d->base_mip,
                .levelCount     = d->mip_count,
                .baseArrayLayer = d->base_layer,
                .layerCount     = d->layer_count,
            },
    };

    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &dep);
}

/* -----------------------------------------------------------------------------
   Variadic front-end macro
----------------------------------------------------------------------------- */

#define IMAGE_BARRIER_IMMEDIATE(cmd, img, oldL, newL, ...)                                                             \
    cmd_image_barrier((cmd), &(ImageBarrierDesc){IMAGE_BARRIER_IMMEDIATE_DEFAULT_FIELDS(img, oldL, newL), __VA_ARGS__})


typedef struct BufferBarrierDesc
{
    VkBuffer     buffer;
    VkDeviceSize offset;
    VkDeviceSize size;

    VkPipelineStageFlags2 src_stage;
    VkPipelineStageFlags2 dst_stage;

    VkAccessFlags2 src_access;
    VkAccessFlags2 dst_access;

    uint32_t src_queue_family;
    uint32_t dst_queue_family;
} BufferBarrierDesc;
#define INFER_ACCESS_WRITE_FROM_STAGE(stage) ( \
    ((((stage) & (VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)) != 0) \
        ? VK_ACCESS_2_MEMORY_WRITE_BIT : 0) \
  | ((((stage) & (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | \
                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | \
                  VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | \
                  VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT | \
                  VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT)) != 0) \
        ? VK_ACCESS_2_SHADER_WRITE_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_HOST_BIT) != 0) \
        ? VK_ACCESS_2_HOST_WRITE_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_TRANSFER_BIT) != 0) \
        ? VK_ACCESS_2_TRANSFER_WRITE_BIT : 0) \
  | ((((stage) & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | \
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0) \
        ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0) \
        ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT) != 0) \
        ? VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT : 0) \
)

#define INFER_ACCESS_READ_FROM_STAGE(stage) ( \
    ((((stage) & (VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)) != 0) \
        ? VK_ACCESS_2_MEMORY_READ_BIT : 0) \
  | ((((stage) & (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | \
                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | \
                  VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | \
                  VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT | \
                  VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | \
                  VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT)) != 0) \
        ? (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT) : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_HOST_BIT) != 0) \
        ? VK_ACCESS_2_HOST_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_TRANSFER_BIT) != 0) \
        ? VK_ACCESS_2_TRANSFER_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT) != 0) \
        ? VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT) != 0) \
        ? VK_ACCESS_2_INDEX_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) != 0) \
        ? VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT : 0) \
  | ((((stage) & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | \
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0) \
        ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0) \
        ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT : 0) \
  | ((((stage) & VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT) != 0) \
        ? VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT : 0) \
)

#define BUFFER_BARRIER_DEFAULT_FIELDS(buf, srcStage, dstStage) \
    .buffer = (buf), \
    .offset = 0, \
    .size = VK_WHOLE_SIZE, \
    .src_stage = (srcStage), \
    .dst_stage = (dstStage), \
    .src_access = INFER_ACCESS_WRITE_FROM_STAGE(srcStage), \
    .dst_access = INFER_ACCESS_READ_FROM_STAGE(dstStage), \
    .src_queue_family = VK_QUEUE_FAMILY_IGNORED, \
    .dst_queue_family = VK_QUEUE_FAMILY_IGNORED


static inline void cmd_buffer_barrier(VkCommandBuffer cmd, const BufferBarrierDesc* d)
{
    VkBufferMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask        = d->src_stage,
        .dstStageMask        = d->dst_stage,
        .srcAccessMask       = d->src_access,
        .dstAccessMask       = d->dst_access,
        .srcQueueFamilyIndex = d->src_queue_family,
        .dstQueueFamilyIndex = d->dst_queue_family,
        .buffer              = d->buffer,
        .offset              = d->offset,
        .size                = d->size,
    };

    VkDependencyInfo dep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &dep);
}

#define BUFFER_BARRIER_IMMEDIATE(cmd, buf, srcStage, dstStage, ...) \
    cmd_buffer_barrier((cmd), &(BufferBarrierDesc){ \
        BUFFER_BARRIER_DEFAULT_FIELDS(buf, srcStage, dstStage), \
        __VA_ARGS__ \
    })

typedef struct BarrierBatch
{
    VkImageMemoryBarrier2  image[16];
    VkBufferMemoryBarrier2 buffer[16];

    uint32_t image_count;
    uint32_t buffer_count;
} BarrierBatch;


#define BEGIN_BARRIER_SCOPE(cmd)                                              \
    for(BarrierBatch __bb = {0};                                             \
        /* init */ (__bb.image_count = 0, __bb.buffer_count = 0, 1);        \
        /* flush once */                                                     \
        ({                                                                   \
            VkDependencyInfo dep = {                                         \
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,                  \
                .imageMemoryBarrierCount  = __bb.image_count,                \
                .pImageMemoryBarriers     = __bb.image,                      \
                .bufferMemoryBarrierCount = __bb.buffer_count,               \
                .pBufferMemoryBarriers    = __bb.buffer,                     \
            };                                                               \
            vkCmdPipelineBarrier2((cmd), &dep);                              \
            0;                                                               \
        }))


#define ADD_IMAGE_BARRIER_TO_SCOPE(desc_ptr)                                  \
    do {                                                                      \
        extern BarrierBatch __bb;                                             \
        VkImageMemoryBarrier2* b = &__bb.image[__bb.image_count++];           \
        *b = (VkImageMemoryBarrier2){                                         \
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,               \
            .srcStageMask = (desc_ptr)->src_stage,                            \
            .dstStageMask = (desc_ptr)->dst_stage,                            \
            .srcAccessMask = (desc_ptr)->src_access,                          \
            .dstAccessMask = (desc_ptr)->dst_access,                          \
            .oldLayout = (desc_ptr)->old_layout,                              \
            .newLayout = (desc_ptr)->new_layout,                              \
            .image = (desc_ptr)->image,                                       \
            .subresourceRange = {                                             \
                .aspectMask = (desc_ptr)->aspect,                             \
                .baseMipLevel = (desc_ptr)->base_mip,                         \
                .levelCount = (desc_ptr)->mip_count,                          \
                .baseArrayLayer = (desc_ptr)->base_layer,                     \
                .layerCount = (desc_ptr)->layer_count,                        \
            },                                                                \
        };                                                                    \
    } while(0)


#define IMAGE_BARRIER(cmd, img, oldL, newL, ...)                              \
    do {                                                                      \
        ImageBarrierDesc tmp = {                                              \
            IMAGE_BARRIER_DEFAULT_FIELDS(img, oldL, newL),                    \
            __VA_ARGS__                                                       \
        };                                                                    \
        ADD_IMAGE_BARRIER_TO_SCOPE(&tmp);                                     \
    } while(0)


#define ADD_BUFFER_BARRIER_TO_SCOPE(desc_ptr)                                 \
    do {                                                                      \
        extern BarrierBatch __bb;                                             \
        VkBufferMemoryBarrier2* b = &__bb.buffer[__bb.buffer_count++];        \
        *b = (VkBufferMemoryBarrier2){                                        \
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,              \
            .srcStageMask = (desc_ptr)->src_stage,                            \
            .dstStageMask = (desc_ptr)->dst_stage,                            \
            .srcAccessMask = (desc_ptr)->src_access,                          \
            .dstAccessMask = (desc_ptr)->dst_access,                          \
            .srcQueueFamilyIndex = (desc_ptr)->src_queue_family,              \
            .dstQueueFamilyIndex = (desc_ptr)->dst_queue_family,              \
            .buffer = (desc_ptr)->buffer,                                     \
            .offset = (desc_ptr)->offset,                                     \
            .size   = (desc_ptr)->size,                                       \
        };                                                                    \
    } while(0)


#define BUFFER_BARRIER(cmd, buf, srcStage, dstStage, ...)                     \
    do {                                                                      \
        BufferBarrierDesc tmp = {                                             \
            BUFFER_BARRIER_DEFAULT_FIELDS(buf, srcStage, dstStage),           \
            __VA_ARGS__                                                       \
        };                                                                    \
        ADD_BUFFER_BARRIER_TO_SCOPE(&tmp);                                    \
    } while(0)


#endif // VK_BARRIER









