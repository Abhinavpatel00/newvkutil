#ifndef VK_STARTUP_H_
#define VK_STARTUP_H_

#include "vk_defaults.h"
#include "vk_queue.h"


typedef struct VkFeatureChain
{
    VkPhysicalDeviceFeatures2        core;

    VkPhysicalDeviceVulkan11Features v11;
    VkPhysicalDeviceVulkan12Features v12;
    VkPhysicalDeviceVulkan13Features v13;


    // ---- add this ----
    VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5;
} VkFeatureChain;


typedef struct VkPropertyChain
{
    // Core properties2 head
    VkPhysicalDeviceProperties2 core;

    // ------------------------------------------------------------
    // Descriptor Indexing Properties (EXT)
    //
    // These are LIMITS and "how native is this" indicators.
    // Example:
    // - maxPerStageDescriptorUpdateAfterBindSampledImages
    // - maxUpdateAfterBindDescriptorsInAllPools
    //
    // You don't "enable" these, you read them to size your bindless arrays.
    // ------------------------------------------------------------
    VkPhysicalDeviceDescriptorIndexingProperties desc_indexing;

} VkPropertyChain;
typedef struct renderer_context_desc
{
    const char* app_name;

    const char** instance_layers;
    const char** instance_extensions;
    const char** device_extensions;

    uint32_t instance_layer_count;
    uint32_t instance_extension_count;
    uint32_t device_extension_count;
    int      enable_validation;
    int      enable_gpu_based_validation;

    VkDebugUtilsMessageSeverityFlagsEXT validation_severity;
    VkDebugUtilsMessageTypeFlagsEXT     validation_types;
    bool                                use_custom_features;
    VkFeatureChain                      custom_features;
} renderer_context_desc;


typedef struct renderer_context
{
    VkInstance instance;

    VkDebugUtilsMessengerEXT debug_utils;
    uint32_t                 debug_utils_enabled : 1;
} renderer_context;


void vk_create_instance(renderer_context* ctx, const renderer_context_desc* desc);

void             setup_debug_messenger(renderer_context* ctx, const renderer_context_desc* desc);
VkPhysicalDevice pick_physical_device(VkInstance instance, VkSurfaceKHR surface, renderer_context_desc* desc);

void create_device(VkPhysicalDevice       physical,
                   VkSurfaceKHR           surface,
                   renderer_context_desc* renderer_ctx_desc,
                   const queue_families   q,
                   VkDevice*              out_device);


bool device_supports_extensions(VkPhysicalDevice gpu, const char** req, uint32_t req_count);
bool is_instance_extension_supported(const char* extension_name);

void query_device_features(VkPhysicalDevice gpu, VkFeatureChain* out);


typedef struct RendererCaps {
    bool dynamic_rendering;
    bool sync2;
    bool descriptor_indexing;
    bool timeline_semaphores;
    bool multi_draw_indirect;
    bool multi_draw_indirect_count;
    bool buffer_device_address;
    bool maintenance4;
    bool bindless_textures;

    bool sampler_anisotropy;   // NEW
    bool atomic_int64;         // NEW
    bool scalar_block_layout;  // NEW
    bool robustness2;          // NEW
    bool index_type_uint8;     // NEW
    bool subgroup_size_control;// NEW
} RendererCaps;

//
//
// vk_vk_create_instance ()
// pick_physical_device()
// find_queue_families()
// create_device()
// init_device_queues()
//
//
//
//
#endif  // VK_STARTUP_H_
