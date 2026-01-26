#include "vk_defaults.h"

#ifndef DEBUG_TEXT_MAX_CHARS
#define DEBUG_TEXT_MAX_CHARS 112
#endif

#include "desc_write.h"
#include "vk_barrier.h"
#include "vk_pipelines.h"  // your create_compute_pipeline
#include "vk_swapchain.h"  // FlowSwapchain

typedef struct TextDataPC
{
    int32_t  offset[2];  // ivec2
    int32_t  scale;      // int
    uint32_t color;      // packed RGBA8
    uint32_t data[28];   // 112 bytes packed ASCII
} TextDataPC;

_Static_assert(sizeof(TextDataPC) == 128, "TextDataPC must be 128 bytes");

typedef struct VkDebugText
{
    VkDevice device;

    VkPipelineLayout layout;
    VkPipeline       pipeline;

    VkDescriptorSetLayout set_layout;

    // one descriptor set per swapchain image (binding 0 = storage image)
    VkDescriptorSet* sets;
    uint32_t         set_count;

    // CPU side queue of draw calls
    struct DebugTextCmd
    {
        TextDataPC pc;
        uint32_t   len;
    } queued[256];

    uint32_t queued_count;
} VkDebugText;

uint32_t pack_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void vk_debug_text_init(VkDebugText*           dt,
                        VkDevice               device,
                        DescriptorAllocator*   persistent_desc,
                        DescriptorLayoutCache* desc_cache,
                        PipelineLayoutCache*   pipe_cache,
                        const FlowSwapchain*   swap,
                        const char*            comp_spv_path);

void vk_debug_text_destroy(VkDebugText* dt);

void vk_debug_text_on_swapchain_recreated(VkDebugText*           dt,
                                          DescriptorAllocator*   persistent_desc,
                                          DescriptorLayoutCache* layout_cache,
                                          const FlowSwapchain*   swap);

void vk_debug_text_begin_frame(VkDebugText* dt);

void vk_debug_text_printf(VkDebugText* dt, int x, int y, int scale, uint32_t rgba, const char* fmt, ...);

void vk_debug_text_flush(VkDebugText* dt, VkCommandBuffer cmd, VkImage target_image, uint32_t swapchain_image_index);
