# Plan: Simple Render Object Abstraction
we handle frame of flight with macro so heap alloc can be avoided in many places 
## Goal
Create a small, data-driven “render object” abstraction that minimizes per-effect boilerplate (descriptor writes, pipeline/layout wiring, set scattering) while staying compatible with the current GPU-driven renderer. The abstraction should:
- Accept a concise data specification (buffers, images, push constants, shaders).
- Generate/return handles needed for rendering (pipeline, layout, descriptor sets, bindings).
- Keep synchronization and pass ordering explicit.
- Reuse existing descriptor cache/allocator and SPIR-V reflection.

## Non-Goals
- Replace the existing render loop or GPU-driven draw dispatch.
- Hide synchronization or frame graph ownership.
- Introduce transparency or other large new systems.

---

## 1) Requirements & Constraints
- Maintain predictable GPU work and minimal CPU overhead.
- Reuse descriptor layout cache + allocator in [vk_descriptor.c](../vk_descriptor.c).
- Use SPIR-V reflection via [vk_pipelines.c](../vk_pipelines.c) and [vk_shader_reflect.c](../vk_shader_reflect.c).
- Keep shader filenames and binding conventions stable.
- Explicitly define inputs/outputs for new passes.

---

## 2) Proposed Abstraction: `RenderObject`
A lightweight object that is *configured once* with a data spec and yields reusable handles. Descriptor sets are **reflected and managed internally** (layout creation, allocation, and writes), so call sites only provide concrete resources.

### 2.1 Data Spec (C struct)
```c
typedef struct RenderObjectSpec {
    // Shaders
    const char *vert_spv;
    const char *frag_spv; // optional
    const char *comp_spv; // optional

    // Fixed-function hints
    VkPrimitiveTopology topology;
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    VkCompareOp depth_compare;
    VkBool32 depth_test;
    VkBool32 depth_write;

    // Descriptor behavior (layout is reflected)
    VkBool32 allow_update_after_bind;
    VkBool32 use_bindless_if_available;
    VkBool32 per_frame_sets; // allocate one set per frame

    // Push constants (optional)
    // Size + stages are derived from reflection.

    // Dynamic states (optional)
    uint32_t dynamic_state_count;
    const VkDynamicState *dynamic_states;

    // Specialization constants (optional)
    uint32_t spec_constant_count;
    const VkSpecializationMapEntry *spec_map;
    const void *spec_data;
    uint32_t spec_data_size;
} RenderObjectSpec;
```

### 2.2 Reflected Layout + Resource Writes
Bindings and sets are derived from SPIR-V reflection. The call site only submits **writes** for the reflected bindings it wants to populate.

### 2.3 Runtime Object
```c
typedef struct RenderObject {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout *set_layouts;
    VkDescriptorSet *sets;       // per-frame or persistent
    uint32_t set_count;

    // Cached reflection metadata (optional)
    RenderObjectReflection refl;
} RenderObject;
```

---

## 3) API Sketch
### 3.1 C Prototypes (pseudocode)
```c
// Creation
RenderObject render_object_create(
  VkDevice device,
  VkRenderPass pass,
  const RenderObjectSpec *spec,
  DescriptorAllocator *alloc,
  DescriptorLayoutCache *cache,
  VkPipelineCache pipeline_cache);

// Update descriptors by name or binding (reflection-backed)
// If name is non-null, it is resolved to a binding + set.
void render_object_write_buffer(
  RenderObject *obj,
  const char *name,
  uint32_t set,
  uint32_t binding,
  VkBuffer buffer,
  VkDeviceSize offset,
  VkDeviceSize range,
  uint32_t frame_index);

void render_object_write_image(
  RenderObject *obj,
  const char *name,
  uint32_t set,
  uint32_t binding,
  VkImageView view,
  VkSampler sampler,
  VkImageLayout layout,
  uint32_t frame_index);

// Bulk write API (preferred): name-based, set/binding resolved via reflection
typedef enum RenderWriteType {
  RENDER_WRITE_BUFFER,
  RENDER_WRITE_IMAGE,
} RenderWriteType;

typedef struct RenderWrite {
  const char *name; // shader binding name
  RenderWriteType type;
  union {
    struct { VkBuffer buffer; VkDeviceSize offset; VkDeviceSize range; } buf;
    struct { VkImageView view; VkSampler sampler; VkImageLayout layout; } img;
  } data;
} RenderWrite;

void render_object_write_all(
  RenderObject *obj,
  const RenderWrite *writes,
  uint32_t write_count,
  uint32_t frame_index);

// Bind (graphics or compute)
void render_object_bind(
  VkCommandBuffer cmd,
  const RenderObject *obj,
  VkPipelineBindPoint bind_point,
  uint32_t frame_index);

// Push constants (size/stages derived from reflection)
void render_object_push_constants(
  VkCommandBuffer cmd,
  const RenderObject *obj,
  const void *data,
  uint32_t size);

// Destroy
void render_object_destroy(VkDevice device, RenderObject *obj);
```

### 3.2 Example Call Site (pseudocode in C)
```c
// 1) Describe render object (no manual set/binding specs)
RenderObjectSpec spec = {
  .vert_spv = "compiledshaders/toon.vert.spv",
  .frag_spv = "compiledshaders/toon.frag.spv",
  .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  .cull_mode = VK_CULL_MODE_BACK_BIT,
  .front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
  .depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL,
  .depth_test = VK_TRUE,
  .depth_write = VK_TRUE,
  .per_frame_sets = VK_TRUE,
};

RenderObject toon = render_object_create(
  device, render_pass, &spec, &desc_alloc, &layout_cache, pipeline_cache);

// 2) Per-frame updates (bulk, name-based)
RenderWrite writes[] = {
  { .name = "Meshes", .type = RENDER_WRITE_BUFFER,
    .data.buf = { mesh_ssbo, 0, mesh_ssbo_size } },
  { .name = "Vertices", .type = RENDER_WRITE_BUFFER,
    .data.buf = { vertex_ssbo, 0, vertex_ssbo_size } },
  { .name = "Albedo", .type = RENDER_WRITE_IMAGE,
    .data.img = { albedo_view, sampler,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
};

render_object_write_all(&toon, writes, ARRAY_COUNT(writes), frame_index);

// 5) Draw
render_object_bind(cmd, &toon, VK_PIPELINE_BIND_POINT_GRAPHICS, frame_index);
render_object_push_constants(cmd, &toon, &toon_push, sizeof(ToonPush));

vkCmdDrawIndirect(cmd, indirect_buffer, 0, draw_count, sizeof(VkDrawIndirectCommand));
```

---

## 3.3 Phase 1 Scope (Base API Deliverables)
- Reflection-based set/layout creation.
- Internal descriptor set allocation (persistent or per-frame).
- Name-based bulk descriptor writes via `render_object_write_all()`.
- Pipeline creation + bind helpers.
- Push constants via reflection-derived ranges.

## 4) Implementation Steps (Phase 1: Base API)

### Step 1 — Define Specs & Object
- Add new files:
  - [render_object.h](../render_object.h)
  - [render_object.c](../render_object.c)
- Define `RenderObjectSpec` and `RenderObject` (base API only).

### Step 2 — Reflection + Layout Build
- Use `vk_shader_reflect.c` to load reflection from SPIR-V.
- Build `VkDescriptorSetLayout` from reflection.
- Validate reflection output (debug asserts).
- Derive push-constant stages and size from reflection.

### Step 3 — Descriptor Set Allocation
- Use existing descriptor cache/allocator from [vk_descriptor.c](../vk_descriptor.c).
- Support:
  - persistent sets (allocated once)
  - per-frame sets (optional flag in spec)

### Step 4 — Pipeline Creation
- Use `vk_pipelines.c` pipeline builder utilities.
- Create `VkPipelineLayout` from set layouts + push constants.
- Create graphics or compute pipeline based on shader inputs.

### Step 5 — Update & Bind Helpers
- Implement `render_object_write_all()` (name-based writes, no SoA yet).
- Implement `render_object_bind` to bind pipeline + sets.
- Implement `render_object_push_constants` to use reflection-derived ranges.

### Step 6 — Integrate in `test.c`
- Replace one existing effect (e.g., toon or terrain) with `RenderObject`.
- Keep compute + graphics passes explicit and preserve barriers.

### Step 7 — Validation
- Ensure `VK_COMPARE_OP_GREATER_OR_EQUAL` and reverse-Z defaults.
- Verify descriptor update-after-bind for bindless textures if used.
- Confirm no new stalls or extra CPU work.

---

## 5) Incremental Rollout Strategy
1. Create a `RenderObject` for a simple effect (toon).
2. Measure CPU time vs current approach.
3. Migrate one more effect (terrain or water).
4. Decide whether to expand to all pipelines.

---

## 6) Risks & Mitigations
- **Risk:** Layout mismatch between reflection and spec.
  - **Mitigation:** Assert binding compatibility and dump reflection info in debug builds.
- **Risk:** Descriptor update overhead.
  - **Mitigation:** Provide “per-frame” vs “persistent” sets and only update changed bindings.
- **Risk:** Complexity creep.
  - **Mitigation:** Keep the object small; do not hide pass scheduling or barriers.

---

## 7) Open Questions
- Should `RenderObject` own per-frame descriptor sets or return layouts only?
- Should we support multiple render passes or subpass variants per object?
- Should we enforce a strict naming convention for shader bindings to simplify setup?

---

## 8) Phase 2: Data-Oriented API Enhancements (later)
These are postponed until the base API is complete:

1) **Stable binding IDs**
  - Hash binding names at load time and store `uint32_t` or `uint64_t` IDs in reflection metadata.
  - Update calls use IDs to avoid runtime string compares.

2) **Single SoA write table**
  - Replace per-write calls with a single `RenderWriteTable` using structure-of-arrays:
    - `names[]`, `types[]`, `buffers[]`, `images[]`, etc.
  - `render_object_write_all()` consumes the table in one pass.

3) **Shader metadata tags for presets**
  - Allow shader annotations (e.g., `@bindless`, `@per_frame`) to drive descriptor flags.
  - Removes manual flags from `RenderObjectSpec`.

4) **Per-draw instances**
  - Introduce `RenderObjectInstance` for per-draw data:
    - Push constants + per-instance resources only.
  - Keeps `RenderObject` immutable and shareable.

5) **Split pipeline vs resources**
  - `RenderPipeline` = immutable pipeline + layout.
  - `RenderResources` = sets + updates (per frame or per instance).
  - Encourages reuse and reduces re-creation.

6) **Batch updates by default**
  - `render_object_write_all()` takes a pointer + count only.
  - Avoid per-write functions in hot loops.

### 8.1 C Pseudocode (deferred)
The following is **not** part of Phase 1 and will be implemented later.
```c
// Stable binding IDs (hashed at load time)
typedef uint64_t BindingId;
BindingId bind_id(const char *name);

// SoA write table
typedef struct RenderWriteTable {
  uint32_t count;
  BindingId *ids;           // size: count
  RenderWriteType *types;   // size: count

  VkBuffer *buffers;        // size: count
  VkDeviceSize *offsets;    // size: count
  VkDeviceSize *ranges;     // size: count

  VkImageView *views;       // size: count
  VkSampler *samplers;      // size: count
  VkImageLayout *layouts;   // size: count
} RenderWriteTable;

typedef struct RenderPipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  RenderObjectReflection refl;
} RenderPipeline;

typedef struct RenderResources {
  VkDescriptorSet *sets; // per-frame or persistent
  uint32_t set_count;
} RenderResources;

typedef struct RenderObjectInstance {
  RenderPipeline *pipe;
  RenderResources *res;
  uint8_t push_data[256]; // size validated against reflection
  uint32_t push_size;
} RenderObjectInstance;

// Create immutable pipeline (reflects sets, bindings, push constants)
RenderPipeline render_pipeline_create(
  VkDevice device,
  VkRenderPass pass,
  const RenderObjectSpec *spec,
  DescriptorLayoutCache *cache,
  VkPipelineCache pipeline_cache);

// Allocate resources (descriptor sets) once
RenderResources render_resources_alloc(
  VkDevice device,
  const RenderPipeline *pipe,
  DescriptorAllocator *alloc,
  uint32_t frames_in_flight,
  VkBool32 per_frame_sets);

// Batch update resources
void render_resources_write_all(
  RenderResources *res,
  const RenderPipeline *pipe,
  const RenderWriteTable *table,
  uint32_t frame_index);

// Bind + push
void render_instance_bind(
  VkCommandBuffer cmd,
  const RenderObjectInstance *inst,
  VkPipelineBindPoint bind_point,
  uint32_t frame_index);

void render_instance_push(
  VkCommandBuffer cmd,
  const RenderObjectInstance *inst);

// Example usage
RenderPipeline pipe = render_pipeline_create(device, pass, &spec, &cache, pipeline_cache);
RenderResources res = render_resources_alloc(device, &pipe, &alloc, frames_in_flight, VK_TRUE);

BindingId ids[] = { bind_id("Meshes"), bind_id("Vertices"), bind_id("Albedo") };
RenderWriteType types[] = { RENDER_WRITE_BUFFER, RENDER_WRITE_BUFFER, RENDER_WRITE_IMAGE };
VkBuffer bufs[] = { mesh_ssbo, vertex_ssbo, VK_NULL_HANDLE };
VkDeviceSize offs[] = { 0, 0, 0 };
VkDeviceSize ranges[] = { mesh_ssbo_size, vertex_ssbo_size, 0 };
VkImageView views[] = { VK_NULL_HANDLE, VK_NULL_HANDLE, albedo_view };
VkSampler samplers[] = { VK_NULL_HANDLE, VK_NULL_HANDLE, sampler };
VkImageLayout layouts[] = { 0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

RenderWriteTable table = {
  .count = 3,
  .ids = ids,
  .types = types,
  .buffers = bufs,
  .offsets = offs,
  .ranges = ranges,
  .views = views,
  .samplers = samplers,
  .layouts = layouts,
};

render_resources_write_all(&res, &pipe, &table, frame_index);

RenderObjectInstance inst = { .pipe = &pipe, .res = &res };
memcpy(inst.push_data, &toon_push, sizeof(ToonPush));
inst.push_size = sizeof(ToonPush);

render_instance_bind(cmd, &inst, VK_PIPELINE_BIND_POINT_GRAPHICS, frame_index);
render_instance_push(cmd, &inst);
```

---

## 9) Success Criteria
- Adding a new effect requires:
  - A `RenderObjectSpec` definition
  - Minimal set of descriptor updates
  - No manual layout plumbing or descriptor scatter
- No regression in frame time or GPU synchronization correctness.
