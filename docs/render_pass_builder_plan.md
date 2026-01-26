# Render/Pass Object Builder Plan

## Goal
Create a simple, data-driven API that describes a pass/pipeline once and returns handles for rendering, reducing manual descriptor layout/sets/pipeline wiring across multiple files.

## Design Principles
- Keep GPU-driven focus: minimal CPU work per frame, predictable GPU work.
- Data-driven: describe resources and shader stages in one place.
- Reuse existing caches: `DescriptorLayoutCache`, `PipelineLayoutCache`, `DescriptorAllocator`.
- Zero hidden sync: caller still inserts explicit barriers; builder only builds layouts, sets, pipelines.

## Proposed Abstraction
### `PassDesc`
Defines the pass in one struct:
- Shader paths (vert/frag/comp).
- Descriptor set bindings per set (binding, type, stage flags, array count, bindless flags).
- Push constant ranges.
- Render state (blend, depth test/write, cull, topology, formats).
- Static resources (optional): immutable samplers.

### `PassHandles`
Returned object:
- `VkPipeline` + `VkPipelineLayout`.
- `VkDescriptorSetLayout[]`.
- `VkDescriptorSet[]` (optional, only if requested).
- `PassId` for hot reload / rebuild.

### `ResourceBinding`
A small struct to bind buffers/images by name or binding index:
- Can be updated per-frame without recreating layouts.
- Supports bindless set as a standard slot (set index N reserved for bindless).

## Implementation Steps
1. **Define core structs**
   - Create `pass_builder.h/.c` with `PassDesc`, `PassHandles`, `ResourceBinding`.
2. **Descriptor layout assembly**
   - Convert `PassDesc` bindings to `VkDescriptorSetLayoutBinding` arrays.
   - Support optional update-after-bind flags for bindless set.
3. **Pipeline layout creation**
   - Use `pipeline_layout_cache_get` with set layouts and push constants.
4. **Pipeline creation**
   - Call existing `create_graphics_pipeline` / `create_compute_pipeline`.
5. **Descriptor set allocation (optional)**
   - If `PassDesc` requests persistent sets, allocate via `DescriptorAllocator`.
6. **Resource update helper**
   - `pass_update_resources(PassHandles*, ResourceBinding* bindings)` to write buffers/images.
7. **Hot reload integration**
   - Register pass pipelines with existing hot reload system.
8. **Minimal example**
   - Replace one pipeline (e.g., water or toon) to validate flow.

## Example Usage (target API)
- Create a `PassDesc` with shaders + bindings.
- Build pass once at startup.
- Each frame: update push constants, bind pass pipeline + descriptor sets, draw.

## Out of Scope
- Barrier insertion
- Render graph scheduling
- Automatic resource lifetime management

## Adoption Plan
- Phase 1: Implement builder + migrate a single pipeline.
- Phase 2: Migrate terrain + water.
- Phase 3: Migrate GLTF/toon + compute passes.
- Phase 4: Optional render graph wrapper.
