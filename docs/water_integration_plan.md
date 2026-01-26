# Stylized water integration plan

## Goal
Integrate the Unity stylized water package into the Vulkan renderer while preserving GPU-driven, data-driven design and minimal CPU overhead.

## Inputs (Unity asset)
- Source package: unity-stylized-water/Assets/Stylized Water
- Expected data:
  - Water textures (normal, foam, noise, depth/flow masks if present)
  - Material parameters (colors, shallow/deep tint, foam intensity, distortion, tiling)
  - Any animated settings (wind speed, wave scale)

## Integration plan
1. **Asset inventory & extraction**
   - List textures, materials, and any meshes in the Unity package.
   - Export textures to a renderer-friendly format (PNG/TGA/KTX2). Use external/dds-ktx and stb image.
   - Record material parameter defaults for the water surface.

2. **Water data model (CPU/GPU)**
   - Define `WaterMaterialGpu` (std430 aligned) with colors, tiling, speeds, and strength parameters.
   - Define `WaterInstanceGpu` for per-water-plane data (transform, bounds, material index).
   - Mirror structs in GLSL and C.

3. **Shader translation (Unity → GLSL)**
   - Translate the Unity shader logic to Vulkan GLSL (vertex + fragment).
   - Features to keep:
     - Depth-based color fade (shallow/deep)
     - Surface normals from normal map + time-based scrolling
     - Foam generation near shore (depth or height-based)
     - Simple specular highlight
   - Keep the shader minimal and compatible with existing renderer conventions.

4. **Rendering path**
   - Add a dedicated water draw pass after opaque terrain/geometry, before post effects.
   - Use a single pipeline with transparency/blending if required.
   - Ensure depth testing uses reverse-Z conventions.

5. **Descriptors & bindings**
   - Use existing descriptor allocator/cache.
   - Bind:
     - Global UBO (camera)
     - Water material buffer (SSBO)
     - Water instance buffer (SSBO)
     - Textures (normal/foam/noise)

6. **Water mesh setup**
   - Start with a large grid/plane aligned to terrain extents.
   - Support per-tile instances if needed for large scenes.

7. **Synchronization & barriers**
   - Insert minimal barriers for any water-related compute or updates.
   - Reuse existing barrier helpers (`vk_barrier.h`).

8. **Debug & tuning hooks**
   - Add GUI toggles for water parameters and on/off switch.
   - Provide quick switches for foam and distortion.

9. **Performance checks**
   - Keep one draw call for water where possible.
   - Minimize descriptor changes and avoid per-frame reallocations.

## Deliverables
- Water shaders: `shaders/water.vert`, `shaders/water.frag`
- GPU structs in C and GLSL
- Water pipeline setup in `test.c`
- Water textures imported into bindless system
- GUI controls for tuning

## Open questions
- Which textures from the Unity package are essential? aimost ass
- Do we need refraction or screen-space reflection, or keep it simple? we need it
- Should water be a single plane or tiled patches? which is good performance
