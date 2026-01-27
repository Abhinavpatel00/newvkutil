# Granite Lighting System Integration Guide

This document details how to integrate Granite's advanced lighting features into the vkutil renderer.

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Light Types](#light-types)
3. [PBR Shading Model](#pbr-shading-model)
4. [Clustered Lighting](#clustered-lighting)
5. [Shadow Mapping](#shadow-mapping)
6. [Volumetric Effects](#volumetric-effects)
7. [Integration Steps](#integration-steps)
8. [Data Structures](#data-structures)
9. [Shader Integration](#shader-integration)

---

## Architecture Overview

Granite uses a **clustered forward+ renderer** with bindless descriptors for efficient light culling:

```
┌─────────────────────────────────────────────────────────────┐
│                    LIGHTING PIPELINE                        │
├─────────────────────────────────────────────────────────────┤
│  1. Light Culling (Compute)                                 │
│     - Bin lights into screen-space clusters                 │
│     - Generate bitmasks per cluster                         │
│                                                             │
│  2. Shadow Pass                                             │
│     - Directional: Cascaded Shadow Maps (CSM)               │
│     - Point: Cubemap shadows                                │
│     - Spot: 2D shadow atlas                                 │
│                                                             │
│  3. Lighting Pass (Forward/Deferred)                        │
│     - PBR BRDF evaluation                                   │
│     - Cluster-based light iteration                         │
│     - Volumetric effects                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## Light Types

### 1. Directional Light

**Purpose**: Sun/moon, global illumination source  
**Location**: [lighting_data.h](../Granite/assets/shaders/lights/lighting_data.h)

```c
// C-side structure (std140 aligned)
typedef struct DirectionalLight {
    vec3  color;      // RGB intensity
    float _pad0;
    vec3  direction;  // Normalized world-space direction (towards light)
    float _pad1;
} DirectionalLight;
```

**Shader Usage** ([directional.frag](../Granite/assets/shaders/lights/directional.frag)):
- Full-screen quad or deferred pass
- Cascaded shadow map sampling
- PBR BRDF evaluation

### 2. Point Light

**Purpose**: Omnidirectional local lights (lamps, torches, explosions)  
**Location**: [point.h](../Granite/assets/shaders/lights/point.h), [lights.hpp](../Granite/renderer/lights/lights.hpp)

```c
// GPU structure (matches PositionalLightInfo in clusterer_data.h)
typedef struct PointLightGpu {
    vec3     color;           // RGB intensity (pre-multiplied by scale²)
    uint32_t spot_scale_bias; // Unused for point lights
    vec3     position;        // World-space position
    uint32_t offset_radius;   // Packed: x=offset (for spot center), y=radius
    vec3     direction;       // Unused for point lights (can ignore)
    float    inv_radius;      // 1.0 / max_range
} PointLightGpu;
```

**Key formulas**:
```glsl
// Attenuation
float light_dist = max(0.1, length(world_pos - light_pos));
float static_falloff = 1.0 - smoothstep(0.9, 1.0, light_dist * inv_radius);
vec3 radiance = color * static_falloff / (light_dist * light_dist);
```

### 3. Spot Light

**Purpose**: Directional cone lights (flashlights, stage lights)  
**Location**: [spot.h](../Granite/assets/shaders/lights/spot.h)

```c
typedef struct SpotLightGpu {
    vec3     color;
    uint32_t spot_scale_bias; // Packed half2: scale, bias for cone falloff
    vec3     position;
    uint32_t offset_radius;   // Packed half2: center offset, bounding radius
    vec3     direction;       // Cone direction (normalized)
    float    inv_radius;
} SpotLightGpu;
```

**Cone falloff calculation**:
```glsl
// Unpack spot parameters
vec2 spot_scale_bias = unpackHalf2x16(light.spot_scale_bias);

// Compute cone angle
float cone_angle = dot(normalize(world_pos - light_pos), light.direction);
float cone_falloff = clamp(cone_angle * spot_scale_bias.x + spot_scale_bias.y, 0.0, 1.0);
cone_falloff *= cone_falloff; // Squared for softer edges
```

**Setting up spot parameters** (C-side):
```c
void spot_light_set_cone(SpotLight* light, float inner_cone_cos, float outer_cone_cos) {
    // inner_cone/outer_cone are cosines of half-angles
    float spot_scale = 1.0f / fmaxf(0.001f, inner_cone_cos - outer_cone_cos);
    float spot_bias = -outer_cone_cos * spot_scale;
    light->spot_scale_bias = glm_packHalf2x16((vec2){spot_scale, spot_bias});
}
```

---

## PBR Shading Model

**Location**: [pbr.h](../Granite/assets/shaders/lights/pbr.h)

Granite uses a standard Cook-Torrance microfacet BRDF:

### GGX Normal Distribution (D)
```glsl
float D_GGX(float roughness, vec3 N, vec3 H) {
    float NoH = clamp(dot(N, H), 0.0001, 1.0);
    float m = roughness * roughness;
    float m2 = m * m;
    float d = (NoH * m2 - NoH) * NoH + 1.0;
    return m2 / (PI * d * d);
}
```

### Schlick-GGX Geometry (G)
```glsl
float G_schlick(float roughness, float NoV, float NoL) {
    float r = roughness + 1.0;
    float k = r * r * (1.0 / 8.0);
    float V = NoV * (1.0 - k) + k;
    float L = NoL * (1.0 - k) + k;
    return 0.25 / max(V * L, 0.001); // 1/(4*NoV*NoL) folded in
}
```

### Fresnel (F)
```glsl
vec3 fresnel(vec3 F0, float HoV) {
    return mix(F0, vec3(1.0), pow(1.0 - HoV, 5.0));
}

vec3 compute_F0(vec3 base_color, float metallic) {
    return mix(vec3(0.04), base_color, metallic);
}
```

### Complete BRDF
```glsl
vec3 cook_torrance_specular(vec3 N, vec3 H, float NoL, float NoV, vec3 F, float roughness) {
    float D = D_GGX(roughness, N, H);
    float G = G_schlick(roughness, NoV, NoL);
    return F * G * D;
}
```

---

## Clustered Lighting

**Location**: [clusterer.h](../Granite/assets/shaders/lights/clusterer.h), [clusterer_bindless.h](../Granite/assets/shaders/lights/clusterer_bindless.h)

### Cluster Parameters

```c
typedef struct ClustererParameters {
    mat4  transform;        // View-projection for cluster assignment
    vec4  clip_scale;       // Scaling factors
    
    vec3  camera_base;      // Camera position
    float _pad0;
    vec3  camera_front;     // Camera forward vector
    float _pad1;
    
    vec2  xy_scale;         // Screen to cluster scale
    ivec2 resolution_xy;    // Cluster grid resolution (e.g., 64x32)
    vec2  inv_resolution_xy;
    
    int   num_lights;
    int   num_lights_32;    // ceil(num_lights / 32) for bitmask iteration
    int   num_decals;
    int   num_decals_32;
    int   decals_texture_offset;
    int   z_max_index;      // Max Z slice index
    float z_scale;          // Depth to Z-slice conversion
} ClustererParameters;
```

### Cluster Bitmask Structure

```c
// Per-cluster bitmask buffer (SSBO)
// cluster_bitmask[cluster_index * num_lights_32 + word_index]
// Each bit = 1 light, iterate set bits

// Range buffer for Z-culling
// cluster_range[z_index] = uvec2(min_light_bit, max_light_bit)
```

### Light Iteration (Fragment Shader)
```glsl
vec3 compute_cluster_light(...) {
    // 1. Compute cluster coordinate from fragment position
    ivec2 cluster_coord = ivec2(gl_FragCoord.xy * inv_resolution * xy_scale);
    cluster_coord = clamp(cluster_coord, ivec2(0), resolution_xy - 1);
    int cluster_index = cluster_coord.y * resolution_xy.x + cluster_coord.x;
    int cluster_base = cluster_index * num_lights_32;
    
    // 2. Compute Z-slice for depth culling
    float z = dot(world_pos - camera_base, camera_front);
    int z_index = clamp(int(z * z_scale), 0, z_max_index);
    uvec2 z_range = cluster_range[z_index];
    
    // 3. Subgroup optimization: find common range
    int z_start = int(subgroupMin(z_range.x) >> 5u);
    int z_end = int(subgroupMax(z_range.y) >> 5u);
    
    // 4. Iterate bitmask words
    vec3 result = vec3(0.0);
    for (int i = z_start; i <= z_end; i++) {
        uint mask = cluster_bitmask[cluster_base + i];
        mask = cluster_mask_range(mask, z_range, 32u * i);
        mask = subgroupOr(mask); // Coherent access
        
        int type_mask = int(type_mask_buffer[i]);
        while (mask != 0u) {
            int bit = findLSB(mask);
            int index = 32 * i + bit;
            
            if ((type_mask & (1 << bit)) != 0)
                result += compute_point_light(index, ...);
            else
                result += compute_spot_light(index, ...);
            
            mask &= ~(1u << bit);
        }
    }
    return result;
}
```

---

## Shadow Mapping

### Directional Shadows (Cascaded)

**Location**: [lighting_resources.h](../Granite/assets/shaders/lights/lighting_resources.h)

```c
#define SHADOW_NUM_CASCADES 4

typedef struct ShadowParameters {
    mat4  transforms[SHADOW_NUM_CASCADES]; // World-to-shadow-clip per cascade
    float cascade_log_bias;                // log2 scale for cascade selection
} ShadowParameters;
```

**Cascade Selection**:
```glsl
float view_z = max(dot(camera_front, world_pos - camera_pos), 0.0);
float cascade_idx = log2(view_z) + cascade_log_bias;
int layer_near = clamp(int(cascade_idx), 0, NUM_CASCADES - 1);
```

### Point Light Shadows (Cubemap)

```glsl
// Shadow lookup
vec3 light_dir = world_pos - light_pos;
float max_z = max(max(abs(light_dir.x), abs(light_dir.y)), abs(light_dir.z));

// Transform for depth comparison
vec4 shadow_transform = point_shadow_transforms[index];
vec2 shadow_ref2 = shadow_transform.zw - shadow_transform.xy * max_z;
float shadow_ref = shadow_ref2.x / shadow_ref2.y;

float shadow = texture(samplerCubeShadow(shadow_atlas[index], shadow_sampler),
                       vec4(light_dir, shadow_ref));
```

### Spot Light Shadows (2D Atlas)

```glsl
vec4 clip = spot_shadow_transform[index] * vec4(world_pos, 1.0);
float shadow = textureProjLod(sampler2DShadow(shadow_atlas[index], shadow_sampler),
                              clip, 0.0);
```

### Shadow Techniques

**PCF (Percentage Closer Filtering)** - [pcf.h](../Granite/assets/shaders/lights/pcf.h):
```glsl
// Multi-tap PCF for soft shadows
#define SAMPLE_PCF_KERNEL(result, sampler, coord) \
    result = 0.0; \
    for (int i = 0; i < PCF_SAMPLES; i++) { \
        vec2 offset = pcf_offsets[i] * shadow_texel_size; \
        result += texture(sampler, vec4(coord.xy + offset, coord.z, coord.w)); \
    } \
    result /= float(PCF_SAMPLES);
```

**VSM (Variance Shadow Maps)** - [vsm.h](../Granite/assets/shaders/lights/vsm.h):
```glsl
float vsm(float depth, vec2 moments) {
    float variance = moments.y - moments.x * moments.x;
    variance = max(variance, 0.00001);
    float d = depth - moments.x;
    float p = variance / (variance + d * d);
    return max(step(depth, moments.x), p);
}
```

---

## Volumetric Effects

### Volumetric Fog

**Location**: [fog.h](../Granite/assets/shaders/lights/fog.h), [volumetric_fog.h](../Granite/assets/shaders/lights/volumetric_fog.h)

```glsl
// Simple distance fog
float fog_factor(vec3 eye_vec, float falloff) {
    float distance = dot(eye_vec, eye_vec);
    return exp2(-distance * falloff);
}

vec3 apply_fog(vec3 color, vec3 eye_vec, vec3 fog_color, float falloff) {
    float lerp = fog_factor(eye_vec, falloff);
    return mix(fog_color, color, lerp);
}
```

### Volumetric Diffuse (GI Probes)

**Location**: [volumetric_diffuse.h](../Granite/assets/shaders/lights/volumetric_diffuse.h)

Uses 3D texture atlases storing irradiance in 6 directions (±X, ±Y, ±Z).

```c
typedef struct DiffuseVolumeParameters {
    vec4  world_to_texture[3]; // Affine transform rows
    vec4  world_lo;            // Volume AABB min
    vec4  world_hi;            // Volume AABB max
    float lo_tex_coord_x;
    float hi_tex_coord_x;
    float guard_band_factor;
    float guard_band_sharpen;
} DiffuseVolumeParameters;
```

```glsl
vec3 compute_volumetric_diffuse(int index, DiffuseVolumeParameters vol, vec3 pos, vec3 N) {
    // Transform world to volume local coords
    vec3 local = vec3(
        dot(vec4(pos, 1.0), vol.world_to_texture[0]),
        dot(vec4(pos, 1.0), vol.world_to_texture[1]),
        dot(vec4(pos, 1.0), vol.world_to_texture[2]));
    
    // Sample 6-direction irradiance (normal-weighted)
    vec3 N2 = N * N;
    vec3 offsets = mix(vec3(0.0), vec3(1.0/6.0), lessThan(N, vec3(0.0)));
    
    vec3 irradiance = 
        N2.x * texture(volume_atlas[index], vec3(base_x + 0.0/3.0 + offsets.x, local.yz)).rgb +
        N2.y * texture(volume_atlas[index], vec3(base_x + 1.0/3.0 + offsets.y, local.yz)).rgb +
        N2.z * texture(volume_atlas[index], vec3(base_x + 2.0/3.0 + offsets.z, local.yz)).rgb;
    
    return irradiance;
}
```

---

## Integration Steps

### Step 1: Add Light Data Structures

Create `vk_lights.h`:

```c
#ifndef VK_LIGHTS_H_
#define VK_LIGHTS_H_

#include "tinytypes.h"

// GPU-side light (48 bytes, std430)
typedef struct PositionalLightGpu {
    vec3     color;
    uint32_t spot_scale_bias;
    vec3     position;
    uint32_t offset_radius;
    vec3     direction;
    float    inv_radius;
} PositionalLightGpu;

// Directional light (UBO)
typedef struct DirectionalLightGpu {
    vec3  color;
    float _pad0;
    vec3  direction;
    float _pad1;
} DirectionalLightGpu;

// Light type enum for clustering
typedef enum LightType {
    LIGHT_TYPE_POINT = 0,
    LIGHT_TYPE_SPOT  = 1,
} LightType;

// CPU-side light handle
typedef struct PointLight {
    vec3  color;
    vec3  position;
    float radius;
    float falloff_range;
} PointLight;

typedef struct SpotLight {
    vec3  color;
    vec3  position;
    vec3  direction;
    float inner_cone; // cosine of inner half-angle
    float outer_cone; // cosine of outer half-angle
    float radius;
} SpotLight;

// Pack CPU light to GPU format
PositionalLightGpu point_light_to_gpu(const PointLight* light);
PositionalLightGpu spot_light_to_gpu(const SpotLight* light);

#endif
```

### Step 2: Create Light Buffer Management

```c
typedef struct LightManager {
    PositionalLightGpu* lights_gpu;
    uint32_t*           type_mask;     // Bit per light: 0=spot, 1=point
    uint32_t            light_count;
    uint32_t            max_lights;
    
    Buffer              light_buffer;
    Buffer              type_mask_buffer;
    
    // Shadow atlases
    VkImage             spot_shadow_atlas;
    VkImage             point_shadow_atlas;  // Cubemap array
} LightManager;

void light_manager_init(LightManager* mgr, ResourceAllocator* alloc, uint32_t max_lights);
uint32_t light_manager_add_point(LightManager* mgr, const PointLight* light);
uint32_t light_manager_add_spot(LightManager* mgr, const SpotLight* light);
void light_manager_upload(LightManager* mgr, VkDevice dev, VkQueue queue, VkCommandPool pool);
```

### Step 3: Add Cluster Compute Pass

Create compute shader `shaders/cluster_assign.comp`:

```glsl
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set=0, binding=0) buffer ClusterBitmask { uint bitmask[]; };
layout(set=0, binding=1) buffer LightBuffer { PositionalLightGpu lights[]; };

layout(push_constant) uniform ClusterPC {
    mat4  inv_proj;
    ivec3 cluster_dims;  // e.g., 16x8x24
    int   num_lights;
    vec2  screen_dims;
    float z_near;
    float z_far;
};

shared uint shared_mask[MAX_LIGHTS_32];

void main() {
    ivec3 cluster_id = ivec3(gl_WorkGroupID);
    uint local_id = gl_LocalInvocationIndex;
    
    // Clear shared mask
    if (local_id < MAX_LIGHTS_32)
        shared_mask[local_id] = 0u;
    barrier();
    
    // Compute cluster AABB in view space
    vec3 cluster_min = compute_cluster_aabb_min(cluster_id);
    vec3 cluster_max = compute_cluster_aabb_max(cluster_id);
    
    // Each thread tests subset of lights
    for (uint i = local_id; i < num_lights; i += 64) {
        if (light_intersects_aabb(lights[i], cluster_min, cluster_max)) {
            uint word = i / 32;
            uint bit = i % 32;
            atomicOr(shared_mask[word], 1u << bit);
        }
    }
    barrier();
    
    // Write to global buffer
    uint cluster_idx = cluster_id.z * cluster_dims.x * cluster_dims.y +
                       cluster_id.y * cluster_dims.x + cluster_id.x;
    if (local_id < MAX_LIGHTS_32) {
        bitmask[cluster_idx * MAX_LIGHTS_32 + local_id] = shared_mask[local_id];
    }
}
```

### Step 4: Integrate into Fragment Shaders

Update `shaders/terrain.frag` or create `shaders/pbr_lit.frag`:

```glsl
#version 450

// Include PBR functions
#include "pbr.glsl"

layout(set=0, binding=0) uniform GlobalUBO { ... } ubo;
layout(set=1, binding=0) uniform DirectionalLight { ... } sun;
layout(set=2, binding=0) buffer LightBuffer { PositionalLightGpu lights[]; };
layout(set=2, binding=1) buffer ClusterBitmask { uint cluster_bitmask[]; };
layout(set=2, binding=2) buffer TypeMask { uint type_mask[]; };

layout(push_constant) uniform PC {
    // ... existing terrain PC
    ivec2 cluster_dims;
    int   num_lights_32;
};

void main() {
    // Material properties
    vec3 albedo = base_color;
    float metallic = 0.0;
    float roughness = 0.5;
    vec3 N = normalize(vWorldNrm);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    
    // 1. Directional light
    vec3 result = compute_directional_light(albedo, N, V, metallic, roughness,
                                            sun.direction, sun.color);
    
    // 2. Clustered positional lights
    ivec2 cluster_coord = ivec2(gl_FragCoord.xy / vec2(cluster_tile_size));
    int cluster_idx = cluster_coord.y * cluster_dims.x + cluster_coord.x;
    int cluster_base = cluster_idx * num_lights_32;
    
    for (int i = 0; i < num_lights_32; i++) {
        uint mask = cluster_bitmask[cluster_base + i];
        int types = int(type_mask[i]);
        
        while (mask != 0u) {
            int bit = findLSB(mask);
            int idx = 32 * i + bit;
            
            if ((types & (1 << bit)) != 0)
                result += compute_point_light(lights[idx], albedo, N, V, metallic, roughness, vWorldPos);
            else
                result += compute_spot_light(lights[idx], albedo, N, V, metallic, roughness, vWorldPos);
            
            mask &= ~(1u << bit);
        }
    }
    
    // 3. Ambient
    result += albedo * 0.03;
    
    outColor = vec4(result, 1.0);
}
```

### Step 5: Update Render Loop

```c
// In frame loop (test.c)

// 1. Update light buffer if lights changed
if (lights_dirty) {
    light_manager_upload(&light_mgr, device, queue, pool);
    lights_dirty = false;
}

// 2. Run cluster assignment compute
GPU_SCOPE(cmd, P, "cluster_assign", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cluster_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);
    vkCmdDispatch(cmd, cluster_dims.x, cluster_dims.y, cluster_dims.z);
}

// 3. Barrier: compute -> fragment read
BUFFER_BARRIER_IMMEDIATE(cmd, cluster_bitmask_buffer,
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

// 4. Render scene with lighting
// ... existing terrain/object rendering
```

---

## Data Structures Summary

### UBO Layout (set=0)

```c
// Binding 0: Global render parameters
layout(std140, set=0, binding=0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
    vec4 cameraFront;
    // Fog
    vec3 fogColor;
    float fogFalloff;
    // Directional
    vec3 sunColor;
    float _pad;
    vec3 sunDirection;
    float _pad2;
};
```

### SSBO Layout (set=2)

```c
// Binding 0: Light data
layout(std430, set=2, binding=0) readonly buffer Lights {
    PositionalLightGpu lights[];
};

// Binding 1: Cluster bitmasks
layout(std430, set=2, binding=1) readonly buffer Clusters {
    uint cluster_bitmask[];  // [cluster_count * ceil(light_count/32)]
};

// Binding 2: Light type mask (1=point, 0=spot)
layout(std430, set=2, binding=2) readonly buffer TypeMask {
    uint type_mask[];  // [ceil(light_count/32)]
};

// Binding 3: Z-range for culling
layout(std430, set=2, binding=3) readonly buffer ZRange {
    uvec2 z_range[];  // [z_slice_count]
};
```

---

## Performance Considerations

1. **Cluster Size**: 64x32 screen tiles with 24 depth slices is typical
2. **Max Lights**: 256-4096 depending on target hardware
3. **Shadow Atlas**: 4096x4096 with 512x512 per light typical
4. **Subgroup Operations**: Use `subgroupOr`/`subgroupMin` for coherent access
5. **Early-out**: Check `mask == 0` to skip empty clusters

---

## Files to Create

1. `vk_lights.h` - Light structures and API
2. `vk_lights.c` - Light management implementation
3. `vk_clusterer.h` - Cluster parameters and compute dispatch
4. `shaders/pbr.glsl` - PBR BRDF functions (include file)
5. `shaders/cluster_assign.comp` - Light binning compute shader
6. `shaders/lights_common.glsl` - Point/spot evaluation functions

---

## References

- Granite source: `Granite/renderer/lights/`
- Granite shaders: `Granite/assets/shaders/lights/`
- Clustered shading paper: [Olsson & Assarsson, 2012](http://www.cse.chalmers.se/~uMDRag/ClusterAssign.pdf)
- PBR reference: [Filament PBR](https://google.github.io/filament/Filament.md.html)
