# Slang Shader Support Plan

## Executive Summary
Add Slang shader compilation to the existing Vulkan renderer while maintaining 100% compatibility with current GLSL workflow. Slang will emit SPIR-V that works seamlessly with existing pipeline reflection, descriptor layout caching, and vertex pulling system.

## Goals
- **Primary**: Add a Slang compilation path that emits SPIR-V usable by the current Vulkan pipeline/reflection system
- **Secondary**: Keep existing GLSL workflow intact (dual-path support)
- **Tertiary**: Integrate with current shader caching and pipeline layout reflection
- **Bonus**: Enable advanced Slang features (parameter blocks, interface types) for future architectural improvements

## Scope
### In Scope
- Add Slang shader compilation to the build workflow (via `cs.sh`)
- Allow loading Slang-produced `.spv` files alongside existing SPIR-V assets
- Document naming conventions and how to add new Slang shaders
- Validate SPIR-V reflection compatibility
- Create example shaders demonstrating Slang features

### Out of Scope (for initial implementation)
- Slang runtime compilation (use offline compilation only)
- Hot-reload of Slang shaders (use existing SPIR-V hot-reload if available)
- Slang-specific debugging tools (use existing SPIR-V tools)
- Complete GLSL → Slang migration (incremental adoption)

## Assumptions
- Slang compiler (`slangc`) is available in PATH or vendored in `external/slang/`
- Slang targets Vulkan SPIR-V 1.3+ (matching `--target-env=vulkan1.3` in current cs.sh)
- Existing SPIR-V reflection in [vk_shader_reflect.c](vk_shader_reflect.c) remains compatible
- Current vertex-pulling architecture (SSBOs for vertices/indices) is preserved
- Descriptor set layout conventions (set 0 = per-frame, set 1 = per-pass, etc.) are maintained

## Proposed Workflow

### Phase 1: Tooling Setup (Day 1)
1. **Slang Compiler Installation**
   - **Option A (Recommended)**: System installation
     - Download from https://github.com/shader-slang/slang/releases
     - Install to system PATH
     - Document minimum version: 2024.14 or later
   - **Option B**: Vendored binary
     - Add to `external/slang/bin/slangc`
     - Update `.gitignore` to exclude binary if using downloaded version
     - Update `cs.sh` to use `external/slang/bin/slangc` if PATH lookup fails
   
2. **Verification**
   ```bash
   slangc --version  # Should output version >= 2024.14
   ```

### Phase 2: Build System Integration (Day 1-2)
1. **Extend `cs.sh`**
   - Add Slang compilation block after GLSL block
   - Detect `.slang` files in `shaders/` and `cooleffects/`
   - Compile to `compiledshaders/<name>.<stage>.spv`
   - Support incremental builds (check modification times)
   
2. **Slang Compilation Flags**
   ```bash
   slangc \
     -target spirv \
     -stage <vertex|fragment|compute> \
     -entry <entrypoint> \
     -fvk-use-scalar-layout \
     -O3 \
     -g0 \Implementation Changes

### 1) Update `cs.sh` - Complete Example
```bash
#!/bin/bash
# compile_shaders.sh

mkdir -p compiledshaders

# Slang compiler detection
SLANGC=""
if [ ! -z "$SLANGC_PATH" ]; then
  SLANGC="$SLANGC_PATH"
elif command -v slangc &> /dev/null; then
  SLANGC="slangc"
elif [ -f "external/slang/bin/slangc" ]; then
  SLANGC="external/slang/bin/slangc"
fi

# GLSL compilation (existing)
for shader in shaders/*.vert shaders/*.comp shaders/*.frag; do
  if [ ! -f "$shader" ]; then
    continue
  fi
  
  filename=$(basename "$shader")
  output="compiledshaders/$filename.spv"
  
  if [ "$shader" -nt "$output" ] || [ ! -f "$output" ]; then
    echo "Compiling GLSL: $shader..."
    glslc --target-env=vulkan1.3 "$shader" -o "$output"
    if [ $? -ne 0 ]; then
      echo "ERROR: GLSL compilation failed for $shader"
    fi
  fi
done

# Slang compilation (new)
if [ ! -z "$SLANGC" ]; then
  echo "Using Slang compiler: $SLANGC"
  
  # Compile single-stage Slang files
  for shader in shaders/*.vert.slang shaders/*.frag.slang shaders/*.comp.slang; do
    if [ ! -f "$shader" ]; then
      continue
    fi
    
    filename=$(basename "$shader" .slang)
    output="compiledshaders/$filename.spv"
    
    # Determine stage
    if [[ $shader == *.vert.slang ]]; then
      stage="vertex"
    elif [[ $shader == *.frag.slang ]]; then
      stage="fragment"
    elif [[ $shader == *.comp.slang ]]; then
      stage="compute"
    fi
    
    if [ "$shader" -nt "$output" ] || [ ! -f "$output" ]; then
      echo "Compiling Slang: $shader (stage=$stage)..."
      $SLANGC \
        -target spirv \
        -stage "$stage" \
        -entry main \
        -fvk-use-scalar-layout \
        -O3 \
        "$shader" \
        -o "$output"
      
      if [ $? -ne 0 ]; then
        echo "ERROR: Slang compilation failed for $shader"
      fi
    fi
  done
  
  # Compile multi-stage Slang files (advanced)
  # Example: shaders/pbr.slang with multiple entry points
  # Add custom rules here as needed
  
else
  echo "WARNING: Slang compiler not found. Skipping .slang files."
  echo "  Set SLANGC_PATH or install slangc to PATH"
fi

echo "Shader compilation complete."
```

### 2) Descriptor Compatibility Checklist
When writing Slang shaders, ensure:

**✅ DO:**
- Use explicit `[[vk::binding(X, Y)]]` for all resources
- Match existing set/binding layout from GLSL shaders
- Use `StructuredBuffer<T>` for SSBOs
- Use `ConstantBuffer<T>` for UBOs
- Use `[[vk::push_constant]]` for push constants
- Declare array dimensions explicitly

**❌ DON'T:**
- Rely on automatic binding assignment
- Use DirectX-style `register(b0)` syntax
- Mix descriptor indexing styles
- Use unbounded arrays without `[[vk::binding]]`

**Example Push Constants:**
```slang
struct PushConstants {
    uint drawID;
    uint meshIndex;
};

[[vk::push_constant]]
PushConstants push;
```

### 3) Vertex Pulling Pattern (Critical for Compatibility)
Current system uses storage buffers for geometry. Preserve this in Slang:

```slang
// Vertex attributes packed in buffers
struct VertMitigations

### Risk 1: Different Resource Binding Semantics
**Problem**: Slang may auto-assign bindings differently than GLSL
**Impact**: Descriptor set mismatches, runtime validation errors
**Mitigation**:
- Enforce explicit `[[vk::binding(X, Y)]]` on all resources
- Add validation step in `cs.sh` to check for missing bindings
- Document Criteria

### Minimum Viable Product (MVP)
- [ ] `cs.sh` successfully compiles `.slang` files into `compiledshaders/`
- [ ] Slang-generated SPIR-V passes `vk_shader_reflect.c` without errors
- [ ] At least one Slang shader renders correctly in the demo
- [ ] No regressions in existing GLSL shader pipeline
- [ ] Build time increase < 20% on incremental builds

### Full Success
- [ ] Vertex-pulling pattern works identically between GLSL and Slang
- [ ] Descriptor set layouts match byte-for-byte
- [ ] Push constants work in both shader languages
- [ ] Bindless textures accessible from Slang shaders
- [ ] Performance parity (no GPU perf difference vs GLSL)
- [ ] Documentation complete (README.md updated)

### Stretch Goals
- [ ] Hot-reload support for Slang shaders
- [ ] Slang parameter blocks integrated
- [ ] Shared utility library (common.slang)
- [ ] Automated GLSL→Slang migration tool

## Testing Plan

### Phase 1: Compilation
1. Run `cs.sh` with empty `test.slang` → no errors
2. Add minimal vertex shader → produces valid SPIR-V
3. Add fragment shader → produces valid SPIR-V
4. Verify modification time checks work (incremental build)

### Phase 2: Reflection
1. Load Slang SPIR-V in `vk_shader_reflect.c`
2. Print descriptor set layout to console
3. Compare with equivalent GLSL shader layout
4. Check for binding collisions or mismatches

### Phase 3: Rendering
1. Create simple triangle shader in Slang
2. Load in `test.c` with existing pipeline code
3. Render to screen
4. Visual validation (no artifacts, correct colors)

### Phase 4: Stress Testing
1. GPU-driven culling compute shader in Slang
2. Complex material with multiple textures
3. Vertex pulling from large mesh (>1M vertices)
4. Frame time comparison vs GLSL baseline

## Timeline & Effort Estimate

| Phase | Tasks | Time | Blocker Risk |
|-------|-------|------|--------------|
| Phase 1: Setup | Install slangc, verify version | 1 hour | Low |
| Phase 2: Build System | Update cs.sh, add Slang block | 2-4 hours | Low |
| Phase 3: Test Shader | Write slang_test.vert/frag | 2-3 hours | Medium |
| Phase 4: Validation | Reflection tests, rendering | 3-5 hours | Medium |
| Phase 5: Documentation | README, code comments | 1-2 hours | Low |
| **Total** | | **9-15 hours** | |

## Next Actions (Prioritized)

### Immediate (Do First)
1. ✅ Create this plan document
2. ⬜ Install Slang compiler (choose system vs vendored)
3. ⬜ Run `slangc --version` to verify installation
4. ⬜ Create backup of current `cs.sh` (version control checkpoint)

### Short-term (This Week)
5. ⬜ Implement Slang block in `cs.sh`
6. ⬜ Write `shaders/slang_test.vert.slang`
7. ⬜ Write `shaders/slang_test.frag.slang`
8. ⬜ Compile and check for SPIR-V output
9. ⬜ Run reflection test to validate bindings

### Medium-term (Next Week)
10. ⬜ Wire slang_test shader into demo (optional render path)
11. ⬜ Visual validation and screenshot comparison
12. ⬜ Performance profiling (GPU timer pass)
13. ⬜ Convert one production shader (candidate: `grass.slang`)

### Long-term (Ongoing)
14. ⬜ Update README.md with Slang usage guide
15. ⬜ Create `shaders/common.slang` utility library
16. ⬜ Investigate parameter blocks for material system
17. ⬜ Evaluate Slang preprocessor vs current `#include` usage

## Resources & References

### Official Documentation
- Slang GitHub: https://github.com/shader-slang/slang
- Slang User Guide: https://shader-slang.com/slang/user-guide/
- Vulkan SPIR-V spec: https://www.khronos.org/registry/SPIR-V/
- Vulkan binding model: https://www.khronos.org/registry/vulkan/specs/1.3/html/chap15.html

### Example Projects Using Slang
- Falcor (NVIDIA): https://github.com/NVIDIAGameWorks/Falcor
- Shader Playground: https://shader-playground.timjones.io/

### Related Files to Review
- [vk_shader_reflect.c](vk_shader_reflect.c) - SPIR-V reflection implementation
- [vk_pipelines.c](vk_pipelines.c) - Pipeline creation and shader loading
- [scene.h](scene.h) - GPU data structure definitions
- [shaderdata.h](shaderdata.h) - Shared CPU/GPU types

## Open Questions

1. **Q**: Should we use Slang's module system or stick to single-file shaders?
   **A**: Start with single-file for simplicity; evaluate modules later

2. **Q**: How to handle shader variants (permutations)?
   **A**: Keep using current approach initially (separate files per variant)

3. **Q**: Should we use Slang parameter blocks?
   **A**: Defer until descriptor frequency system is stable

4. **Q**: What about compute shader groups and workgroup size?
   **A**: Test with existing `cull.comp` equivalent in Slang

5. **Q**: Hot-reload support for Slang?
   **A**: Out of scope for MVP; existing SPIR-V hot-reload should work

## Appendix: Example Shader Comparison

### GLSL (existing - tri.vert)
```glsl
#version 450
layout(location = 0) out vec3 fragColor;

layout(binding = 0) readonly buffer PositionBuffer {
    vec3 positions[];
};

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

void main() {
    vec3 pos = positions[gl_VertexIndex];
    gl_Position = push.mvp * vec4(pos, 1.0);
    fragColor = pos * 0.5 + 0.5;
}
```

### Slang (equivalent - slang_tri.vert.slang)
```slang
struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[[vk::binding(0, 0)]]
StructuredBuffer<float3> positions;

[[vk::push_constant]]
cbuffer PushConstants {
    float4x4 mvp;
};

[shader("vertex")]
VSOutput main(uint vertexID : SV_VertexID) {
    float3 pos = positions[vertexID];
    VSOutput output;
    output.position = mul(mvp, float4(pos, 1.0));
    output.color = pos * 0.5 + 0.5;
    return output;
}
```

**Key Differences:**
- Slang uses struct for output vs GLSL `out` variables
- Slang uses `[[vk::binding]]` vs GLSL `layout(binding=...)`
- Slang uses `SV_VertexID` vs GLSL `gl_VertexIndex`
- Slang uses `mul(matrix, vector)` vs GLSL `matrix * vector`

Both should produce functionally identical SPIR-V

### Risk 3: Multiple Entry Points Per File
**Problem**: Slang supports multiple stages in one file; naming gets ambiguous
**Impact**: Build system complexity, maintenance burden
**Mitigation**:
- **Preferred**: Use single-stage files (`foo.vert.slang`, `foo.frag.slang`)
- **Advanced**: Document multi-entry pattern in cs.sh with explicit rules
- Keep it simple for initial implementation

### Risk 4: Reflection Incompatibilities
**Problem**: Slang SPIR-V decorations differ from glslc output
**Impact**: Pipeline layout cache misses, descriptor allocation failures
**Mitigation**:
- Run side-by-side tests (GLSL vs Slang for same shader)
- Add reflection output comparison tool
- Update `vk_shader_reflect.c` if normalization needed

### Risk 5: Buffer Layout Differences (std140 vs std430)
**Problem**: Slang defaults may not match existing GPU struct layouts
**Impact**: Data corruption, visual artifacts, crashes
**Mitigation**:
- Use `-fvk-use-scalar-layout` consistently
- Test with address sanitizer on GPU (if available)
- Document packing rules in `shaderdata.h` comments
- Cross-reference C struct sizes with shader buffer sizes

### Risk 6: Compilation Time Increase
**Problem**: Two compiler toolchains = longer build times
**Impact**: Developer iteration speed
**Mitigation**:
- Incremental builds (check modification times)
- Parallel compilation (background job for Slang)
- Cache SPIR-V in version control for release builds
struct MeshLod {
    uint indexOffset;
    uint indexCount;
    uint vertexOffset;
    uint vertexCount;
};

struct Mesh {
    MeshLod lods[4];
    uint materialIndex;
    uint lodCount;
    float boundingSphereRadius;
    float3 boundingSphereCenter;
};

// Bindings (match existing pipeline)
[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> vertices;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(2, 0)]]
StructuredBuffer<Mesh> meshes;

[[vk::push_constant]]
struct {
    uint meshIndex;
    uint lodIndex;
} push;

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    Mesh mesh = meshes[push.meshIndex];
    MeshLod lod = mesh.lods[push.lodIndex];
    
    uint idx = indices[lod.indexOffset + vertexID];
    Vertex v = vertices[lod.vertexOffset + idx];
    
    // Transform and output...
}
```

### 4) Test Shader Template
Create `shaders/slang_test.vert.slang`:
```slang
// Minimal vertex shader for validation

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[[vk::binding(0, 0)]]
StructuredBuffer<float3> positions;

[[vk::push_constant]]
cbuffer PushConstants {
    float4x4 mvp;
};

[shader("vertex")]
VSOutput main(uint vertexID : SV_VertexID) {
    float3 pos = positions[vertexID];
    VSOutput output;
    output.position = mul(mvp, float4(pos, 1.0));
    output.color = pos * 0.5 + 0.5; // Color based on position
    return output;
}
```

Create `shaders/slang_test.frag.slang`:
```slang
struct PSInput {
    float3 color : COLOR0;
};

struct PSOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input) {
    PSOutput output;
    output.color = float4(input.color, 1.0);
    return output;
}
```

### 5) Integration Points in Renderer

**No changes needed** if shader filenames match expectations:
- `vk_pipelines.c`: Already loads SPIR-V by path
- `vk_shader_reflect.c`: Works on SPIR-V (source-agnostic)
- `test.c`: Pipeline creation is data-driven

**Optional**: Add shader source tracking for debug:
```c
typedef struct {
    const char* source_file;  // "foo.glsl" or "foo.slang"
    const char* spirv_path;   // "compiledshaders/foo.vert.spv"
    ShaderStage stage;
} ShaderInfo;
```
     [[vk::binding(0, 0)]]
     ConstantBuffer<SceneData> sceneData;
     
     [[vk::binding(1, 0)]]
     StructuredBuffer<Mesh> meshes;
     ```

2. **Test Reflection Output**
   - Compile a simple Slang shader
   - Run through `vk_shader_reflect.c`
   - Verify:
     - Set/binding numbers match expected layout
     - Push constant ranges detected correctly
     - Storage buffer vs uniform buffer classification correct

3. **Current Descriptor Set Convention** (preserve this!)
   - Set 0: Per-frame data (scene globals, camera)
   - Set 1: Per-pass data (lighting, shadow maps)
   - Set 2: Material textures (bindless array)
   - Set 3: Per-draw data (if needed)

### Phase 4: Example Implementation (Day 3-4)
1. **Convert `test.slang` to Production Shader**
   - Create `shaders/slang_tri.slang` as proof-of-concept
   - Implement vertex pulling (match existing `tri.vert` behavior)
   - Example structure:
     ```slang
     struct VSInput {
         uint vertexID : SV_VertexID;
     };
     
     struct VSOutput {
         float4 position : SV_Position;
         float3 color : COLOR;
     };
     
     [[vk::binding(0, 0)]]
     StructuredBuffer<float3> positions;
     
     [[vk::binding(1, 0)]]
     StructuredBuffer<uint> indices;
     
     [shader("vertex")]
     VSOutput vertexMain(VSInput input) {
         uint idx = indices[input.vertexID];
         VSOutput output;
         output.position = float4(positions[idx], 1.0);
         output.color = float3(1.0, 1.0, 1.0);
         return output;
     }
     ```

2. **Create Matching Fragment Shader**
   - Simple pass-through or basic lighting
   - Validate descriptor compatibility

3. **Wire into `test.c`**
   - Add conditional compilation or runtime switch
   - Load `slang_tri.vert.spv` / `slang_tri.frag.spv`
   - Use existing pipeline creation path

### Phase 5: Advanced Features (Day 5+)
1. **Parameter Blocks** (optional)
   - Group related uniforms
   - Simplify descriptor set management
   
2. **Interface Types** (optional)
   - Material system abstraction
   - Shader variants without preprocessor

3. **Shader Libraries** (optional)
   - Shared utility functions
   - Import system for common code

## Detailed changes
### 1) Update `cs.sh`
- Add Slang compilation block:
  - Find `.slang` files.
  - For each file, compile each required stage/entry.
  - Emit to `compiledshaders/` with the current naming pattern.
- Keep GLSL compilation unchanged.

### 2) Optional: add `slangc` discovery helper
- Extend `build.sh` or add a small helper in `cs.sh`:
  - Prefer `$SLANGC` environment variable.
  - Fall back to `slangc` in PATH.

### 3) Validate reflection
- Run a Slang-generated shader through `vk_shader_reflect.c`.
- Ensure:
  - Descriptor set/binding numbers match expected.
  - `set` and `binding` decorations are present and stable.

### 4) Add a minimal Slang example
- Add a `shaders/slang_test.slang` with a simple vertex/fragment pair.
- Produce `compiledshaders/slang_test.vert.spv` and `compiledshaders/slang_test.frag.spv`.
- Wire it into a small optional pipeline in [test.c](test.c) for validation.

## Risks & mitigations
- **Different resource binding semantics**
  - Mitigation: enforce explicit `[[vk::binding(X, Y)]]` or `[[vk::binding]]` attributes in Slang.
- **SPIR-V version mismatch**
  - Mitigation: specify `-fvk-use-dx-layout` or appropriate flags if needed for layout compatibility.
- **Multiple entry points per file**
  - Mitigation: define a deterministic naming rule and encode stage/entry in `cs.sh`.

## Success criteria
- `cs.sh` compiles `.slang` files into `compiledshaders/`.
- A Slang shader can be used in the current pipeline setup without code changes to the renderer.
- Reflection output matches the expected set/binding layout.

## Next actions
- Confirm Slang compiler availability strategy.
- Choose a Slang naming/entry-point convention.
- Implement the `cs.sh` update and add a test shader.
