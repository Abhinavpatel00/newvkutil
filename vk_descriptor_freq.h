/*
 * =============================================================================
 * FREQUENCY-BASED DESCRIPTOR MANAGEMENT
 * =============================================================================
 *
 * DESIGN PHILOSOPHY:
 * ------------------
 * This system organizes descriptors by their update frequency, minimizing
 * the number of vkCmdBindDescriptorSets calls and reducing CPU overhead.
 *
 * The key insight is that different data changes at different rates:
 * - Global/per-frame data (camera, time, lighting) changes once per frame
 * - Material data changes per-material batch
 * - Per-draw data (transforms, object IDs) changes every draw call
 *
 * By grouping data by frequency, we bind once and reuse as much as possible.
 *
 * SET ORGANIZATION:
 * -----------------
 *
 * SET 0: GLOBAL (per-frame, rarely changes)
 * ----------------------------------------
 * - Camera matrices (view, projection, viewproj, inverse variants)
 * - Time uniforms (total time, delta time, frame count)
 * - Global lighting (sun direction, ambient color)
 * - Shadow map cascades (if applicable)
 * - Environment maps (skybox, IBL)
 *
 * Binding frequency: Once per frame (or once per render pass)
 * Memory strategy: Ring buffer with N frames in flight
 * Allocation: Persistent, preallocated
 *
 * SET 1: MATERIAL (per-material, changes with material switches)
 * --------------------------------------------------------------
 * - Material textures (albedo, normal, metallic-roughness, etc.)
 * - Material parameters (PBR factors, tint colors)
 * - Sampler configurations
 *
 * Binding frequency: Once per material batch
 * Memory strategy: Persistent descriptor sets, cached
 * Allocation: Pool of descriptor sets, allocated on material creation
 *
 * SET 2: PER-DRAW (per-object, changes every draw)
 * -------------------------------------------------
 * - Model matrix / transform
 * - Object ID / instance data
 * - Skinning palette (for animated meshes)
 *
 * Binding frequency: Every draw call (via dynamic offsets or push constants)
 * Memory strategy: Dynamic uniform buffer with offsets
 * Allocation: Large UBO, write transforms sequentially, bind with dynamic offset
 *
 * PERFORMANCE NOTES:
 * ------------------
 * 1. Sort draw calls by material to minimize Set 1 rebinds
 * 2. Use dynamic offsets for Set 2 to avoid descriptor set switches
 * 3. Consider push constants for very small per-draw data (< 128 bytes)
 * 4. Double/triple buffer Set 0 data for frames in flight
 *
 * VULKAN 1.3 FEATURES USED:
 * -------------------------
 * - Dynamic uniform buffer offsets (core)
 * - Timeline semaphores for frame sync (core)
 * - Dynamic rendering (VK_KHR_dynamic_rendering, core in 1.3)
 *
 * =============================================================================
 */

#ifndef VK_DESCRIPTOR_FREQ_H_
#define VK_DESCRIPTOR_FREQ_H_

#include "vk_defaults.h"
#include "vk_resources.h"

// Maximum frames in flight for per-frame resources
#define FREQ_MAX_FRAMES_IN_FLIGHT 3

// Maximum materials that can be registered
#define FREQ_MAX_MATERIALS 1024

// Per-draw buffer size (how many transforms per frame)
#define FREQ_MAX_DRAWS_PER_FRAME 16384

// Alignment for dynamic uniform buffer offsets
#define FREQ_MIN_UBO_ALIGNMENT 256

/* =============================================================================
 * DATA STRUCTURES - GPU SIDE
 * =============================================================================
 */

// Set 0, Binding 0: Global uniforms (140 bytes aligned to 256)
typedef struct FreqGlobalData
{
    float view[16];        // 64 bytes
    float projection[16];  // 64 bytes
    float viewproj[16];    // 64 bytes
    float camera_pos[4];   // 16 bytes (xyz + padding)
    float time;            // 4 bytes
    float delta_time;      // 4 bytes
    uint32_t frame_count;  // 4 bytes
    float _pad0;           // 4 bytes (align to 16)
} FreqGlobalData;

// Set 0, Binding 1: Light data
typedef struct FreqLightData
{
    float sun_direction[4];  // 16 bytes (xyz + intensity)
    float sun_color[4];      // 16 bytes (rgb + padding)
    float ambient_color[4];  // 16 bytes (rgb + intensity)
    uint32_t light_count;    // 4 bytes
    float _pad[3];           // 12 bytes
} FreqLightData;

// Set 1: Material parameters (alongside texture bindings)
typedef struct FreqMaterialParams
{
    float base_color_factor[4];    // 16 bytes
    float metallic_factor;         // 4 bytes
    float roughness_factor;        // 4 bytes
    float normal_scale;            // 4 bytes
    float occlusion_strength;      // 4 bytes
    float emissive_factor[4];      // 16 bytes
    uint32_t flags;                // 4 bytes (bitfield for enabled features)
    float alpha_cutoff;            // 4 bytes
    float _pad[2];                 // 8 bytes (align to 64)
} FreqMaterialParams;

// Set 2, Binding 0: Per-draw transform (with dynamic offset)
typedef struct FreqDrawData
{
    float model[16];          // 64 bytes
    float normal_matrix[12];  // 48 bytes (mat3x4, row-major)
    uint32_t object_id;       // 4 bytes
    uint32_t material_idx;    // 4 bytes
    float _pad[2];            // 8 bytes (align to 128)
} FreqDrawData;


/* =============================================================================
 * DESCRIPTOR SET LAYOUTS
 * =============================================================================
 */

// Set 0 layout info
typedef struct FreqSet0Layout
{
    VkDescriptorSetLayout layout;
    
    // Binding indices (for reference)
    // Binding 0: UBO for FreqGlobalData
    // Binding 1: UBO for FreqLightData
    // Binding 2: Combined image sampler for shadow map (optional)
    // Binding 3: Combined image sampler for environment map (optional)
} FreqSet0Layout;

// Set 1 layout info
typedef struct FreqSet1Layout
{
    VkDescriptorSetLayout layout;
    
    // Binding indices:
    // Binding 0: UBO for FreqMaterialParams
    // Binding 1: Combined image sampler for albedo
    // Binding 2: Combined image sampler for normal
    // Binding 3: Combined image sampler for metallic-roughness
    // Binding 4: Combined image sampler for occlusion
    // Binding 5: Combined image sampler for emissive
} FreqSet1Layout;

// Set 2 layout info
typedef struct FreqSet2Layout
{
    VkDescriptorSetLayout layout;
    
    // Binding indices:
    // Binding 0: Dynamic UBO for FreqDrawData
} FreqSet2Layout;


/* =============================================================================
 * MATERIAL SYSTEM
 * =============================================================================
 */

typedef struct FreqMaterial
{
    VkDescriptorSet set;           // Set 1 descriptor
    FreqMaterialParams params;     // CPU-side params
    Buffer param_buffer;           // GPU buffer for params
    
    // Texture handles (VkImageView + VkSampler pairs)
    VkDescriptorImageInfo albedo;
    VkDescriptorImageInfo normal;
    VkDescriptorImageInfo metallic_roughness;
    VkDescriptorImageInfo occlusion;
    VkDescriptorImageInfo emissive;
    
    uint32_t material_id;
    bool dirty;  // Needs GPU update
} FreqMaterial;


/* =============================================================================
 * PER-FRAME RESOURCES
 * =============================================================================
 */

typedef struct FreqFrameResources
{
    // Set 0 resources
    Buffer global_buffer;          // FreqGlobalData
    Buffer light_buffer;           // FreqLightData
    VkDescriptorSet set0;          // Bound once per frame
    
    // Set 2 resources (per-draw dynamic buffer)
    Buffer draw_buffer;            // Large buffer for FreqDrawData
    VkDescriptorSet set2;          // Bound with dynamic offset
    
    uint32_t draw_count;           // How many draws this frame
    uint32_t draw_buffer_offset;   // Current write offset
} FreqFrameResources;


/* =============================================================================
 * MAIN SYSTEM
 * =============================================================================
 */

typedef struct FreqDescriptorSystem
{
    VkDevice device;
    ResourceAllocator* allocator;
    
    // Layouts (shared across all frames/materials)
    FreqSet0Layout set0_layout;
    FreqSet1Layout set1_layout;
    FreqSet2Layout set2_layout;
    
    // Descriptor pool for the system
    VkDescriptorPool pool;
    
    // Per-frame resources (ring buffer)
    FreqFrameResources frames[FREQ_MAX_FRAMES_IN_FLIGHT];
    uint32_t current_frame;
    
    // Material registry
    FreqMaterial* materials;  // stb_ds array
    
    // Default textures (1x1 white, normal, etc.)
    VkImageView default_white;
    VkImageView default_normal;
    VkImageView default_black;
    VkSampler default_sampler;
    
} FreqDescriptorSystem;


/* =============================================================================
 * API - INITIALIZATION
 * =============================================================================
 */

// Initialize the frequency-based descriptor system
void freq_init(FreqDescriptorSystem* sys, VkDevice device, ResourceAllocator* allocator);

// Destroy the system and all resources
void freq_destroy(FreqDescriptorSystem* sys);

// Create the default textures (call after freq_init)
void freq_create_defaults(FreqDescriptorSystem* sys, VkCommandBuffer cmd);


/* =============================================================================
 * API - PER-FRAME
 * =============================================================================
 */

// Begin a new frame (advances frame index, resets per-draw counter)
void freq_begin_frame(FreqDescriptorSystem* sys);

// Update Set 0 global data (call once per frame)
void freq_update_global(FreqDescriptorSystem* sys, const FreqGlobalData* global, const FreqLightData* lights);

// Get the current Set 0 descriptor (to bind at start of frame)
VkDescriptorSet freq_get_set0(FreqDescriptorSystem* sys);


/* =============================================================================
 * API - MATERIALS (Set 1)
 * =============================================================================
 */

// Create a new material, returns material ID
uint32_t freq_material_create(FreqDescriptorSystem* sys, const FreqMaterialParams* params);

// Update material textures
void freq_material_set_textures(FreqDescriptorSystem* sys, uint32_t material_id,
                                  VkImageView albedo, VkImageView normal,
                                  VkImageView metallic_roughness, VkImageView occlusion,
                                  VkImageView emissive, VkSampler sampler);

// Update material parameters
void freq_material_set_params(FreqDescriptorSystem* sys, uint32_t material_id, const FreqMaterialParams* params);

// Flush dirty materials to GPU
void freq_material_flush(FreqDescriptorSystem* sys);

// Get Set 1 descriptor for a material
VkDescriptorSet freq_material_get_set(FreqDescriptorSystem* sys, uint32_t material_id);

// Destroy a material
void freq_material_destroy(FreqDescriptorSystem* sys, uint32_t material_id);


/* =============================================================================
 * API - PER-DRAW (Set 2)
 * =============================================================================
 */

// Allocate space for a draw call, returns dynamic offset
// Write your FreqDrawData to the returned pointer
uint32_t freq_alloc_draw(FreqDescriptorSystem* sys, FreqDrawData** out_data);

// Get Set 2 with the dynamic offset for binding
VkDescriptorSet freq_get_set2(FreqDescriptorSystem* sys);

// Get all three layouts for pipeline creation
void freq_get_layouts(FreqDescriptorSystem* sys, VkDescriptorSetLayout* out_layouts);

// Get the pipeline layout (creates internally if needed)
VkPipelineLayout freq_get_pipeline_layout(FreqDescriptorSystem* sys, VkPushConstantRange* push_ranges, uint32_t push_range_count);


/* =============================================================================
 * API - RENDERING HELPERS
 * =============================================================================
 */

// Bind all three sets for a draw call
// This is optimized: Set 0 cached, Set 1 per material, Set 2 with dynamic offset
void freq_bind_for_draw(FreqDescriptorSystem* sys,
                        VkCommandBuffer cmd,
                        VkPipelineBindPoint bind_point,
                        VkPipelineLayout layout,
                        uint32_t material_id,
                        uint32_t draw_offset);

// Bind only Set 0 (global) - call once at start of render pass
void freq_bind_global(FreqDescriptorSystem* sys,
                      VkCommandBuffer cmd,
                      VkPipelineBindPoint bind_point,
                      VkPipelineLayout layout);

// Bind Set 1 (material) and Set 2 (per-draw) together
// Use this when iterating through sorted draw calls
void freq_bind_material_draw(FreqDescriptorSystem* sys,
                             VkCommandBuffer cmd,
                             VkPipelineBindPoint bind_point,
                             VkPipelineLayout layout,
                             uint32_t material_id,
                             uint32_t draw_offset);


/* =============================================================================
 * USAGE EXAMPLE (in comments for reference)
 * =============================================================================
 *
 * // === INITIALIZATION ===
 * FreqDescriptorSystem freq;
 * freq_init(&freq, device, &resource_allocator);
 * freq_create_defaults(&freq, init_cmd);  // Upload 1x1 default textures
 *
 * // === CREATE MATERIALS ===
 * FreqMaterialParams mat_params = {
 *     .base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f},
 *     .metallic_factor = 0.0f,
 *     .roughness_factor = 0.5f,
 * };
 * uint32_t mat_id = freq_material_create(&freq, &mat_params);
 * freq_material_set_textures(&freq, mat_id, albedo_view, normal_view, NULL, NULL, NULL, sampler);
 * freq_material_flush(&freq);
 *
 * // === GET PIPELINE LAYOUT ===
 * VkDescriptorSetLayout layouts[3];
 * freq_get_layouts(&freq, layouts);
 * // Use layouts when creating your graphics pipeline
 *
 * // === RENDER LOOP ===
 * freq_begin_frame(&freq);
 *
 * // Update per-frame data
 * FreqGlobalData global = { ... camera matrices, time ... };
 * FreqLightData lights = { ... sun direction, colors ... };
 * freq_update_global(&freq, &global, &lights);
 *
 * // Bind global set once
 * freq_bind_global(&freq, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout);
 *
 * // For each object (sorted by material)
 * for (int i = 0; i < draw_count; i++)
 * {
 *     DrawCall* dc = &draw_calls[i];
 *     
 *     // Allocate per-draw data
 *     FreqDrawData* draw;
 *     uint32_t offset = freq_alloc_draw(&freq, &draw);
 *     draw->model = dc->transform;
 *     // ... fill rest of draw data
 *     
 *     // Bind material (Set 1) and draw data (Set 2 with offset)
 *     freq_bind_material_draw(&freq, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
 *                             pipeline_layout, dc->material_id, offset);
 *     
 *     // Draw!
 *     vkCmdDrawIndexed(cmd, dc->index_count, 1, dc->first_index, dc->vertex_offset, 0);
 * }
 *
 * =============================================================================
 */

#endif  // VK_DESCRIPTOR_FREQ_H_
