# Descriptor System Design Document

## Overview

This document describes two high-performance descriptor management strategies for Vulkan 1.3:

1. **Frequency-Based Descriptors** (`vk_descriptor_freq.h/.c`)
2. **Bindless Descriptors** (`vk_descriptor_bindless.h/.c`)

Both systems are designed for maximum performance while serving different use cases.

---

## When to Use Each System

### Frequency-Based System
Best for:
- Traditional forward/deferred rendering
- Moderate object counts (< 10K draws per frame)
- When you need per-material texture variations
- Cross-platform compatibility (works on all Vulkan 1.0+ devices)
- Easier to understand and debug

### Bindless System
Best for:
- GPU-driven rendering pipelines
- Very high object counts (100K+ draws per frame)
- Open-world games with streaming
- When all resources should be always-available
- Vulkan 1.2+ only (requires descriptor indexing)

---

## Frequency-Based System Design

### Core Concept
Group descriptors by how often they change:

```
Set 0: Global (once per frame)
├── Camera matrices
├── Time uniforms
├── Lighting data
└── Environment maps

Set 1: Material (per-material batch)
├── Albedo texture
├── Normal texture
├── PBR textures
└── Material parameters UBO

Set 2: Per-Draw (every draw call)
└── Dynamic UBO with transforms
```

### Binding Strategy

```c
// Start of frame
freq_begin_frame(&freq);
freq_update_global(&freq, &camera_data, &light_data);

// Bind global once
freq_bind_global(&freq, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout);

// For each draw (sorted by material)
for (int i = 0; i < draw_count; i++) {
    // Allocate per-draw data
    FreqDrawData* draw;
    uint32_t offset = freq_alloc_draw(&freq, &draw);
    memcpy(draw->model, &transform, sizeof(mat4));
    
    // Bind material + per-draw with dynamic offset
    freq_bind_material_draw(&freq, cmd, layout, material_id, offset);
    
    vkCmdDrawIndexed(cmd, ...);
}
```

### Performance Tips

1. **Sort by Material**: Minimize Set 1 rebinds
2. **Use Dynamic Offsets**: Set 2 never needs reallocation
3. **Ring Buffer Global Data**: Triple-buffer for frames in flight
4. **Cache Material Sets**: Create once, reuse forever

### Memory Layout

```
Per-Frame Memory:
┌─────────────────────────────────────────┐
│ Global UBO (256 bytes aligned)          │
├─────────────────────────────────────────┤
│ Light UBO (256 bytes aligned)           │
├─────────────────────────────────────────┤
│ Per-Draw Dynamic UBO                    │
│ [Draw 0: 256 bytes]                     │
│ [Draw 1: 256 bytes]                     │
│ [Draw 2: 256 bytes]                     │
│ ...                                     │
│ [Draw N: 256 bytes]                     │
└─────────────────────────────────────────┘

Material Memory (persistent):
┌─────────────────────────────────────────┐
│ Material 0 Params UBO                   │
│ Material 0 Descriptor Set               │
├─────────────────────────────────────────┤
│ Material 1 Params UBO                   │
│ Material 1 Descriptor Set               │
├─────────────────────────────────────────┤
│ ...                                     │
└─────────────────────────────────────────┘
```

---

## Bindless System Design

### Core Concept
Keep ALL resources permanently bound. Use indices to select resources.

```
Set 0: Bindless Resources (NEVER rebound)
├── Binding 0: sampler2D textures[4096]
├── Binding 1: image2D storage_images[1024]
├── Binding 2: sampler samplers[32]
└── Binding 3: buffer storage_buffers[256]

Set 1: Per-Frame Data (once per frame)
├── Binding 0: Global UBO
├── Binding 1: DrawData SSBO
├── Binding 2: Material SSBO
└── Binding 3: Transform SSBO
```

### GPU Data Structures

```glsl
// Material - stores texture indices, not textures
struct Material {
    uint albedo_idx;
    uint normal_idx;
    uint metallic_roughness_idx;
    uint occlusion_idx;
    uint emissive_idx;
    uint sampler_idx;
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    // ...
};

// Per-draw data - indexed by gl_DrawID
struct DrawData {
    uint material_idx;
    uint transform_idx;
    uint vertex_offset;
    uint index_offset;
    // ...
};

// Shader usage
DrawData draw = draws[gl_DrawID];
Material mat = materials[draw.material_idx];
mat4 model = transforms[draw.transform_idx].model;

// Non-uniform texture sampling
vec4 albedo = texture(
    sampler2D(textures[nonuniformEXT(mat.albedo_idx)], 
              samplers[mat.sampler_idx]),
    uv
);
```

### GPU-Driven Rendering Flow

```c
// === UPLOAD PHASE ===
// Upload all vertices to single buffer
uint32_t vertex_offset = bindless_upload_vertices(&bindless, 
    mesh.vertices, mesh.vertex_count, sizeof(Vertex));

// Register all textures (returns indices)
BindlessTextureHandle albedo = bindless_register_texture(&bindless,
    albedo_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, format);

// Create materials with texture indices
BindlessMaterial mat = {
    .albedo_idx = albedo.index,
    .sampler_idx = bindless.default_sampler_linear.index,
    // ...
};
uint32_t mat_id = bindless_material_create(&bindless, &mat);

// === RENDER PHASE ===
bindless_begin_frame(&bindless);
bindless_update_global(&bindless, &global_data);

// Queue ALL draws (CPU or GPU can do this)
for (int i = 0; i < object_count; i++) {
    BindlessDrawData* draw;
    bindless_alloc_draw(&bindless, &draw);
    draw->material_idx = objects[i].material;
    draw->transform_idx = objects[i].transform;
    // ...
    
    BindlessIndirectCommand* cmd_data;
    bindless_alloc_indirect(&bindless, &cmd_data);
    cmd_data->indexCount = objects[i].index_count;
    cmd_data->instanceCount = 1;
    cmd_data->firstInstance = i;  // Pass draw ID
    // ...
}

// Flush to GPU
bindless_flush_resources(&bindless, cmd);

// === DRAW PHASE ===
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
bindless_bind(&bindless, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout);

// Push vertex buffer address
BindlessPushConstants pc = {
    .vertex_buffer = bindless_get_vertex_buffer_address(&bindless),
};
vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

// SINGLE DRAW CALL for all objects!
bindless_draw_indirect(&bindless, cmd);
```

### Required Vulkan Features

```c
// Physical device features needed:
VkPhysicalDeviceDescriptorIndexingFeatures indexing = {
    .descriptorBindingPartiallyBound = VK_TRUE,
    .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
    .runtimeDescriptorArray = VK_TRUE,
    .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
};

VkPhysicalDeviceBufferDeviceAddressFeatures bda = {
    .bufferDeviceAddress = VK_TRUE,
};

// Required extensions (core in Vulkan 1.2+):
// VK_EXT_descriptor_indexing
// VK_KHR_buffer_device_address
// VK_KHR_draw_indirect_count
```

### Memory Layout

```
Persistent Buffers:
┌─────────────────────────────────────────────────────┐
│ Material SSBO (1024 materials × 96 bytes)           │
├─────────────────────────────────────────────────────┤
│ Transform SSBO (16K transforms × 128 bytes)         │
├─────────────────────────────────────────────────────┤
│ Vertex Storage Buffer (64 MB)                       │
├─────────────────────────────────────────────────────┤
│ Index Storage Buffer (32 MB)                        │
└─────────────────────────────────────────────────────┘

Per-Frame Buffers (triple-buffered):
┌─────────────────────────────────────────────────────┐
│ Frame N:                                            │
│   Global UBO (416 bytes)                            │
│   DrawData SSBO (64K draws × 64 bytes = 4 MB)       │
│   Indirect Commands (64K × 20 bytes = 1.3 MB)       │
│   Draw Count Buffer (4 bytes)                       │
└─────────────────────────────────────────────────────┘
```

---

## Manual Vertex Fetching

Both systems can use manual vertex fetching for maximum flexibility:

### GLSL Shader

```glsl
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// Define vertex buffer as buffer reference
layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

// Push constant with buffer address
layout(push_constant) uniform PushConstants {
    VertexBuffer vertex_ptr;
    uint draw_offset;
};

// Vertex format
struct Vertex {
    vec3 position;
    vec3 normal;
    vec4 tangent;
    vec2 uv0;
    vec2 uv1;
};

void main() {
    // Fetch vertex manually
    Vertex v = vertex_ptr.vertices[gl_VertexIndex];
    
    // Use draw data
    DrawData draw = draws[gl_DrawID + draw_offset];
    mat4 model = transforms[draw.transform_idx].model;
    
    gl_Position = global.viewproj * model * vec4(v.position, 1.0);
    // ...
}
```

### Benefits
- No VkVertexInputState needed
- Dynamic vertex formats
- Multi-mesh batching in single draw
- Compute shader vertex processing

---

## Performance Comparison

| Metric | Frequency-Based | Bindless |
|--------|-----------------|----------|
| CPU overhead per draw | Low | Near-zero |
| Descriptor binding calls | 1-3 per draw batch | 1 per frame |
| Memory overhead | Low | Higher (large arrays) |
| GPU cache efficiency | High | Medium (non-uniform access) |
| Implementation complexity | Low | High |
| Debugging ease | Easy | Hard |
| Min Vulkan version | 1.0 | 1.2 |
| Max practical draws | ~10K | Unlimited |
| GPU-driven rendering | Limited | Full support |

---

## Choosing Between Systems

### Use Frequency-Based When:
- Building a traditional renderer
- Need broad hardware compatibility
- Draw counts are moderate
- Debugging and profiling are priorities
- Material system is well-defined

### Use Bindless When:
- Building GPU-driven renderer
- Need 100K+ draws per frame
- Implementing GPU culling
- All content loads at startup
- Targeting modern GPUs only

### Hybrid Approach
You can use both systems:
- Frequency-based for UI, particles, debug rendering
- Bindless for main scene geometry

---

## Example Shader Headers

### Frequency-Based (freq_common.glsl)

```glsl
// Set 0: Global
layout(set = 0, binding = 0) uniform GlobalData {
    mat4 view;
    mat4 projection;
    mat4 viewproj;
    vec4 camera_pos;
    float time;
    float delta_time;
    uint frame_count;
};

layout(set = 0, binding = 1) uniform LightData {
    vec4 sun_direction;
    vec4 sun_color;
    vec4 ambient_color;
    uint light_count;
};

// Set 1: Material
layout(set = 1, binding = 0) uniform MaterialParams {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    vec4 emissive_factor;
    uint flags;
    float alpha_cutoff;
};

layout(set = 1, binding = 1) uniform sampler2D albedo_tex;
layout(set = 1, binding = 2) uniform sampler2D normal_tex;
layout(set = 1, binding = 3) uniform sampler2D metallic_roughness_tex;
layout(set = 1, binding = 4) uniform sampler2D occlusion_tex;
layout(set = 1, binding = 5) uniform sampler2D emissive_tex;

// Set 2: Per-Draw
layout(set = 2, binding = 0) uniform DrawData {
    mat4 model;
    mat4 normal_matrix;
    uint object_id;
    uint material_idx;
};
```

### Bindless (bindless_common.glsl)

```glsl
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

// Set 0: Bindless resources
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform image2D storage_images[];
layout(set = 0, binding = 2) uniform sampler samplers[];

struct Material {
    uint albedo_idx;
    uint normal_idx;
    uint metallic_roughness_idx;
    uint occlusion_idx;
    uint emissive_idx;
    uint sampler_idx;
    uint pad[2];
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    vec4 emissive_factor;
    float alpha_cutoff;
    uint flags;
    float pad2[2];
};

struct Transform {
    mat4 model;
    mat4 normal_matrix;
};

struct DrawData {
    uint material_idx;
    uint transform_idx;
    uint vertex_offset;
    uint index_offset;
    uint first_index;
    uint index_count;
    uint instance_count;
    int vertex_bias;
    vec4 bounding_sphere;
    uint flags;
    uint lod_level;
    float pad[2];
};

// Set 1: Per-frame
layout(set = 1, binding = 0) uniform GlobalData { ... };
layout(set = 1, binding = 1) readonly buffer DrawDataBuffer { DrawData draws[]; };
layout(set = 1, binding = 2) readonly buffer MaterialBuffer { Material materials[]; };
layout(set = 1, binding = 3) readonly buffer TransformBuffer { Transform transforms[]; };

// Helper macro for texture sampling
#define SAMPLE_TEX(tex_idx, sampler_idx, uv) \
    texture(sampler2D(textures[nonuniformEXT(tex_idx)], samplers[sampler_idx]), uv)
```

---

## Files Reference

| File | Purpose |
|------|---------|
| `vk_descriptor_freq.h` | Frequency-based system header with full documentation |
| `vk_descriptor_freq.c` | Frequency-based system implementation |
| `vk_descriptor_bindless.h` | Bindless system header with full documentation |
| `vk_descriptor_bindless.c` | Bindless system implementation |
| `vk_descriptor.h/.c` | Original simple descriptor allocator (still usable) |

---

## Future Improvements

1. **GPU Culling Integration**: Add compute pass for frustum/occlusion culling
2. **LOD Selection**: GPU-based LOD selection in bindless system
3. **Sparse Residency**: Virtual textures with sparse binding
4. **Async Resource Loading**: Background texture streaming
5. **Mesh Shaders**: Replace vertex input with mesh shaders
