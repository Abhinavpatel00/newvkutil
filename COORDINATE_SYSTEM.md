# Coordinate System (Repo Standard)

This repo uses **one consistent coordinate system** across C and GLSL.

## World Space (authoring, physics, scene data)
- **Handedness:** right-handed
- **Axes:**
  - +X = right
  - +Y = up
  - +Z = back
  - **-Z = forward**
- **Units:** 1.0 = 1 meter (assumed)
- **Rotations:** right-handed, quaternions in (x, y, z, w)

## View Space (camera)
- Camera looks down **-Z** in its local space.
- `camera_build_view()` returns the inverse of the camera transform.
- Basis vectors are consistent with world-space axes (+X right, +Y up, -Z forward).

## Clip / NDC / Depth (Vulkan)
- **Depth range:** $[0, 1]$
- **Projection:** reverse-Z, infinite far plane
- **Depth compare:** `VK_COMPARE_OP_GREATER_OR_EQUAL`
- **Depth clear:** 0.0
- **Y flip:** applied in CPU (`build_global_ubo`) for Vulkan screen-space Y-down.

## Winding / Face Culling
- **Front face:** counter-clockwise (CCW)
- **Default culling:** none (unless overridden per pipeline)

## Shader Expectations
- Use `GlobalUBO.viewproj` directly for world → clip.
- When computing forward depth in view space, use **`viewZ = -posView.z`**.
- Terrain height is **+Y**.

If you add new shaders or systems, **follow this standard** to avoid axis/handedness drift.
