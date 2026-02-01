# Postprocess descriptor updates vs bindless

## Short answer
Bindless doesn’t eliminate **all** descriptor writes. It only avoids per-draw/per-dispatch rebinding **when the shader actually uses a bindless array**. The postprocess pass still uses **explicit named bindings** for `inputImage`, `outputImage`, and `linearSampler`, so those descriptors must be written when their backing resources change.

## What “bindless” means here
Bindless in this renderer refers to a descriptor set that exposes large arrays of resources (e.g., `u_textures`) and lets shaders index them dynamically. That removes the need to re-bind textures for each draw because the shader reads `u_textures[index]` directly.

## Why the postprocess pass still needs writes
The postprocess compute shader is **not using the bindless array** for its main inputs. It has separate, named bindings:
- `inputImage` (HDR color target)
- `outputImage` (current swapchain image)
- `linearSampler`

These bindings are **ordinary descriptors**, so the driver has to be told which image view and sampler are connected to each binding. That mapping is what `render_object_write_frame()` does.

## Why we still update only when needed
Even though we must write those descriptors, we can avoid doing it every frame:
- `outputImage` changes with `image_index` (swapchain image).
- `inputImage` can change if HDR target is recreated.
- `linearSampler` can change if the sampler is rebuilt.

So we cache the last-written views/samplers **per frame** and only re-write when any of those values differ. That keeps CPU overhead low while remaining correct.

## If you want to make it fully bindless
You can change the postprocess shader to read from a bindless image array and pass indices via push constants. Then you’d avoid per-frame descriptor writes entirely (except when the bindless set itself changes). That would require:
- Adding the bindless set to the postprocess pipeline layout.
- Updating the shader to use `u_textures[index]` or a bindless storage image array.
- Ensuring the image is in a bindless-capable descriptor set with update-after-bind enabled.

## Summary
Bindless helps when the shader **uses bindless bindings**. The postprocess shader currently uses **fixed bindings**, so descriptor writes are still required. We just moved them out of the hot loop and only update on changes to reduce overhead.
