/*
 * =============================================================================
 * BINDLESS DESCRIPTOR MANAGEMENT
 * =============================================================================
 *
 * DESIGN PHILOSOPHY:
 * ------------------
 * The bindless approach eliminates most per-draw descriptor binding overhead
 * by keeping ALL resources permanently bound in massive descriptor arrays.
 * The GPU selects which resource to use via indices passed in draw data.
 *
 * This is the foundation for GPU-driven rendering where:
 * - The GPU decides what to draw (via indirect commands)
 * - All data is in large storage buffers
 * - Draw ID (gl_DrawID) or instance ID indexes into draw data
 * - Per-draw data contains indices into material, transform, and vertex buffers
 *
 * VULKAN 1.3 FEATURES USED:
 * -------------------------
 * - VK_EXT_descriptor_indexing (core in 1.2/1.3)
 *   - VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
 *   - VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
 *   - VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
 *   - descriptorBindingSampledImageUpdateAfterBind
 *   - descriptorBindingStorageBufferUpdateAfterBind
 *   - runtimeDescriptorArray
 *   - shaderSampledImageArrayNonUniformIndexing
 *
 * - VK_KHR_buffer_device_address (core in 1.2/1.3)
 *   - Allows passing GPU pointers in push constants
 *   - Manual vertex fetching via buffer address
 *
 * - VK_KHR_draw_indirect_count (core in 1.2)
 *   - vkCmdDrawIndirectCount
 *   - GPU writes draw count, no CPU readback needed
 *
 * SET ORGANIZATION:
 * -----------------
 *
 * SET 0: BINDLESS RESOURCES (persistent, update-after-bind)
 * ---------------------------------------------------------
 * Binding 0: Sampled images array (textures)     [4096 descriptors]
 * Binding 1: Storage images array (UAVs)         [1024 descriptors]
 * Binding 2: Samplers array                      [32 descriptors]
 * Binding 3: Storage buffers (SSBO) array        [256 descriptors]
 *
 * All textures, samplers, and buffers registered at load time.
 * Never rebound during rendering.
 *
 * SET 1: GLOBAL DATA (per-frame, small UBO)
 * -----------------------------------------
 * Binding 0: GlobalData UBO (camera, time, etc.)
 * Binding 1: DrawDataBuffer SSBO (pointer via BDA preferred)
 *
 * Bound once per frame with per-frame buffer offset.
 *
 * BUFFER LAYOUTS:
 * ---------------
 *
 * MaterialBuffer (SSBO, registered in Set 0 Binding 3):
 * struct Material {
 *     uint albedo_idx;
 *     uint normal_idx;
 *     uint metallic_roughness_idx;
 *     uint occlusion_idx;
 *     uint emissive_idx;
 *     uint sampler_idx;
 *     vec4 base_color_factor;
 *     float metallic_factor;
 *     float roughness_factor;
 *     // ... other PBR params
 * };
 *
 * TransformBuffer (SSBO):
 * struct Transform {
 *     mat4 model;
 *     mat4 normal_matrix;  // or mat3x4
 * };
 *
 * DrawDataBuffer (SSBO, indexed by gl_DrawID):
 * struct DrawData {
 *     uint material_idx;
 *     uint transform_idx;
 *     uint vertex_buffer_idx;    // for manual vertex fetch
 *     uint index_buffer_idx;     // optional
 *     uint first_vertex;
 *     uint vertex_count;
 *     // ... mesh LOD, flags, etc.
 * };
 *
 * GPU-DRIVEN RENDERING FLOW:
 * --------------------------
 * 1. CPU uploads all meshes to vertex storage buffer
 * 2. CPU uploads all materials to material storage buffer
 * 3. CPU uploads all transforms to transform storage buffer
 * 4. CPU or GPU fills DrawDataBuffer with draw parameters
 * 5. CPU or GPU fills IndirectDrawBuffer with VkDrawIndirectCommand
 * 6. CPU or GPU fills DrawCountBuffer with draw count
 * 7. Render:
 *    - Bind Set 0 (all resources, never changes)
 *    - Bind Set 1 (per-frame globals + draw data pointer)
 *    - vkCmdDrawIndirectCount(cmd, indirect_buf, count_buf, max_draws)
 *
 * SHADER EXAMPLE:
 * ---------------
 * // GLSL (with GL_EXT_buffer_reference, GL_EXT_nonuniform_qualifier)
 *
 * layout(set = 0, binding = 0) uniform sampler2D textures[];
 * layout(set = 0, binding = 2) uniform sampler samplers[];
 * layout(set = 0, binding = 3) buffer MaterialBuffer { Material materials[]; };
 *
 * layout(set = 1, binding = 0) uniform GlobalData { ... };
 * layout(set = 1, binding = 1) buffer DrawDataBuffer { DrawData draws[]; };
 *
 * // In vertex/fragment shader:
 * DrawData draw = draws[gl_DrawID];
 * Material mat = materials[draw.material_idx];
 * vec4 albedo = texture(textures[nonuniformEXT(mat.albedo_idx)], uv);
 *
 * MANUAL VERTEX FETCHING:
 * -----------------------
 * Instead of using VkVertexInputState, read vertices directly:
 *
 * layout(buffer_reference) buffer VertexBuffer { Vertex vertices[]; };
 * layout(push_constant) uniform PushConstants { VertexBuffer vertex_ptr; };
 *
 * // In vertex shader:
 * Vertex v = vertex_ptr.vertices[gl_VertexIndex];
 * gl_Position = viewproj * model * vec4(v.position, 1.0);
 *
 * PERFORMANCE CHARACTERISTICS:
 * ----------------------------
 * Pros:
 * - Near-zero CPU overhead per draw call
 * - Perfect for GPU-driven rendering
 * - Minimal descriptor binding (1-2 binds per frame)
 * - Easy to batch everything into few draw calls
 * - Scales to millions of objects without CPU bottleneck
 *
 * Cons:
 * - Requires Vulkan 1.2+ features
 * - Non-uniform resource access can hurt cache on some GPUs
 * - More complex shader code
 * - Need to manage resource indices carefully
 * - Debugging is harder (no validation layers for array access)
 *
 * =============================================================================
 */

#ifndef VK_DESCRIPTOR_BINDLESS_H_
#define VK_DESCRIPTOR_BINDLESS_H_

#include "vk_defaults.h"
#include "vk_resources.h"

// Resource array limits
#define BINDLESS_MAX_TEXTURES       4096
#define BINDLESS_MAX_STORAGE_IMAGES 1024
#define BINDLESS_MAX_SAMPLERS       32
#define BINDLESS_MAX_BUFFERS        256

// Per-frame limits
#define BINDLESS_MAX_FRAMES_IN_FLIGHT 3
#define BINDLESS_MAX_DRAWS_PER_FRAME  65536

// Invalid index sentinel
#define BINDLESS_INVALID_INDEX 0xFFFFFFFF


/* =============================================================================
 * GPU DATA STRUCTURES (match your GLSL)
 * =============================================================================
 */

// Global per-frame data (Set 1, Binding 0)
typedef struct BindlessGlobalData
{
    float view[16];         // 64 bytes
    float projection[16];   // 64 bytes
    float viewproj[16];     // 64 bytes
    float inv_view[16];     // 64 bytes
    float inv_projection[16]; // 64 bytes
    float inv_viewproj[16];   // 64 bytes
    float camera_pos[4];    // 16 bytes (xyz + fov)
    float camera_dir[4];    // 16 bytes (xyz + aspect)
    float time;             // 4 bytes
    float delta_time;       // 4 bytes
    uint32_t frame_count;   // 4 bytes
    uint32_t pad;           // 4 bytes
} BindlessGlobalData;  // 416 bytes

// Material data (stored in MaterialBuffer SSBO)
typedef struct BindlessMaterial
{
    // Texture indices into the bindless texture array
    uint32_t albedo_idx;
    uint32_t normal_idx;
    uint32_t metallic_roughness_idx;
    uint32_t occlusion_idx;
    uint32_t emissive_idx;
    uint32_t sampler_idx;
    uint32_t pad[2];
    
    // PBR parameters
    float base_color_factor[4];   // 16 bytes
    float metallic_factor;        // 4 bytes
    float roughness_factor;       // 4 bytes
    float normal_scale;           // 4 bytes
    float occlusion_strength;     // 4 bytes
    float emissive_factor[4];     // 16 bytes
    float alpha_cutoff;           // 4 bytes
    uint32_t flags;               // 4 bytes
    float pad2[2];                // 8 bytes
} BindlessMaterial;  // 96 bytes

// Transform data (stored in TransformBuffer SSBO)
typedef struct BindlessTransform
{
    float model[16];         // 64 bytes
    float normal[12];        // 48 bytes (mat3x4, row-major for GPU)
    float pad[4];            // 16 bytes alignment
} BindlessTransform;  // 128 bytes

// Per-draw data (indexed by gl_DrawID in DrawDataBuffer SSBO)
typedef struct BindlessDrawData
{
    uint32_t material_idx;       // Index into MaterialBuffer
    uint32_t transform_idx;      // Index into TransformBuffer
    uint32_t vertex_offset;      // Offset into vertex buffer (for multi-mesh)
    uint32_t index_offset;       // Offset into index buffer
    uint32_t first_index;        // For indexed drawing
    uint32_t index_count;        // For indexed drawing
    uint32_t instance_count;     // Usually 1
    int32_t  vertex_bias;        // Vertex offset for indexed
    // Extra data for culling, LOD, etc.
    float    bounding_sphere[4]; // xyz = center, w = radius
    uint32_t flags;              // Visibility, LOD flags
    uint32_t lod_level;          // Current LOD
    float    pad[2];             // Align to 64 bytes
} BindlessDrawData;  // 64 bytes

// Indirect draw command (matches VkDrawIndexedIndirectCommand)
typedef struct BindlessIndirectCommand
{
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;  // Can be used to pass draw ID
} BindlessIndirectCommand;


/* =============================================================================
 * VERTEX FORMATS (for manual vertex fetching)
 * =============================================================================
 */

// Standard vertex format for manual fetching
typedef struct BindlessVertex
{
    float position[3];   // 12 bytes
    float normal[3];     // 12 bytes
    float tangent[4];    // 16 bytes (xyz + handedness)
    float uv0[2];        // 8 bytes
    float uv1[2];        // 8 bytes (lightmap UVs)
} BindlessVertex;  // 56 bytes (pad to 64 for alignment if needed)

// Skinned vertex (for skeletal animation)
typedef struct BindlessSkinnedVertex
{
    float position[3];
    float normal[3];
    float tangent[4];
    float uv0[2];
    uint8_t joint_indices[4];  // Bone indices
    uint8_t joint_weights[4];  // Bone weights (normalized to 255)
} BindlessSkinnedVertex;


/* =============================================================================
 * RESOURCE HANDLES
 * =============================================================================
 */

// Handle returned when registering a texture
typedef struct BindlessTextureHandle
{
    uint32_t index;       // Index in bindless array
    VkImageView view;     // Original view
    VkFormat format;      // For reference
} BindlessTextureHandle;

// Handle returned when registering a sampler
typedef struct BindlessSamplerHandle
{
    uint32_t index;
    VkSampler sampler;
} BindlessSamplerHandle;

// Handle returned when registering a buffer
typedef struct BindlessBufferHandle
{
    uint32_t index;
    VkBuffer buffer;
    VkDeviceAddress address;  // For BDA usage
    VkDeviceSize size;
} BindlessBufferHandle;


/* =============================================================================
 * DESCRIPTOR LAYOUTS
 * =============================================================================
 */

typedef struct BindlessSet0Layout
{
    VkDescriptorSetLayout layout;
    
    // Binding info:
    // 0: sampler2D textures[]          - PARTIALLY_BOUND, UPDATE_AFTER_BIND
    // 1: image2D   storage_images[]    - PARTIALLY_BOUND, UPDATE_AFTER_BIND
    // 2: sampler   samplers[]          - PARTIALLY_BOUND, UPDATE_AFTER_BIND
    // 3: buffer    storage_buffers[]   - PARTIALLY_BOUND, UPDATE_AFTER_BIND
} BindlessSet0Layout;

typedef struct BindlessSet1Layout
{
    VkDescriptorSetLayout layout;
    
    // Binding info:
    // 0: uniform GlobalData           - per-frame UBO
    // 1: buffer  DrawDataBuffer       - per-frame draw data
    // 2: buffer  MaterialBuffer       - all materials
    // 3: buffer  TransformBuffer      - all transforms
} BindlessSet1Layout;


/* =============================================================================
 * PER-FRAME RESOURCES
 * =============================================================================
 */

typedef struct BindlessFrameResources
{
    // Per-frame UBO for global data
    Buffer global_buffer;
    
    // Per-frame draw data
    Buffer draw_data_buffer;
    uint32_t draw_count;
    uint32_t draw_buffer_capacity;
    
    // Per-frame indirect commands
    Buffer indirect_buffer;
    Buffer draw_count_buffer;  // For vkCmdDrawIndirectCount
    
    // Set 1 descriptor for this frame
    VkDescriptorSet set1;
    
} BindlessFrameResources;


/* =============================================================================
 * MAIN SYSTEM
 * =============================================================================
 */

typedef struct BindlessDescriptorSystem
{
    VkDevice device;
    ResourceAllocator* allocator;
    
    // Feature support flags
    bool supports_descriptor_indexing;
    bool supports_buffer_device_address;
    bool supports_draw_indirect_count;
    
    // Descriptor layouts
    BindlessSet0Layout set0_layout;
    BindlessSet1Layout set1_layout;
    
    // Bindless descriptor pool (UPDATE_AFTER_BIND)
    VkDescriptorPool bindless_pool;
    
    // Per-frame pool (standard)
    VkDescriptorPool frame_pool;
    
    // Set 0 - THE bindless set (never rebound after creation)
    VkDescriptorSet set0;
    
    // Per-frame resources
    BindlessFrameResources frames[BINDLESS_MAX_FRAMES_IN_FLIGHT];
    uint32_t current_frame;
    
    // Resource registries
    uint32_t next_texture_idx;
    uint32_t next_storage_image_idx;
    uint32_t next_sampler_idx;
    uint32_t next_buffer_idx;
    
    // Material storage
    Buffer material_buffer;
    BindlessMaterial* materials;  // CPU-side array (stb_ds)
    uint32_t material_count;
    bool materials_dirty;
    
    // Transform storage
    Buffer transform_buffer;
    BindlessTransform* transforms;  // CPU-side array (stb_ds)
    uint32_t transform_count;
    bool transforms_dirty;
    
    // Vertex storage (for manual vertex fetching)
    Buffer vertex_buffer;
    VkDeviceAddress vertex_buffer_address;
    size_t vertex_buffer_offset;
    size_t vertex_buffer_capacity;
    
    // Index buffer
    Buffer index_buffer;
    VkDeviceAddress index_buffer_address;
    size_t index_buffer_offset;
    size_t index_buffer_capacity;
    
    // Default resources
    BindlessTextureHandle default_white;
    BindlessTextureHandle default_black;
    BindlessTextureHandle default_normal;
    BindlessSamplerHandle default_sampler_linear;
    BindlessSamplerHandle default_sampler_nearest;
    
    // Pipeline layout with push constants for BDA
    VkPipelineLayout pipeline_layout;
    
} BindlessDescriptorSystem;


/* =============================================================================
 * API - INITIALIZATION
 * =============================================================================
 */

// Check if device supports bindless features
bool bindless_check_support(VkPhysicalDevice physical_device);

// Initialize the bindless system
void bindless_init(BindlessDescriptorSystem* sys, 
                   VkDevice device, 
                   VkPhysicalDevice physical_device,
                   ResourceAllocator* allocator);

// Destroy the system
void bindless_destroy(BindlessDescriptorSystem* sys);

// Create default textures and samplers
void bindless_create_defaults(BindlessDescriptorSystem* sys, VkCommandBuffer cmd);


/* =============================================================================
 * API - RESOURCE REGISTRATION
 * =============================================================================
 */

// Register a texture (can be called anytime, even during rendering)
BindlessTextureHandle bindless_register_texture(BindlessDescriptorSystem* sys,
                                                 VkImageView view,
                                                 VkImageLayout layout,
                                                 VkFormat format);

// Unregister a texture (frees the index for reuse)
void bindless_unregister_texture(BindlessDescriptorSystem* sys, BindlessTextureHandle handle);

// Register a storage image (for compute shaders)
BindlessTextureHandle bindless_register_storage_image(BindlessDescriptorSystem* sys,
                                                       VkImageView view,
                                                       VkFormat format);

// Register a sampler
BindlessSamplerHandle bindless_register_sampler(BindlessDescriptorSystem* sys, VkSampler sampler);

// Register a storage buffer
BindlessBufferHandle bindless_register_buffer(BindlessDescriptorSystem* sys,
                                               VkBuffer buffer,
                                               VkDeviceSize offset,
                                               VkDeviceSize range);


/* =============================================================================
 * API - MATERIALS
 * =============================================================================
 */

// Create a material, returns index
uint32_t bindless_material_create(BindlessDescriptorSystem* sys, const BindlessMaterial* material);

// Update a material (marks dirty, flushed on next frame)
void bindless_material_update(BindlessDescriptorSystem* sys, uint32_t idx, const BindlessMaterial* material);

// Get material pointer for modification (marks dirty automatically)
BindlessMaterial* bindless_material_get(BindlessDescriptorSystem* sys, uint32_t idx);


/* =============================================================================
 * API - TRANSFORMS
 * =============================================================================
 */

// Allocate transform slot, returns index
uint32_t bindless_transform_alloc(BindlessDescriptorSystem* sys);

// Update a transform
void bindless_transform_update(BindlessDescriptorSystem* sys, uint32_t idx, const BindlessTransform* transform);

// Get transform pointer for modification
BindlessTransform* bindless_transform_get(BindlessDescriptorSystem* sys, uint32_t idx);


/* =============================================================================
 * API - MESH DATA (for manual vertex fetching)
 * =============================================================================
 */

// Upload vertices to the global vertex buffer, returns offset
uint32_t bindless_upload_vertices(BindlessDescriptorSystem* sys,
                                   const void* vertices,
                                   uint32_t vertex_count,
                                   uint32_t vertex_stride);

// Upload indices to the global index buffer, returns offset
uint32_t bindless_upload_indices(BindlessDescriptorSystem* sys,
                                  const void* indices,
                                  uint32_t index_count,
                                  VkIndexType index_type);


/* =============================================================================
 * API - PER-FRAME OPERATIONS
 * =============================================================================
 */

// Begin new frame
void bindless_begin_frame(BindlessDescriptorSystem* sys);

// Update global data
void bindless_update_global(BindlessDescriptorSystem* sys, const BindlessGlobalData* global);

// Flush materials/transforms to GPU if dirty
void bindless_flush_resources(BindlessDescriptorSystem* sys, VkCommandBuffer cmd);

// Allocate a draw data slot for this frame, returns index
uint32_t bindless_alloc_draw(BindlessDescriptorSystem* sys, BindlessDrawData** out_data);

// Allocate an indirect command slot, returns index
uint32_t bindless_alloc_indirect(BindlessDescriptorSystem* sys, BindlessIndirectCommand** out_cmd);


/* =============================================================================
 * API - RENDERING
 * =============================================================================
 */

// Bind all descriptor sets (call once per render pass)
void bindless_bind(BindlessDescriptorSystem* sys,
                   VkCommandBuffer cmd,
                   VkPipelineBindPoint bind_point,
                   VkPipelineLayout layout);

// Get the vertex buffer address for push constants
VkDeviceAddress bindless_get_vertex_buffer_address(BindlessDescriptorSystem* sys);

// Get the index buffer address for push constants
VkDeviceAddress bindless_get_index_buffer_address(BindlessDescriptorSystem* sys);

// Draw all submitted draws via indirect
void bindless_draw_indirect(BindlessDescriptorSystem* sys, VkCommandBuffer cmd);

// Draw with explicit count (if GPU doesn't write count)
void bindless_draw_indirect_count(BindlessDescriptorSystem* sys, 
                                  VkCommandBuffer cmd,
                                  uint32_t max_draws);

// Get descriptor set layouts for pipeline creation
void bindless_get_layouts(BindlessDescriptorSystem* sys, VkDescriptorSetLayout* out_layouts);

// Get or create pipeline layout
VkPipelineLayout bindless_get_pipeline_layout(BindlessDescriptorSystem* sys);


/* =============================================================================
 * PUSH CONSTANT LAYOUTS (example for vertex buffer address)
 * =============================================================================
 */

// Example push constant for passing buffer addresses
typedef struct BindlessPushConstants
{
    VkDeviceAddress vertex_buffer;
    VkDeviceAddress index_buffer;
    uint32_t draw_offset;  // Base offset into DrawDataBuffer
    uint32_t flags;        // Rendering flags
} BindlessPushConstants;


/* =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 *
 * // === INITIALIZATION ===
 * if (!bindless_check_support(physical_device)) {
 *     // Fall back to frequency-based system
 *     return;
 * }
 *
 * BindlessDescriptorSystem bindless;
 * bindless_init(&bindless, device, physical_device, &allocator);
 * bindless_create_defaults(&bindless, init_cmd);
 *
 * // === LOAD TEXTURES ===
 * BindlessTextureHandle albedo = bindless_register_texture(&bindless, 
 *     albedo_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_FORMAT_R8G8B8A8_SRGB);
 * BindlessTextureHandle normal = bindless_register_texture(&bindless,
 *     normal_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_FORMAT_R8G8B8A8_UNORM);
 *
 * // === CREATE MATERIALS ===
 * BindlessMaterial mat = {
 *     .albedo_idx = albedo.index,
 *     .normal_idx = normal.index,
 *     .sampler_idx = bindless.default_sampler_linear.index,
 *     .base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f},
 *     .metallic_factor = 0.0f,
 *     .roughness_factor = 0.5f,
 * };
 * uint32_t mat_id = bindless_material_create(&bindless, &mat);
 *
 * // === UPLOAD MESH ===
 * uint32_t vertex_offset = bindless_upload_vertices(&bindless, 
 *     mesh.vertices, mesh.vertex_count, sizeof(BindlessVertex));
 * uint32_t index_offset = bindless_upload_indices(&bindless,
 *     mesh.indices, mesh.index_count, VK_INDEX_TYPE_UINT32);
 *
 * // === CREATE TRANSFORM ===
 * uint32_t transform_id = bindless_transform_alloc(&bindless);
 * BindlessTransform* t = bindless_transform_get(&bindless, transform_id);
 * // ... fill model matrix
 *
 * // === RENDER LOOP ===
 * bindless_begin_frame(&bindless);
 *
 * // Update camera
 * BindlessGlobalData global = { ... };
 * bindless_update_global(&bindless, &global);
 *
 * // Queue draws
 * for (int i = 0; i < object_count; i++) {
 *     BindlessDrawData* draw;
 *     uint32_t draw_idx = bindless_alloc_draw(&bindless, &draw);
 *     draw->material_idx = objects[i].material;
 *     draw->transform_idx = objects[i].transform;
 *     draw->first_index = objects[i].first_index;
 *     draw->index_count = objects[i].index_count;
 *     
 *     BindlessIndirectCommand* cmd;
 *     bindless_alloc_indirect(&bindless, &cmd);
 *     cmd->indexCount = draw->index_count;
 *     cmd->instanceCount = 1;
 *     cmd->firstIndex = draw->first_index;
 *     cmd->vertexOffset = 0;
 *     cmd->firstInstance = draw_idx;  // Pass draw ID via firstInstance
 * }
 *
 * // Flush to GPU
 * bindless_flush_resources(&bindless, cmd);
 *
 * // Render
 * vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
 * bindless_bind(&bindless, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout);
 *
 * // Push vertex buffer address
 * BindlessPushConstants pc = {
 *     .vertex_buffer = bindless_get_vertex_buffer_address(&bindless),
 * };
 * vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
 *
 * // Single indirect draw for ALL objects
 * bindless_draw_indirect(&bindless, cmd);
 *
 * =============================================================================
 */

#endif  // VK_DESCRIPTOR_BINDLESS_H_
