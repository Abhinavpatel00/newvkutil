# Bindless performance analysis (shaders directory)

## Executive summary
Bindless **can improve CPU-side performance** by reducing descriptor rebinds when many materials/textures are used per draw/dispatch. It **does not inherently reduce GPU shading cost** and **won’t help** passes that already use fixed inputs (single color/depth or fixed heightmaps). In this repo, the main places where bindless matters are the **material-driven mesh passes** (tri/toon/water). Most other shaders either **use no textures** or **use a small, fixed set**—bindless won’t help there.

## What “bindless” helps with
Bindless primarily reduces:
- **Descriptor set churn** when objects use many different textures.
- **CPU overhead** from per-draw descriptor updates/binds.
- **Pipeline variants** when you can unify sampling paths with indices.

Bindless does **not** magically reduce texture sampling cost or bandwidth. It can **increase** GPU cost in some cases due to:
- Non-uniform indexing (harder for caching/prefetch).
- Descriptor indexing indirections.
- Potentially lower cache locality if you sample from many different textures.

## Shader-by-shader analysis (shaders/)

### 1) Materialized mesh passes (bindless already used)
These are the best candidates for bindless, and **already use it**:
- **tri.vert / tri.frag**: `u_textures[]` + non-uniform indexing. Material texture indices come from `MaterialGpu`. Bindless is the right choice here to avoid rebinding textures per draw.
- **toon.vert / toon.frag / toon_outline.frag**: Same pattern with `u_textures[]` and per-material indices.
- **water.frag**: Uses `u_textures[]` with `nonuniformEXT` index for normal/foam maps.
- **water.slang**: Also uses a bindless `Texture2D[]` array and sampler. (This is the Slang version of water.)

**Performance impact:**
- **CPU side:** Positive (fewer descriptor writes/binds).
- **GPU side:** Usually neutral to slightly negative if indices vary widely per wavefront.

**Bindless won’t help further** here unless you *also* move other fixed samplers/inputs into the bindless set (but then you trade simplicity for little gain).

---

### 2) Terrain + grass (fixed samplers, low variety)
- **terrain.vert** / **grass.vert** sample two fixed heightmaps (`uBaseHeight`, `uSculptDelta`).
- **terrain.frag** and **grass.frag** are purely procedural; no texture sampling.

**Performance impact:**
- Bindless provides **no benefit**. There are only two fixed textures, and they’re the same for all draws.
- Keeping them fixed is ideal for cache coherence and predictable binding.

---

### 3) Postprocess, tonemap, DOF (fixed single inputs)
- **postprocess.slang** uses fixed bindings: `inputImage`, `outputImage`, `linearSampler`.
- **tonemap.frag** uses `uColor` (single input).
- **dof.frag** uses `uColor` + `uDepth` (two inputs).

**Performance impact:**
- Bindless doesn’t help. These passes run once per frame and use a fixed set of images.
- The descriptor updates are minimal and unavoidable (especially for swapchain output).

---

### 4) Fullscreen / debug / sky (no textures)
- **fullscreen.vert/frag**: no texture sampling.
- **sky.slang**: procedural sky, no textures.
- **debug_text.comp**: writes to a storage image only; no sampling.

**Performance impact:**
- Bindless provides **zero benefit**.

---

### 5) Compute (cull, terrain paint)
- **cull.comp**: uses only buffers, no textures.
- **terrain_paint.comp**: uses a single storage image (`sculptDelta`).

**Performance impact:**
- Bindless doesn’t help; these are single-resource fixed bindings.

## Where bindless can help (in this codebase)
1. **Material-heavy passes** (already using bindless):
   - tri/toon/water benefit from less CPU binding as material counts grow.
2. **If you add more material texture slots** (roughness/metalness/normal/ao/emissive, etc.), bindless scales cleanly.
3. **If you add GPU-driven material indexing** (per-instance), bindless avoids per-object set updates.

## Where bindless won’t help (and why)
- **Single-input postprocessing** (postprocess/tonemap/dof): only 1–2 textures per pass.
- **Procedural shaders** (sky, terrain.frag, grass.frag): no textures.
- **Compute buffer-only passes** (cull.comp): no textures.
- **Fixed shared textures** (terrain/grass heightmaps): no per-draw variance.

## Practical takeaway
Bindless is **best** for material-driven object rendering where textures vary per draw. It is **not** a blanket performance win across all shaders. In this repo, bindless already covers the parts that benefit most. Most other passes are either texture-free or use a tiny, fixed set of resources where bindless would add complexity without measurable gains.
