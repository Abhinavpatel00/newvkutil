
# Copilot instructions for vkutil

## Big picture
- You are working on a Vulkan renderer with a strict goal:
**High performance, scalable, data-driven, GPU-driven rendering**.
Prioritize **predictable GPU work**, **minimal CPU overhead**, and **correct synchronization**.
- The app entry point and renderer orchestration live in [test.c](test.c). This file wires windowing, Vulkan setup, resource uploads, per-frame update, and rendering/compute passes.
- Vulkan utilities are modularized in the vk_*.c/h files (device setup, swapchain, commands, barriers, pipelines, descriptors). Pipeline layouts are derived via SPIR-V reflection in [vk_pipelines.c](vk_pipelines.c) using [vk_shader_reflect.c](vk_shader_reflect.c).
- Mesh/scene data comes from glTF loading and packing in [scene.c](scene.c) and types in [scene.h](scene.h), with vertex packing and bounds computed there.
- GPU-driven rendering uses storage buffers for draw data and indirect commands; compute culling/LOD fills these buffers before the graphics pass (see [shaders/cull.comp](shaders/cull.comp) and the dispatch in [test.c](test.c)).
- The camera uses reverse-Z with infinite far plane (see `camera_build_proj_reverse_z_infinite()` in [camera.c](camera.c)); depth compare is `VK_COMPARE_OP_GREATER_OR_EQUAL` and depth clear is 0.0 in [test.c](test.c).

## Build and shader workflow
- Build: use the Makefile at the repo root (targets `test`, `clean`). See [Makefile](Makefile).
- Shader compilation: run the script in [cs.sh](cs.sh) to generate SPIR-V into compiledshaders/.
- The demo expects SPIR-V files like compiledshaders/tri.vert.spv, compiledshaders/tri.frag.spv, and compiledshaders/cull.comp.spv; see [test.c](test.c) and shaders/.

## Project-specific conventions
- - Prefer batching and indirect draw over CPU submission loops.
- Vertex pulling: shaders read vertex/index data from SSBOs instead of traditional vertex attributes (see [shaders/tri.vert](shaders/tri.vert) and the buffer bindings setup in [test.c](test.c)).
- Descriptor layout caching and allocation is centralized in [vk_descriptor.c](vk_descriptor.c) and [vk_descriptor.h](vk_descriptor.h). Bindless textures use update-after-bind layouts in [bindlesstextures.c](bindlesstextures.c).
- Synchronization uses helper macros in [vk_barrier.h](vk_barrier.h); follow existing `BUFFER_BARRIER_IMMEDIATE`/`IMAGE_BARRIER_IMMEDIATE` patterns when inserting new GPU passes.
- Resource memory is allocated via VMA wrappers in [vk_resources.c](vk_resources.c) and [vk_resources.h](vk_resources.h).

## External dependencies & integration points
- GLFW for windowing, Volk for Vulkan function loading, VMA for memory, cgltf for model import, meshoptimizer for mesh processing, and cglm for math; see the external/ directory and usage in [test.c](test.c) and [scene.c](scene.c).

## When editing or adding features
- Keep new GPU data structs mirrored between C and GLSL (see `Mesh`, `MeshLod` in [scene.h](scene.h) and their GPU equivalents in [test.c](test.c) and [shaders/cull.comp](shaders/cull.comp)).
- If you add a new shader or binding, update the descriptor set layouts in [test.c](test.c) and recompile shaders with [cs.sh](cs.sh).

 Adding features (approved patterns)

### New GPU pass
When adding a new pass:
- define its input/output buffers explicitly
- add only necessary barriers
- reuse existing descriptor allocator/cache
- keep the frame loop readable

### New material feature
- extend `MaterialGpu` (std430 aligned)
- update packing in C
- update fragment shader sampling
- keep bindless indexing stable

### New effect (easy wins)
Prefer cheap post/lighting effects:
- fog (depth-based)
- rim light
- simple SSAO (optional later)
- tonemap + gamma
- sky gradient / procedural sky



Do NOT add transparency systems unless explicitly requested.

---

## 8) Build workflow
- Build via repo `Makefile` (`make test`, `make clean`)
- Shaders compile via `cs.sh` into `compiledshaders/`
- Keep shader filenames stable:
  - `tri.vert.spv`
  - `tri.frag.spv`
