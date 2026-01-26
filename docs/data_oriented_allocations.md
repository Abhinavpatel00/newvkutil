# Data-oriented memory + descriptor writes (notes)

## 1) What the current warnings mean

The validation warnings are telling you that small buffers and images are being bound to **dedicated** allocations. That defeats suballocation and increases memory overhead + fragmentation.

Typical causes:
- Creating a VMA allocation per buffer/image without pooling.
- Missing or disabled VMA pools for small resources.
- Passing flags that force dedicated allocations.

**Goal:** suballocate small buffers/images from larger blocks, and reserve dedicated allocations only for large resources.

---

## 2) Data-oriented allocation strategy (practical)

Think in terms of **arenas** and **streaming buffers**, not per-object allocations.

### 2.1 “Big buffers, many views”
- Allocate **large SSBO/UBO arenas** (e.g., 16–128 MB).
- Suballocate ranges for meshes, materials, draw data, etc.
- Use `VkBuffer` + offset for binds (or bindless with dynamic indexing).

### 2.2 Dedicated pools (VMA)
Create **pools per usage** so allocations are batched:
- **Small device-local buffers** (suballoc): draw commands, material buffers, small SSBOs.
- **Small images** (suballoc): non-mip, small textures, LUTs.
- **Large resources** (dedicated): big textures, big buffers, render targets.

This prevents “allocation per object” churn and eliminates the warnings.

---

## 3) VMA settings to eliminate the warnings

Use VMA’s pool system. These are the levers that matter:

- **`VmaPool`** per memory usage.
- **`VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT`** only for big resources.
- **`VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT`** (or MIN_TIME for faster allocs).
- **Large block sizes** (e.g., 64–256 MB) for pools.

**Rule of thumb:**
- `< 1 MB` → pooled suballocation
- `> 1–4 MB` → dedicated if necessary

---

## 4) OffsetAllocator: when/where it shines

The OffsetAllocator in this repo is excellent for **suballocations inside a single large buffer**:

### Benefits
- Fast allocate/free by size class.
- Good for transient ring-like or frame-local allocations.
- Excellent for **GPU-driven data packing** where you need many variable-sized chunks.

### Limits
- It only manages **offsets**, not Vulkan memory binding.
- You still need a single VkBuffer/VkImage with backing memory.
- You must do your own alignment + defragmentation policy.

### Best use cases
- **Mesh/vertex/indirect arenas** inside one big device-local buffer.
- **Per-frame transient UBO/SSBO** packed into a ring buffer.
- **Descriptor-driven addressable ranges** (bindless or dynamic offsets).

### Example layout
```
BigBuffer (device-local, 128 MB)
├── Mesh data arena (OffsetAllocator)
├── Material data arena (OffsetAllocator)
├── Draw data arena (OffsetAllocator)
└── Free space
```

---

## 5) Removing the swapchain warnings

These warnings mean `vkCreateSwapchainKHR` was called before querying:
- `vkGetPhysicalDeviceSurfacePresentModesKHR`
- `vkGetPhysicalDeviceSurfaceFormatsKHR`

**Fix:** ensure those queries are done before swapchain creation. (Likely in `vk_swapchain.c`.)

---

## 6) Recommended next steps

1) Add **VMA pools** for small buffers/images.
2) Use **OffsetAllocator** for suballocations inside big GPU buffers.
3) Avoid per-object allocations for frequently-updated data.

If you want, I can wire this into the allocator code and update the swapchain path too.

---

## 7) Data-oriented allocation design (actionable)

This is a concrete layout + flow you can implement with the C99 OffsetAllocator.

### 7.1 Memory arenas (SoA-oriented)
Use a small number of **large GPU buffers** and suballocate offsets:

- **StaticArena** (device-local, rarely resized)
	- Mesh vertex/index blobs
	- Material tables
	- Static draw data

- **FrameArena[N]** (device-local, per-frame ring)
	- Per-frame uniforms/SSBOs
	- Transient draw/cull buffers

- **UploadArena** (host-visible)
	- Staging uploads
	- CPU-written transient data

Each arena owns a single VkBuffer + a suballocator (OffsetAllocator). This keeps allocations contiguous and cache-friendly.

### 7.2 Allocation records (SoA)
Track allocations in a structure-of-arrays so the CPU can stream over metadata efficiently:

- `alloc_offsets[]`
- `alloc_sizes[]`
- `alloc_arena_id[]`
- `alloc_generation[]`

This avoids per-object allocation structs and makes debug/defrag passes faster.

### 7.3 Allocation policy

- **Small (<1 MB)** → suballocate from arena
- **Large** → dedicated resource (VMA)
- **Per-frame** → ring buffer suballoc (reset per frame)

### 7.4 OffsetAllocator integration

Use the C99 port in `offset_allocator.c/h`:

1) Create one `OA_Allocator` per arena.
2) Suballocate offsets for resources.
3) Store `(buffer, offset, size)` in render data.
4) Use offsets in descriptor writes or vertex pulling.

### 7.5 Defragmentation strategy

- **StaticArena**: compact at load time only.
- **FrameArena**: no defrag (ring reset each frame).
- **UploadArena**: no defrag (discard after upload).

### 7.6 Swapchain + transient allocations

Swapchain images are naturally dedicated. Everything else should go through arenas/pools unless it exceeds the threshold.

---

## 8) C99 OffsetAllocator port (project)

The allocator is now in:
- offset_allocator.h
- offset_allocator.c

This is a straight C99 port of the original C++ algorithm. It is suitable for arena suballocation and avoids per-object Vulkan allocations.