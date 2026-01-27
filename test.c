#include "external/stb/stb_image.h"

#include "external/tracy/public/tracy/TracyC.h"
#include "vk_gui.h"
#include "tinytypes.h"
#include "vk_defaults.h"
#include "vk_startup.h"
#include "vk_swapchain.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "vk_barrier.h"
#include "vk_cmd.h"
#include "vk_pipelines.h"
#include "render_object.h"
#include "vk_resources.h"
#include "bindlesstextures.h"
#include "proceduraltextures.h"
#include "debugtext.h"
#include "gpu_timer.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include "depth.h"
#include "camera.h"
#include "scene.h"
#include "file_utils.h"
#include "terrain.h"

static void recreate_hdr_target(ResourceAllocator* allocator,
                                VkDevice           device,
                                VkQueue            queue,
                                VkCommandPool      upload_pool,
                                uint32_t           width,
                                uint32_t           height,
                                Image*             image)
{
    if(image->view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, image->view, NULL);
        image->view = VK_NULL_HANDLE;
    }
    if(image->image != VK_NULL_HANDLE)
    {
        res_destroy_image(allocator, image->image, image->allocation);
        image->image      = VK_NULL_HANDLE;
        image->allocation = NULL;
    }

    VkImageCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent        = {width, height, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    res_create_image(allocator, &info, alloc_info.usage, alloc_info.flags, &image->image, &image->allocation);

    image->extent      = info.extent;
    image->format      = info.format;
    image->mipLevels   = info.mipLevels;
    image->arrayLayers = info.arrayLayers;
    image_state_reset(image);

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R16G16B16A16_SFLOAT,
        .subresourceRange =
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image->view));

    VkCommandBuffer cmd = begin_one_time_cmd(device, upload_pool);
    image_to_color(cmd, image);
    end_one_time_cmd(device, queue, upload_pool, cmd);
}
static bool g_framebuffer_resized = false;

#define render_pc(cmd, obj, T, value_ptr) render_object_push_constants((cmd), (obj), (value_ptr), sizeof(T))

static const uint32_t GRASS_GRID           = 256;
static const uint32_t GRASS_INSTANCE_COUNT = GRASS_GRID * GRASS_GRID;
static void           framebuffer_resize_callback(GLFWwindow* window, int width, int height)
{
    (void)window;
    (void)width;
    (void)height;
    g_framebuffer_resized = true;
}


typedef struct WaterVertex
{
    float pos[3];
    float uv[2];
} WaterVertex;
static void water_generate_grid(uint32_t grid, float size, WaterVertex** out_verts, uint32_t* out_vcount, uint32_t** out_inds, uint32_t* out_icount)
{
    uint32_t vcount = (grid + 1) * (grid + 1);
    uint32_t icount = grid * grid * 6;

    WaterVertex* verts = malloc(sizeof(WaterVertex) * vcount);
    uint32_t*    inds  = malloc(sizeof(uint32_t) * icount);

    uint32_t v = 0;
    for(uint32_t y = 0; y <= grid; y++)
    {
        for(uint32_t x = 0; x <= grid; x++)
        {
            float fx = ((float)x / grid - 0.5f) * size;
            float fy = ((float)y / grid - 0.5f) * size;

            verts[v].pos[0] = fx;
            verts[v].pos[1] = 0.0f;
            verts[v].pos[2] = fy;

            verts[v].uv[0] = (float)x / grid;
            verts[v].uv[1] = (float)y / grid;
            v++;
        }
    }

    uint32_t i = 0;
    for(uint32_t y = 0; y < grid; y++)
    {
        for(uint32_t x = 0; x < grid; x++)
        {
            uint32_t i0 = y * (grid + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + (grid + 1);
            uint32_t i3 = i2 + 1;

            inds[i++] = i0;
            inds[i++] = i2;
            inds[i++] = i1;

            inds[i++] = i1;
            inds[i++] = i2;
            inds[i++] = i3;
        }
    }

    *out_verts  = verts;
    *out_inds   = inds;
    *out_vcount = vcount;
    *out_icount = icount;
}
static inline void render_draw_indexed_mesh(VkCommandBuffer cmd, const GpuMeshBuffers* mesh)
{
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertex.buffer, offsets);
    vkCmdBindIndexBuffer(cmd, mesh->index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
}

static inline VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
    if(alignment == 0)
        return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

static inline void render_draw_indirect_count(VkCommandBuffer cmd,
                                              VkBuffer        indirect_buffer,
                                              VkDeviceSize    indirect_offset,
                                              VkBuffer        count_buffer,
                                              VkDeviceSize    count_offset,
                                              uint32_t        draw_count)
{
    vkCmdDrawIndexedIndirectCount(cmd, indirect_buffer, indirect_offset, count_buffer, count_offset, draw_count,
                                  sizeof(VkDrawIndexedIndirectCommand));
}
typedef struct GrassPC
{
    float time;
    float heightScale;
    float freq;
    float worldScale;
    vec2  mapMin;
    vec2  mapMax;
    vec2  noiseOffset;
    float bladeHeight;
    float bladeWidth;
    float windStrength;
    float density;
    float farDistance;
    float pad0;
} GrassPC;
typedef struct GlobalUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
} GlobalUBO;
typedef struct RaymarchUBO
{
    float resolution[2];
    float time;
    float pad;
} RaymarchUBO;
typedef struct
{
    VkSemaphore image_available_semaphore;
    VkFence     in_flight_fence;
} FrameSync;

typedef struct Vertex
{
    float pos[2];
    float col[3];
} Vertex;


typedef struct InstanceData
{
    float pos[3];
    float rot[4];  // quat (x,y,z,w)
    float scale;
} InstanceData;
typedef struct MeshDrawCommand
{
    uint32_t drawId;
} MeshDrawCommand;

typedef struct MeshDrawGpu
{
    float    position_scale[4];
    float    orientation[4];
    uint32_t meshIndex;
    uint32_t postPass;
    uint32_t materialIndex;
    uint32_t pad;
} MeshDrawGpu;

typedef struct MeshLodGpu
{
    uint32_t indexOffset;
    uint32_t indexCount;
    float    error;
    float    pad;
} MeshLodGpu;

typedef struct MeshGpu
{
    float      center_radius[4];
    uint32_t   vertexOffset;
    uint32_t   vertexCount;
    uint32_t   lodCount;
    uint32_t   pad;
    MeshLodGpu lods[SCENE_MAX_LODS];
} MeshGpu;

typedef struct CullDataGpu
{
    mat4     view;
    vec4     frustum;    // x=1, y=tanHalfX, z=1, w=tanHalfY
    vec4     params;     // x=znear, y=zfar, z=lodTargetPx, w=viewportHeight
    uint32_t counts[4];  // x=drawCount, y=lodEnabled
} CullDataGpu;

typedef struct MaterialGpu
{
    uint32_t textures[4];
    float    diffuseFactor[4];
    float    specularFactor[4];
    float    emissiveFactor[4];
} MaterialGpu;

typedef struct WaterMaterialGpu
{
    float    shallow_color[4];
    float    deep_color[4];
    float    foam_color[4];
    float    params0[4];  // x=tiling, y=foamTiling, z=normalSpeed1, w=foamStrength
    float    params1[4];  // x=normalSpeed2, y=normalScale2, z=colorVariation, w=distortion
    uint32_t textures[4];
} WaterMaterialGpu;

typedef struct WaterInstanceGpu
{
    mat4     model;
    uint32_t material_index;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} WaterInstanceGpu;


typedef struct WaterPC
{
    float time;
    float opacity;

    float normalScale;
    float foamStrength;

    float specular;
    float fresnelPower;
    float fresnelStrength;

    float specPower;
    float pad;  // alignment padding
    float sunDirIntensity[4];
} WaterPC;

typedef struct ToonPC
{
    vec4 light_dir_intensity;  // xyz=direction, w=intensity
    vec4 indirect_min_color;   // rgb=min color, a=multiplier
    vec4 shadow_map_color;     // rgb=shadow color, a=receiveShadowAmount
    vec4 outline_color;        // rgb=outline color
    vec4 params0;              // x=celMidPoint, y=celSoftness, z=outlineWidth, w=outlineZOffset
    vec4 params1;              // x=useAlphaClip, y=cutoff, z=useEmission, w=emissionMulByBase
    vec4 params2;              // x=useOcclusion, y=occlusionStrength, z=occlusionRemapStart, w=occlusionRemapEnd
    vec4 params3;              // x=isFace, y=outlineZOffsetRemapStart, z=outlineZOffsetRemapEnd, w=unused
} ToonPC;

typedef struct DOFPC
{

    float focal_distance;  // meters
    float focal_length;    // meters (50mm = 0.05f)
    float coc_scale;       // f^2 / (sensor_height * f_stop)
    float max_coc_px;      // pixels
    float z_near;          // camera near

} DOFPC;


static const Vertex TRIANGLE_VERTS[] = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
};


static void build_global_ubo(GlobalUBO* ubo, Camera* cam, float aspect)
{
    camera_build_view(ubo->view, cam);
    camera_build_proj(ubo->proj, cam, aspect);

    // Vulkan clip fix (Y flip)
    ubo->proj[1][1] *= -1.0f;

    glm_mat4_mul(ubo->proj, ubo->view, ubo->viewproj);

    glm_vec3_copy(cam->position, ubo->cameraPos);
    ubo->cameraPos[3] = 1.0f;
}


int main()
{


    // ============================================================
    // Platform / Window
    // ============================================================
    VK_CHECK(volkInitialize());

    if(!is_instance_extension_supported("VK_KHR_wayland_surface"))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    else
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);

    assert(glfwInit());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);


    glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);
    // ============================================================
    // Instance / Device setup
    // ============================================================
    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    u32          glfw_ext_count = 0;
    const char** glfw_exts      = glfwGetRequiredInstanceExtensions(&glfw_ext_count);


    renderer_context_desc desc = {
        .app_name = "My Renderer",

        .instance_layers     = NULL,
        .instance_extensions = glfw_exts,
        .device_extensions   = dev_exts,

        .instance_layer_count        = 0,
        .instance_extension_count    = glfw_ext_count,
        .device_extension_count      = 1,
        .enable_gpu_based_validation = false,
        .enable_validation           = true,

        .validation_severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .validation_types = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .use_custom_features = false  // IMPORTANT
    };


    VkDescriptorPool persistent_pool;
    VkDescriptorPool frame_pools[MAX_FRAME_IN_FLIGHT];
    VkCommandPool    upload_pool;
    renderer_context ctx = {0};
    vk_create_instance(&ctx, &desc);
    volkLoadInstanceOnly(ctx.instance);
    setup_debug_messenger(&ctx, &desc);

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(ctx.instance, window, NULL, &surface));

    VkPhysicalDevice gpu = pick_physical_device(ctx.instance, surface, &desc);


    queue_families qf = {0};
    find_queue_families(gpu, surface, &qf);

    VkDevice device = VK_NULL_HANDLE;
    create_device(gpu, surface, &desc, qf, &device);
    volkLoadDevice(device);
    init_device_queues(device, &qf);


    ResourceAllocator allocator = {0};

    VmaAllocatorCreateInfo vmaInfo = {
        .physicalDevice = gpu,
        .device         = device,
        .instance       = ctx.instance,
    };
    res_init(ctx.instance, device, gpu, &allocator, vmaInfo);
    // ============================================================
    // Per-frame sync + command buffers
    // ============================================================
    u32             current_frame = 0;
    u32             image_index   = 0;
    FrameSync       frame_sync[MAX_FRAME_IN_FLIGHT];
    VkCommandPool   cmd_pools[MAX_FRAME_IN_FLIGHT];
    VkCommandBuffer cmd_buffers[MAX_FRAME_IN_FLIGHT];
    forEach(i, MAX_FRAME_IN_FLIGHT)
    {
        vk_create_semaphore(device, &frame_sync[i].image_available_semaphore);
        vk_create_fence(device, true, &frame_sync[i].in_flight_fence);
    };
    vk_cmd_create_many_pools(device, qf.graphics_family, true, false, MAX_FRAME_IN_FLIGHT, cmd_pools);
    forEach(i, MAX_FRAME_IN_FLIGHT)
    {
        vk_cmd_alloc(device, cmd_pools[i], true, &cmd_buffers[i]);
    }

    vk_cmd_create_pool(device, qf.graphics_family, false, true, &upload_pool);

    FlowSwapchain swap = {0};

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);

    FlowSwapchainCreateInfo sci = {.surface         = surface,
                                   .width           = fb_w,
                                   .height          = fb_h,
                                   .min_image_count = 3,
                                   //        .preferred_present_mode = vk_swapchain_select_present_mode(gpu, surface, false),
                                   .preferred_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR,
                                   .preferred_format       = VK_FORMAT_B8G8R8A8_UNORM,
                                   .preferred_color_space  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                                   .extra_usage   = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                   .old_swapchain = VK_NULL_HANDLE};

    vk_create_swapchain(device, gpu, &swap, &sci, qf.graphics_queue, upload_pool);

    VkDescriptorPool imgui_pool = VK_NULL_HANDLE;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 128},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 128},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 128},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64},
        };

        VkDescriptorPoolCreateInfo pool_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets       = 1024,
            .poolSizeCount = (uint32_t)(sizeof(pool_sizes) / sizeof(pool_sizes[0])),
            .pPoolSizes    = pool_sizes,
        };

        VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &imgui_pool));
    }


    DepthTarget depth;
    VkFormat    depth_format = pick_depth_format(gpu);
    assert(depth_format != VK_FORMAT_UNDEFINED);
    create_depth_target(&allocator, &depth, swap.extent.width, swap.extent.height, depth_format);

    PipelineLayoutCache pipe_cache = {0};
    pipeline_layout_cache_init(&pipe_cache);

    DescriptorLayoutCache desc_cache = {0};
    descriptor_layout_cache_init(&desc_cache, device);

    DescriptorAllocator persistent_desc = {0};
    descriptor_allocator_init(&persistent_desc, device, false);

    DescriptorAllocator bindless_desc = {0};
    descriptor_allocator_init(&bindless_desc, device, true);  // bindless needs update-after-bind

    BindlessTextures bindless = {0};
    bindless_textures_init(&bindless, device, &bindless_desc, &desc_cache, MAX_BINDLESS_TEXTURES);

    VkGuiState gui = {0};
    vk_gui_init_state(&gui);
    VkFormat hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vk_gui_imgui_init(window, ctx.instance, gpu, device, qf.graphics_family, qf.graphics_queue, imgui_pool,
                      swap.image_count, swap.image_count, swap.format, depth_format, swap.image_usage, upload_pool);

    // -------------------------------------------------------------
    // Procedural bindless textures
    // -------------------------------------------------------------
    const uint32_t tex_w      = 256;
    const uint32_t tex_h      = 256;
    size_t         tex_size   = (size_t)tex_w * (size_t)tex_h * 4u;
    uint8_t*       tex_pixels = (uint8_t*)malloc(tex_size);

    uint32_t dummy_slot        = 0;
    uint32_t checker_slot      = 0;
    uint32_t gradient_slot     = 0;
    uint32_t black_slot        = 0;
    uint32_t water_normal_slot = 0;
    uint32_t water_foam_slot   = 0;
    uint32_t water_noise_slot  = 0;

    // dummy slot 0 (solid white)
    procedural_fill_solid_rgba8(tex_pixels, 1, 1, 255, 255, 255, 255);
    if(!tex_create_from_rgba8_cpu(&bindless, &allocator, device, qf.graphics_queue, upload_pool, 1, 1, tex_pixels, 0, &dummy_slot))
    {
        log_error("Failed to create dummy texture");
        return 1;
    }

    procedural_fill_checker_rgba8(tex_pixels, tex_w, tex_h, 16, 32, 32, 32, 220, 220, 220);
    if(!tex_create_from_rgba8_cpu(&bindless, &allocator, device, qf.graphics_queue, upload_pool, tex_w, tex_h,
                                  tex_pixels, TEX_SLOT_AUTO, &checker_slot))
    {
        log_error("Failed to create checker texture");
        return 1;
    }

    procedural_fill_gradient_rgba8(tex_pixels, tex_w, tex_h);
    if(!tex_create_from_rgba8_cpu(&bindless, &allocator, device, qf.graphics_queue, upload_pool, tex_w, tex_h,
                                  tex_pixels, TEX_SLOT_AUTO, &gradient_slot))
    {
        log_error("Failed to create gradient texture");
        return 1;
    }

    procedural_fill_solid_rgba8(tex_pixels, 1, 1, 0, 0, 0, 255);
    if(!tex_create_from_rgba8_cpu(&bindless, &allocator, device, qf.graphics_queue, upload_pool, 1, 1, tex_pixels,
                                  TEX_SLOT_AUTO, &black_slot))
    {
        log_error("Failed to create black texture");
        return 1;
    }

    free(tex_pixels);

    stbi_set_flip_vertically_on_load(1);
    if(!tex_create_from_file_rgba8(&bindless, &allocator, device, qf.graphics_queue, upload_pool,
                                   "watertextures/SmallWaves.TGA", TEX_SLOT_AUTO, &water_normal_slot))
    {
        water_normal_slot = checker_slot;
    }

    if(!tex_create_from_file_rgba8(&bindless, &allocator, device, qf.graphics_queue, upload_pool,
                                   "watertextures/Seafoam.TGA", TEX_SLOT_AUTO, &water_foam_slot))
    {
        water_foam_slot = gradient_slot;
    }

    if(!tex_create_from_file_rgba8(&bindless, &allocator, device, qf.graphics_queue, upload_pool,
                                   "watertextures/SeaPattern.TGA", TEX_SLOT_AUTO, &water_noise_slot))
    {
        water_noise_slot = checker_slot;
    }

    VkDebugText dbg = {0};
    vk_debug_text_init(&dbg, device, &persistent_desc, &desc_cache, &pipe_cache, &swap, "compiledshaders/debug_text.comp.spv");


    GraphicsPipelineConfig cfg = graphics_pipeline_config_default();
    cfg.color_attachment_count = 1;
    cfg.color_formats          = &hdr_format;
    cfg.depth_format           = depth_format;

    cfg.depth_test_enable  = true;
    cfg.depth_write_enable = true;
    cfg.reloadable         = true;

    RenderObject tri_obj           = {0};
    RenderObject toon_obj          = {0};
    RenderObject toon_outline_obj  = {0};
    RenderObject raymarch_obj      = {0};
    RenderObject terrain_obj       = {0};
    RenderObject grass_obj         = {0};
    RenderObject water_obj         = {0};
    RenderObject cull_obj          = {0};
    RenderObject terrain_paint_obj = {0};
    RenderObject dof_obj           = {0};
    RenderObject tonemap_obj       = {0};

    RenderObjectInstance toon_inst          = {0};
    RenderObjectInstance toon_outline_inst  = {0};
    RenderObjectInstance tri_inst           = {0};
    RenderObjectInstance terrain_inst       = {0};
    RenderObjectInstance grass_inst         = {0};
    RenderObjectInstance water_ro_inst      = {0};
    RenderObjectInstance cull_inst          = {0};
    RenderObjectInstance terrain_paint_inst = {0};
    RenderObjectInstance raymarch_inst      = {0};
    RenderObjectInstance dof_inst           = {0};
    RenderObjectInstance tonemap_inst       = {0};

    RenderObjectSpec tri_spec          = render_object_spec_from_config(&cfg);
    tri_spec.vert_spv                  = "compiledshaders/tri.vert.spv";
    tri_spec.frag_spv                  = "compiledshaders/tri.frag.spv";
    tri_spec.use_vertex_input          = VK_FALSE;
    tri_spec.allow_update_after_bind   = VK_TRUE;
    tri_spec.use_bindless_if_available = VK_TRUE;
    tri_spec.bindless_descriptor_count = bindless.max_textures;

    render_object_create(&tri_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &tri_spec, 1);
    render_object_set_external_set(&tri_obj, "u_textures", bindless.set);
    render_instance_create(&tri_inst, &tri_obj.pipeline, &tri_obj.resources);

    RenderObjectSpec toon_spec          = render_object_spec_from_config(&cfg);
    toon_spec.vert_spv                  = "compiledshaders/toon.vert.spv";
    toon_spec.frag_spv                  = "compiledshaders/toon.frag.spv";
    toon_spec.use_vertex_input          = VK_FALSE;
    toon_spec.allow_update_after_bind   = VK_TRUE;
    toon_spec.use_bindless_if_available = VK_TRUE;
    toon_spec.bindless_descriptor_count = bindless.max_textures;

    render_object_create(&toon_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &toon_spec, 1);
    render_object_set_external_set(&toon_obj, "u_textures", bindless.set);
    render_instance_create(&toon_inst, &toon_obj.pipeline, &toon_obj.resources);
    //
    // Front-face culling is used for toon outlines so the expanded “silhouette” pass only draws backfaces, preventing z-fighting with the main surface and keeping the outline visible around edges. If you want the outline to wrap the model, you typically render backfaces and offset/expand in the vertex shader; culling front faces ensures only the outer shell shows.
    RenderObjectSpec toon_outline_spec = toon_spec;
    toon_outline_spec.frag_spv         = "compiledshaders/toon_outline.frag.spv";
    toon_outline_spec.depth_write      = VK_FALSE;
    toon_outline_spec.cull_mode        = VK_CULL_MODE_FRONT_BIT;


    render_object_create(&toon_outline_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &toon_outline_spec, 1);
    render_object_set_external_set(&toon_outline_obj, "u_textures", bindless.set);
    render_instance_create(&toon_outline_inst, &toon_outline_obj.pipeline, &toon_outline_obj.resources);

    RenderObjectSpec cull_spec = render_object_spec_default();
    cull_spec.comp_spv         = "compiledshaders/cull.comp.spv";


    render_object_create(&cull_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &cull_spec, 1);
    render_instance_create(&cull_inst, &cull_obj.pipeline, &cull_obj.resources);
    RenderObjectSpec terrain_paint_spec = render_object_spec_default();
    terrain_paint_spec.comp_spv         = "compiledshaders/terrain_paint.comp.spv";
    render_object_create(&terrain_paint_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &terrain_paint_spec, 1);
    render_instance_create(&terrain_paint_inst, &terrain_paint_obj.pipeline, &terrain_paint_obj.resources);

    RenderObjectSpec raymarch_spec       = render_object_spec_default();
    raymarch_spec.vert_spv               = "compiledshaders/fullscreen.vert.spv";
    raymarch_spec.frag_spv               = "compiledshaders/fullscreen.frag.spv";
    raymarch_spec.front_face             = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raymarch_spec.blend_enable           = VK_TRUE;
    raymarch_spec.color_attachment_count = 1;
    raymarch_spec.color_formats          = &hdr_format;

    render_object_create(&raymarch_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &raymarch_spec, 1);
    render_instance_create(&raymarch_inst, &raymarch_obj.pipeline, &raymarch_obj.resources);


    RenderObjectSpec terrain_spec = render_object_spec_from_config(&cfg);

    terrain_spec.vert_spv         = "compiledshaders/terrain.vert.spv";
    terrain_spec.frag_spv         = "compiledshaders/terrain.frag.spv";
    terrain_spec.blend_enable     = VK_FALSE;
    terrain_spec.use_vertex_input = VK_TRUE;

    render_object_create(&terrain_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &terrain_spec, 1);
    render_object_enable_hot_reload(&terrain_obj, VK_NULL_HANDLE, &terrain_spec);
    render_instance_create(&terrain_inst, &terrain_obj.pipeline, &terrain_obj.resources);


    RenderObjectSpec grass_spec = terrain_spec;
    grass_spec.vert_spv         = "compiledshaders/grass.vert.spv";
    grass_spec.frag_spv         = "compiledshaders/grass.frag.spv";
    grass_spec.cull_mode        = VK_CULL_MODE_NONE;
    grass_spec.blend_enable     = VK_FALSE;

    render_object_create(&grass_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &grass_spec, 1);
    render_instance_create(&grass_inst, &grass_obj.pipeline, &grass_obj.resources);


    RenderObjectSpec water_spec          = render_object_spec_from_config(&cfg);
    water_spec.vert_spv                  = "compiledshaders/water.vert.spv";
    water_spec.frag_spv                  = "compiledshaders/water.frag.spv";
    water_spec.depth_write               = VK_FALSE;
    water_spec.depth_test                = VK_TRUE;
    water_spec.blend_enable              = VK_TRUE;
    water_spec.use_vertex_input          = VK_TRUE;
    water_spec.cull_mode                 = VK_CULL_MODE_BACK_BIT;
    water_spec.allow_update_after_bind   = VK_TRUE;
    water_spec.use_bindless_if_available = VK_TRUE;
    water_spec.bindless_descriptor_count = bindless.max_textures;

    render_object_create(&water_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &water_spec, 1);
    render_object_set_external_set(&water_obj, "u_textures", bindless.set);
    render_instance_create(&water_ro_inst, &water_obj.pipeline, &water_obj.resources);

    RenderObjectSpec dof_spec       = render_object_spec_from_config(&cfg);
    dof_spec.vert_spv               = "compiledshaders/dof.vert.spv";
    dof_spec.frag_spv               = "compiledshaders/dof.frag.spv";
    dof_spec.color_attachment_count = 1;
    dof_spec.color_formats          = &hdr_format;
    dof_spec.depth_test             = VK_FALSE;
    dof_spec.depth_write            = VK_FALSE;
    dof_spec.blend_enable           = VK_FALSE;
    dof_spec.per_frame_sets         = VK_TRUE;

    render_object_create(&dof_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &dof_spec, MAX_FRAME_IN_FLIGHT);
    render_instance_create(&dof_inst, &dof_obj.pipeline, &dof_obj.resources);

    RenderObjectSpec tonemap_spec       = render_object_spec_from_config(&cfg);
    tonemap_spec.vert_spv               = "compiledshaders/tonemap.vert.spv";
    tonemap_spec.frag_spv               = "compiledshaders/tonemap.frag.spv";
    tonemap_spec.color_attachment_count = 1;
    tonemap_spec.color_formats          = &swap.format;

    render_object_create(&tonemap_obj, VK_NULL_HANDLE, &desc_cache, &pipe_cache, &persistent_desc, &tonemap_spec, 1);

    render_instance_create(&tonemap_inst, &tonemap_obj.pipeline, &tonemap_obj.resources);
    BufferArena host_arena         = {0};
    BufferArena device_arena       = {0};
    BufferSlice global_ubo_buf     = {0};
    BufferSlice cull_data_buffer   = {0};
    BufferSlice raymarch_ubo       = {0};
    BufferSlice water_material_buf = {0};
    BufferSlice water_instance_buf = {0};

    BufferSlice material_buffer   = {0};
    BufferSlice mesh_buffer       = {0};
    BufferSlice draw_count_buffer = {0};

    buffer_arena_init(&allocator, 2 * 1024 * 1024, VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 256, &host_arena);

    raymarch_ubo       = buffer_arena_alloc(&host_arena, sizeof(RaymarchUBO), 256);
    global_ubo_buf     = buffer_arena_alloc(&host_arena, sizeof(GlobalUBO), 256);
    cull_data_buffer   = buffer_arena_alloc(&host_arena, sizeof(CullDataGpu), 256);
    water_material_buf = buffer_arena_alloc(&host_arena, sizeof(WaterMaterialGpu), 256);
    water_instance_buf = buffer_arena_alloc(&host_arena, sizeof(WaterInstanceGpu), 256);

    // Base heightmap (immutable after CPU bake) - procedural terrain
    Image base_height = {0};

    // Sculpt delta (mutable via compute) - starts at 0, stores user edits
    Image sculpt_delta_img = {0};

    // HDR color target for tone mapping
    Image hdr = {0};

    // HDR color target for DOF output
    Image hdr_dof = {0};

    VkSampler heightmap_sampler = VK_NULL_HANDLE;

    VkSampler tonemap_sampler = VK_NULL_HANDLE;

    VkSamplerCreateInfo samp = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };

    vkCreateSampler(device, &samp, NULL, &tonemap_sampler);
    VkTerrainGuiParams terrain_gui = {
        .height_scale   = 20.0f,
        .freq           = 0.02f,
        .noise_offset   = {0.0f, 0.0f},
        .brush_radius   = 8.0f,
        .brush_strength = 0.15f,
        .brush_hardness = 0.4f,
    };

    // Create base height texture (immutable after CPU bake - no STORAGE_BIT needed)
    VkImageCreateInfo base_height_info =
        VK_IMAGE_DEFAULT_2D(HEIGHTMAP_RES, HEIGHTMAP_RES, VK_FORMAT_R16_SFLOAT,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VmaAllocationCreateInfo heightmap_alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    res_create_image(&allocator, &base_height_info, heightmap_alloc_info.usage, heightmap_alloc_info.flags,
                     &base_height.image, &base_height.allocation);

    base_height.extent      = base_height_info.extent;
    base_height.format      = base_height_info.format;
    base_height.mipLevels   = base_height_info.mipLevels;
    base_height.arrayLayers = base_height_info.arrayLayers;
    image_state_reset(&base_height);

    VkImageViewCreateInfo base_height_view_info = VK_IMAGE_VIEW_DEFAULT(base_height.image, VK_FORMAT_R16_SFLOAT);
    VK_CHECK(vkCreateImageView(device, &base_height_view_info, NULL, &base_height.view));

    // Create sculpt delta texture (mutable via compute - needs STORAGE_BIT)
    VkImageCreateInfo sculpt_delta_info = VK_IMAGE_DEFAULT_2D(HEIGHTMAP_RES, HEIGHTMAP_RES, VK_FORMAT_R16_SFLOAT,
                                                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                                                  | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    res_create_image(&allocator, &sculpt_delta_info, heightmap_alloc_info.usage, heightmap_alloc_info.flags,
                     &sculpt_delta_img.image, &sculpt_delta_img.allocation);

    sculpt_delta_img.extent      = sculpt_delta_info.extent;
    sculpt_delta_img.format      = sculpt_delta_info.format;
    sculpt_delta_img.mipLevels   = sculpt_delta_info.mipLevels;
    sculpt_delta_img.arrayLayers = sculpt_delta_info.arrayLayers;
    image_state_reset(&sculpt_delta_img);

    VkImageViewCreateInfo sculpt_delta_view_info = VK_IMAGE_VIEW_DEFAULT(sculpt_delta_img.image, VK_FORMAT_R16_SFLOAT);
    VK_CHECK(vkCreateImageView(device, &sculpt_delta_view_info, NULL, &sculpt_delta_img.view));

    VkSamplerCreateInfo sampler_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_FALSE,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = VK_FALSE,
        .minLod                  = 0.0f,
        .maxLod                  = 0.0f,
        .borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &heightmap_sampler));
    base_height.sampler      = heightmap_sampler;
    sculpt_delta_img.sampler = heightmap_sampler;

    // ------------------------------------------------------------
    // HDR color buffer (scene renders here)
    // ------------------------------------------------------------

    uint32_t hdr_width  = 0;
    uint32_t hdr_height = 0;
    hdr_width           = swap.extent.width;
    hdr_height          = swap.extent.height;

    recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, hdr_width, hdr_height, &hdr);
    hdr.sampler = tonemap_sampler;

    recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, hdr_width, hdr_height, &hdr_dof);
    hdr_dof.sampler = tonemap_sampler;

    // Calculate terrain bounds early (needed for CPU bake)
    float terrain_half_init    = ((float)TERRAIN_GRID - 1.0f) * TERRAIN_CELL * 0.5f;
    vec2  terrain_map_min_init = {-terrain_half_init, -terrain_half_init};
    vec2  terrain_map_max_init = {terrain_half_init, terrain_half_init};

    // Clear sculpt delta to zero
    terrain_clear_heightmap(device, qf.graphics_queue, upload_pool, &sculpt_delta_img);
    printf("[TERRAIN] Sculpt delta cleared to zero\\n");

    // Load or bake base heightmap
    bool              heightmap_loaded = false;
    TerrainSaveHeader loaded_header    = {0};
    if(file_exists(TERRAIN_SAVE_PATH))
    {
        if(terrain_load_heightmap(TERRAIN_SAVE_PATH, &allocator, device, qf.graphics_queue, upload_pool, &sculpt_delta_img, &loaded_header))
        {
            terrain_gui.height_scale    = loaded_header.heightScale;
            terrain_gui.freq            = loaded_header.freq;
            terrain_gui.noise_offset[0] = loaded_header.noiseOffset[0];
            terrain_gui.noise_offset[1] = loaded_header.noiseOffset[1];
            heightmap_loaded            = true;
            printf("[TERRAIN] Loaded sculpt delta from %s\\n", TERRAIN_SAVE_PATH);
        }
    }
    if(!heightmap_loaded)
    {
        printf("[TERRAIN] No saved sculpt data found\\n");
    }

    // Always bake base terrain from procedural noise on CPU (once at startup)
    terrain_bake_base_heightmap(&allocator, device, qf.graphics_queue, upload_pool, &base_height, HEIGHTMAP_RES,
                                terrain_map_min_init[0], terrain_map_min_init[1], terrain_map_max_init[0],
                                terrain_map_max_init[1], terrain_gui.freq, terrain_gui.noise_offset[0],
                                terrain_gui.noise_offset[1], terrain_gui.height_scale);

    GpuProfiler prof[MAX_FRAME_IN_FLIGHT];
    float       cpu_frame_ms[MAX_FRAME_IN_FLIGHT] = {0};

    for(u32 i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        if(!gpu_prof_init(&prof[i], device, gpu, 256))
        {
            printf("gpu_prof_init failed\n");
            return 1;
        }
    }

    Scene scene = {0};
    typedef struct SceneEntry
    {
        const char* path;
        const char* label;
        vec3        pos;
        float       scale;
    } SceneEntry;

    SceneEntry entries[] = {

        {"/home/lk/myprojects/flow14/data/cow.glb", "Cow", {0.0f, 19.0f, 0.0f}, 1.0f},

        {"/home/lk/myprojects/flow14/data/cow.glb", "Cow", {8.0f, 0.0f, 0.0f}, 1.0f},

        {"/home/lk/myprojects/flow14/data/cow.glb", "Cow", {16.0f, 0.0f, 0.0f}, 1.0f},

    };


    uint32_t entry_count              = (uint32_t)(sizeof(entries) / sizeof(entries[0]));
    uint32_t gltf_draw_template_count = 0;


    for(uint32_t i = 0; i < entry_count; i++)
    {
        versor rot;
        glm_quat_identity(rot);

        if(!scene_load_gltf_at(&scene, entries[i].path, entries[i].pos, rot, entries[i].scale, &gltf_draw_template_count))
        {
            printf("Failed to load gltf: %s\n", entries[i].path);
            return 1;
        }

        printf("Loaded: %s at (%.2f %.2f %.2f)\n", entries[i].label, entries[i].pos[0], entries[i].pos[1], entries[i].pos[2]);
    }

    // Load grass.glb for instanced grass rendering
    Scene grass_scene = {0};
    //    if(!scene_load_gltf(&grass_scene, "/home/lk/vkutils/newestvkutil/asset/grass.glb"))
    {
        printf("Failed to load grass.glb\n");
        //       return 1;
    }
    printf("Loaded grass.glb with %u meshes, %u vertices, %u indices\n", (uint32_t)arrlen(grass_scene.geometry.meshes),
           (uint32_t)arrlen(grass_scene.geometry.vertices), (uint32_t)arrlen(grass_scene.geometry.indices));

    uint32_t  texture_count = (uint32_t)arrlen(scene.texturePaths);
    uint32_t* texture_slots = NULL;
    if(texture_count > 0)
    {
        texture_slots = (uint32_t*)malloc(sizeof(uint32_t) * texture_count);
        if(!texture_slots)
        {
            printf("Failed to allocate texture slot map\n");
            return 1;
        }

        texture_slots[0] = 0;
        for(uint32_t i = 1; i < texture_count; i++)
        {
            const char* path = scene.texturePaths[i];
            if(path && path[0] != '\0')
            {
                uint32_t slot = 0;
                if(!tex_create_from_file_rgba8(&bindless, &allocator, device, qf.graphics_queue, upload_pool, path,
                                               TEX_SLOT_AUTO, &slot))
                {
                    printf("Failed to load texture: %s\n", path);
                    slot = 0;
                }
                texture_slots[i] = slot;
            }
            else
                texture_slots[i] = 0;
        }
    }

    uint32_t     material_count = (uint32_t)arrlen(scene.materials);
    MaterialGpu* materials_gpu  = (MaterialGpu*)malloc(sizeof(MaterialGpu) * material_count);
    if(!materials_gpu)
    {
        printf("Failed to allocate material buffer\n");
        return 1;
    }

    for(uint32_t i = 0; i < material_count; i++)
    {
        Material*    src = &scene.materials[i];
        MaterialGpu* dst = &materials_gpu[i];
        memset(dst, 0, sizeof(*dst));

        if(src->albedoTexture > 0 && texture_slots)
            dst->textures[0] = texture_slots[src->albedoTexture];
        else
            dst->textures[0] = 0;

        if(src->emissiveTexture > 0 && texture_slots)
            dst->textures[1] = texture_slots[src->emissiveTexture];
        else
            dst->textures[1] = 0;

        if(src->occlusionTexture > 0 && texture_slots)
            dst->textures[2] = texture_slots[src->occlusionTexture];
        else
            dst->textures[2] = 0;

        dst->textures[3] = black_slot;

        memcpy(dst->diffuseFactor, src->diffuseFactor, sizeof(dst->diffuseFactor));
        memcpy(dst->specularFactor, src->specularFactor, sizeof(dst->specularFactor));

        dst->emissiveFactor[0] = src->emissiveFactor[0];
        dst->emissiveFactor[1] = src->emissiveFactor[1];
        dst->emissiveFactor[2] = src->emissiveFactor[2];
        dst->emissiveFactor[3] = 0.0f;
    }

    VkDeviceSize material_bytes = (VkDeviceSize)material_count * sizeof(MaterialGpu);
    free(texture_slots);


    uint32_t     draw_count               = (uint32_t)arrlen(scene.draws);
    MeshDrawGpu* draws_cpu                = malloc(sizeof(MeshDrawGpu) * draw_count);
    BufferSlice  draw_cmd_buffer          = {0};
    BufferSlice  draws_buffer             = {0};
    BufferSlice  indirect_buffer          = {0};
    Buffer       indirect_fallback_buffer = {0};
    bool         indirect_uses_fallback   = false;
    printf("scene meshes=%u vertices=%u indices=%u\n", (uint32_t)arrlen(scene.geometry.meshes),
           (uint32_t)arrlen(scene.geometry.vertices), (uint32_t)arrlen(scene.geometry.indices));
    GpuMeshBuffers gpu_scene = {0};

    VkDeviceSize vb_size = (VkDeviceSize)arrlen(scene.geometry.vertices) * sizeof(VertexPacked);
    VkDeviceSize ib_size = (VkDeviceSize)arrlen(scene.geometry.indices) * sizeof(uint32_t);
    printf("scene draws=%u vb=%llu ib=%llu\n", draw_count, (unsigned long long)vb_size, (unsigned long long)ib_size);

    res_create_buffer(&allocator, vb_size,
                      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &gpu_scene.vertex);

    res_create_buffer(&allocator, ib_size, VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &gpu_scene.index);

    // Upload data
    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, gpu_scene.vertex.buffer, 0, scene.geometry.vertices, vb_size);

    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, gpu_scene.index.buffer, 0, scene.geometry.indices, ib_size);

    gpu_scene.vertex_count = (uint32_t)arrlen(scene.geometry.vertices);
    gpu_scene.index_count  = (uint32_t)arrlen(scene.geometry.indices);

    GpuMeshBuffers water_gpu = {0};

    {
        WaterVertex* wverts = NULL;
        uint32_t*    winds  = NULL;
        uint32_t     wvc = 0, wic = 0;

        water_generate_grid(64,      // grid resolution (LOW)
                            512.0f,  // world size (big plane)
                            &wverts, &wvc, &winds, &wic);

        res_create_buffer(&allocator, sizeof(WaterVertex) * wvc, VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &water_gpu.vertex);

        res_create_buffer(&allocator, sizeof(uint32_t) * wic, VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &water_gpu.index);

        upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, water_gpu.vertex.buffer, 0, wverts,
                             sizeof(WaterVertex) * wvc);

        upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, water_gpu.index.buffer, 0, winds, sizeof(uint32_t) * wic);

        water_gpu.vertex_count = wvc;
        water_gpu.index_count  = wic;

        free(wverts);
        free(winds);
    }

    // Upload grass mesh to GPU
    GpuMeshBuffers grass_gpu_mesh = {0};
    if(arrlen(grass_scene.geometry.vertices) > 0 && arrlen(grass_scene.geometry.indices) > 0)
    {
        VkDeviceSize grass_vb_size = (VkDeviceSize)arrlen(grass_scene.geometry.vertices) * sizeof(VertexPacked);
        VkDeviceSize grass_ib_size = (VkDeviceSize)arrlen(grass_scene.geometry.indices) * sizeof(uint32_t);

        res_create_buffer(&allocator, grass_vb_size,
                          VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &grass_gpu_mesh.vertex);

        res_create_buffer(&allocator, grass_ib_size, VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &grass_gpu_mesh.index);

        upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, grass_gpu_mesh.vertex.buffer, 0,
                             grass_scene.geometry.vertices, grass_vb_size);

        upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, grass_gpu_mesh.index.buffer, 0,
                             grass_scene.geometry.indices, grass_ib_size);

        grass_gpu_mesh.vertex_count = (uint32_t)arrlen(grass_scene.geometry.vertices);
        grass_gpu_mesh.index_count  = (uint32_t)arrlen(grass_scene.geometry.indices);
        printf("Grass mesh uploaded: %u verts, %u indices\n", grass_gpu_mesh.vertex_count, grass_gpu_mesh.index_count);
    }

    if(arrlen(scene.geometry.meshes) == 0)
    {
        printf("Scene has no meshes\n");
        return 1;
    }

    uint32_t mesh_count = (uint32_t)arrlen(scene.geometry.meshes);
    MeshGpu* meshes_gpu = (MeshGpu*)malloc(sizeof(MeshGpu) * mesh_count);
    if(!meshes_gpu)
    {
        printf("Failed to allocate mesh GPU data\n");
        return 2;
    }

    for(uint32_t i = 0; i < mesh_count; i++)
    {
        Mesh*    src = &scene.geometry.meshes[i];
        MeshGpu* dst = &meshes_gpu[i];
        memset(dst, 0, sizeof(*dst));

        dst->center_radius[0] = src->center[0];
        dst->center_radius[1] = src->center[1];
        dst->center_radius[2] = src->center[2];
        dst->center_radius[3] = src->radius;

        dst->vertexOffset = src->vertexOffset;
        dst->vertexCount  = src->vertexCount;
        dst->lodCount     = src->lodCount;

        for(uint32_t li = 0; li < src->lodCount && li < SCENE_MAX_LODS; li++)
        {
            dst->lods[li].indexOffset = src->lods[li].indexOffset;
            dst->lods[li].indexCount  = src->lods[li].indexCount;
            dst->lods[li].error       = src->lods[li].error;
            dst->lods[li].pad         = 0.0f;
        }
    }

    VkDeviceSize mesh_bytes = (VkDeviceSize)mesh_count * sizeof(MeshGpu);

    for(uint32_t i = 0; i < draw_count; i++)
    {
        MeshDraw* src = &scene.draws[i];

        draws_cpu[i].position_scale[0] = src->position[0];
        draws_cpu[i].position_scale[1] = src->position[1];
        draws_cpu[i].position_scale[2] = src->position[2];
        draws_cpu[i].position_scale[3] = src->scale;

        draws_cpu[i].orientation[0] = src->orientation[0];
        draws_cpu[i].orientation[1] = src->orientation[1];
        draws_cpu[i].orientation[2] = src->orientation[2];
        draws_cpu[i].orientation[3] = src->orientation[3];

        draws_cpu[i].meshIndex     = src->meshIndex;
        draws_cpu[i].postPass      = src->postPass;
        draws_cpu[i].materialIndex = src->materialIndex;
        draws_cpu[i].pad           = 0;
    }


    VkDeviceSize draw_cmd_bytes   = (VkDeviceSize)draw_count * sizeof(MeshDrawCommand);
    VkDeviceSize draws_bytes      = (VkDeviceSize)draw_count * sizeof(MeshDrawGpu);
    VkDeviceSize draw_count_bytes = sizeof(uint32_t);
    VkDeviceSize indirect_bytes   = (VkDeviceSize)draw_count * sizeof(VkDrawIndexedIndirectCommand);

    VkDeviceSize device_arena_size = 0;
    device_arena_size              = align_up(device_arena_size, 256) + material_bytes;
    device_arena_size              = align_up(device_arena_size, 256) + mesh_bytes;
    device_arena_size              = align_up(device_arena_size, 256) + draw_count_bytes;
    device_arena_size              = align_up(device_arena_size, 256) + draw_cmd_bytes;
    device_arena_size              = align_up(device_arena_size, 256) + draws_bytes;
    device_arena_size              = align_up(device_arena_size, 256) + indirect_bytes;

    buffer_arena_init(&allocator, device_arena_size,
                      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 256, &device_arena);

    material_buffer   = buffer_arena_alloc(&device_arena, material_bytes, 256);
    mesh_buffer       = buffer_arena_alloc(&device_arena, mesh_bytes, 256);
    draw_count_buffer = buffer_arena_alloc(&device_arena, draw_count_bytes, 256);
    draw_cmd_buffer   = buffer_arena_alloc(&device_arena, draw_cmd_bytes, 256);
    draws_buffer      = buffer_arena_alloc(&device_arena, draws_bytes, 256);
    indirect_buffer   = buffer_arena_alloc(&device_arena, indirect_bytes, 256);
    if(indirect_buffer.buffer == VK_NULL_HANDLE)
    {
        VkDeviceSize fallback_bytes = indirect_bytes;
        if(fallback_bytes == 0)
            fallback_bytes = sizeof(VkDrawIndexedIndirectCommand);

        res_create_buffer(&allocator, fallback_bytes,
                          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 256, &indirect_fallback_buffer);

        indirect_buffer.buffer  = indirect_fallback_buffer.buffer;
        indirect_buffer.offset  = 0;
        indirect_buffer.size    = fallback_bytes;
        indirect_buffer.mapping = NULL;
        indirect_buffer.address = indirect_fallback_buffer.address;
        indirect_uses_fallback  = true;
    }
    MeshDrawCommand* init_cmds = malloc(draw_count * sizeof(MeshDrawCommand));
    for(uint32_t i = 0; i < draw_count; i++)
        init_cmds[i].drawId = i;

    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, draw_cmd_buffer.buffer, draw_cmd_buffer.offset,
                         init_cmds, draw_count * sizeof(MeshDrawCommand));

    free(init_cmds);


    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, draws_buffer.buffer, draws_buffer.offset, draws_cpu, draws_bytes);

    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, material_buffer.buffer, material_buffer.offset,
                         materials_gpu, material_bytes);
    upload_to_gpu_buffer(&allocator, qf.graphics_queue, upload_pool, mesh_buffer.buffer, mesh_buffer.offset, meshes_gpu, mesh_bytes);

    free(materials_gpu);
    free(meshes_gpu);
    free(draws_cpu);

    TerrainVertex* tverts  = NULL;
    uint32_t*      tinds   = NULL;
    uint32_t       tvcount = 0, ticount = 0;

    terrain_generate_grid(TERRAIN_GRID, TERRAIN_GRID, TERRAIN_CELL, &tverts, &tvcount, &tinds, &ticount);

    GpuMeshBuffers terrain_gpu = {0};
    terrain_upload_to_gpu(&allocator, device, qf.graphics_queue, upload_pool, tverts, tvcount, tinds, ticount, &terrain_gpu);

    free(tverts);
    free(tinds);

    float terrain_half    = ((float)TERRAIN_GRID - 1.0f) * TERRAIN_CELL * 0.5f;
    vec2  terrain_map_min = {-terrain_half, -terrain_half};
    vec2  terrain_map_max = {terrain_half, terrain_half};

    RenderWrite tri_writes[] = {
        RW_BUF_O("drawCommands", draw_cmd_buffer.buffer, draw_cmd_buffer.offset, draw_cmd_bytes),
        RW_BUF_O("draws", draws_buffer.buffer, draws_buffer.offset, draws_bytes),
        RW_BUF("vb", gpu_scene.vertex.buffer, vb_size),
        RW_BUF_O("g", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_BUF_O("materials_buf", material_buffer.buffer, material_buffer.offset, material_bytes),
    };
    render_object_write_static(&tri_obj, tri_writes, (uint32_t)(sizeof(tri_writes) / sizeof(tri_writes[0])));

    RenderWrite toon_writes[] = {
        RW_BUF_O("drawCommands", draw_cmd_buffer.buffer, draw_cmd_buffer.offset, draw_cmd_bytes),
        RW_BUF_O("draws", draws_buffer.buffer, draws_buffer.offset, draws_bytes),
        RW_BUF("vb", gpu_scene.vertex.buffer, vb_size),
        RW_BUF_O("g", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_BUF_O("materials_buf", material_buffer.buffer, material_buffer.offset, material_bytes),
    };
    render_object_write_static(&toon_obj, toon_writes, (uint32_t)(sizeof(toon_writes) / sizeof(toon_writes[0])));

    RenderWrite outline_writes[] = {
        RW_BUF_O("drawCommands", draw_cmd_buffer.buffer, draw_cmd_buffer.offset, draw_cmd_bytes),
        RW_BUF_O("draws", draws_buffer.buffer, draws_buffer.offset, draws_bytes),
        RW_BUF("vb", gpu_scene.vertex.buffer, vb_size),
        RW_BUF_O("g", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_BUF_O("materials_buf", material_buffer.buffer, material_buffer.offset, material_bytes),
    };
    render_object_write_static(&toon_outline_obj, outline_writes, (uint32_t)(sizeof(outline_writes) / sizeof(outline_writes[0])));

    RenderWrite terrain_writes[] = {
        RW_BUF_O("ubo", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_IMG("uBaseHeight", base_height.view, base_height.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        RW_IMG("uSculptDelta", sculpt_delta_img.view, sculpt_delta_img.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
    };
    render_object_write_static(&terrain_obj, terrain_writes, (uint32_t)(sizeof(terrain_writes) / sizeof(terrain_writes[0])));

    // Grass descriptor writes (procedural blades need only heightmaps + UBO)
    RenderWrite grass_writes[] = {
        RW_BUF_O("ubo", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_IMG("uBaseHeight", base_height.view, base_height.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        RW_IMG("uSculptDelta", sculpt_delta_img.view, sculpt_delta_img.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
    };
    render_object_write_static(&grass_obj, grass_writes, (uint32_t)(sizeof(grass_writes) / sizeof(grass_writes[0])));

    RenderWrite water_writes[] = {
        RW_BUF_O("ubo", global_ubo_buf.buffer, global_ubo_buf.offset, sizeof(GlobalUBO)),
        RW_BUF_O("mat_buf", water_material_buf.buffer, water_material_buf.offset, sizeof(WaterMaterialGpu)),
        RW_BUF_O("inst_buf", water_instance_buf.buffer, water_instance_buf.offset, sizeof(WaterInstanceGpu)),
    };
    render_object_write_static(&water_obj, water_writes, (uint32_t)(sizeof(water_writes) / sizeof(water_writes[0])));

    RenderWrite cull_writes[] = {
        RW_BUF_O("cullData", cull_data_buffer.buffer, cull_data_buffer.offset, sizeof(CullDataGpu)),
        RW_BUF_O("drawsBuf", draws_buffer.buffer, draws_buffer.offset, draws_bytes),
        RW_BUF_O("meshesBuf", mesh_buffer.buffer, mesh_buffer.offset, mesh_bytes),
        RW_BUF_O("drawCmds", draw_cmd_buffer.buffer, draw_cmd_buffer.offset, draw_cmd_bytes),
        RW_BUF_O("indirectCmds", indirect_buffer.buffer, indirect_buffer.offset,
                 (VkDeviceSize)draw_count * sizeof(VkDrawIndexedIndirectCommand)),
        RW_BUF_O("drawCount", draw_count_buffer.buffer, draw_count_buffer.offset, sizeof(uint32_t)),
    };
    render_object_write_static(&cull_obj, cull_writes, (uint32_t)(sizeof(cull_writes) / sizeof(cull_writes[0])));

    RenderWrite terrain_paint_writes[] = {
        RW_IMG("sculptDelta", sculpt_delta_img.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL),
    };
    render_object_write_static(&terrain_paint_obj, terrain_paint_writes,
                               (uint32_t)(sizeof(terrain_paint_writes) / sizeof(terrain_paint_writes[0])));

    RenderWrite raymarch_writes[] = {
        RW_BUF_O("params", raymarch_ubo.buffer, raymarch_ubo.offset, sizeof(RaymarchUBO)),
    };
    render_object_write_static(&raymarch_obj, raymarch_writes, (uint32_t)(sizeof(raymarch_writes) / sizeof(raymarch_writes[0])));

    for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        RenderWrite dof_writes[] = {
            RW_IMG("uColor", hdr.view, hdr.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            RW_IMG("uDepth", depth.view[i], tonemap_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        };
        render_object_write_frame(&dof_obj, i, dof_writes, (uint32_t)(sizeof(dof_writes) / sizeof(dof_writes[0])));
    }


    RenderWrite tonemap_writes[] = {
        RW_IMG("uColor", hdr_dof.view, hdr_dof.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
    };
    render_object_write_static(&tonemap_obj, tonemap_writes, 1);


    Camera cam = {0};
    camera_init(&cam);
    // Start above the terrain so we're not under a strictly-positive heightfield.
    cam.position[0] = 0.0f;
    cam.position[1] = 15.0f;
    cam.position[2] = 25.0f;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    double last_mx = 0.0, last_my = 0.0;
    glfwGetCursorPos(window, &last_mx, &last_my);

    srand((unsigned)time(NULL));

    bool sculpt_mode        = false;
    bool last_sculpt_toggle = false;

    // Sculpting brush parameters (Tiny Glade style)

    VkGrassGuiParams grass_gui = {
        .blade_height  = 1.4f,
        .blade_width   = 0.12f,
        .wind_strength = 0.6f,
        .density       = 0.75f,
        .far_distance  = 80.0f,
    };

    VkWaterGuiParams water_gui = {
        .enabled             = true,
        .foam_enabled        = true,
        .fresnel_enabled     = true,
        .specular_enabled    = true,
        .water_height        = 8.0f,
        .depth_fade          = 12.0f,
        .foam_distance       = 2.0f,
        .foam_scale          = 1.5f,
        .foam_speed          = 0.6f,
        .normal_scale        = 0.8f,
        .normal_scale2       = 0.5f,
        .specular            = 0.6f,
        .spec_power          = 64.0f,
        .opacity             = 0.75f,
        .tiling              = 1.5f,
        .foam_tiling         = 2.5f,
        .normal_speed        = 0.05f,
        .normal_speed2       = 0.08f,
        .foam_strength       = 1.0f,
        .fresnel_power       = 4.0f,
        .fresnel_strength    = 0.8f,
        .color_variation     = 0.25f,
        .distortion_strength = 0.35f,
        .sun_dir             = {0.3f, 1.0f, 0.2f},
        .sun_intensity       = 1.0f,
        .shallow_color       = {0.10f, 0.55f, 0.75f},
        .deep_color          = {0.03f, 0.18f, 0.30f},
        .foam_color          = {0.90f, 0.96f, 1.00f},
    };

    VkToonGuiParams toon_gui = {
        .enabled               = true,
        .light_dir             = {0.3f, 1.0f, 0.2f},
        .light_intensity       = 1.0f,
        .indirect_min_color    = {0.1f, 0.1f, 0.1f},
        .indirect_multiplier   = 1.0f,
        .shadow_color          = {1.0f, 0.825f, 0.78f},
        .receive_shadow        = 0.65f,
        .outline_color         = {0.5f, 0.5f, 0.5f},
        .outline_width         = 1.0f,
        .outline_z_offset      = 0.0001f,
        .outline_z_remap_start = 0.0f,
        .outline_z_remap_end   = 1.0f,
        .cel_mid               = -0.5f,
        .cel_soft              = 0.05f,
        .use_alpha_clip        = false,
        .cutoff                = 0.5f,
        .use_emission          = false,
        .emission_mul_by_base  = 0.0f,
        .use_occlusion         = false,
        .occlusion_strength    = 1.0f,
        .occlusion_remap_start = 0.0f,
        .occlusion_remap_end   = 1.0f,
        .is_face               = false,
    };

    VkTerrainGuiActions terrain_actions = {0};

    // Drag sculpting state
    bool  sculpt_dragging   = false;
    vec2  sculpt_anchor_xz  = {0.0f, 0.0f};  // World XZ where user clicked
    float sculpt_last_my    = 0.0f;          // Mouse Y at last drag update
    vec2  brush_hover_xz    = {0.0f, 0.0f};  // Current hover position for visualization
    bool  brush_hover_valid = false;

    bool request_save   = false;
    bool request_load   = false;
    bool request_regen  = false;
    bool last_save_key  = false;
    bool last_load_key  = false;
    bool last_regen_key = false;


    const float    lod_target  = 1.0f;  // max screen-space error in pixels
    const uint32_t lod_enabled = 1;

    while(!glfwWindowShouldClose(window))
    {
        TracyCFrameMarkStart("Frame");
        double cpu_frame_start = glfwGetTime();
        glfwPollEvents();

        render_pipeline_hot_reload_update();


        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        vk_gui_handle_input(&gui, window, &last_mx, &last_my);

        bool sculpt_toggle = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
        if(sculpt_toggle && !last_sculpt_toggle)
        {
            sculpt_mode = !sculpt_mode;
            glfwGetCursorPos(window, &last_mx, &last_my);
            sculpt_dragging = false;
            if(sculpt_mode)
                printf("[SCULPT MODE] Click+drag UP/DOWN to sculpt. ,/. = brush size, +/- = strength | L load, K save, R regenerate | TAB GUI\n");
            else
                printf("[CAMERA MODE]\n");
        }
        last_sculpt_toggle = sculpt_toggle;

        if(!gui.enabled)
            glfwSetInputMode(window, GLFW_CURSOR, sculpt_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

        vk_gui_begin_frame();
        vk_gui_draw_terrain_controls(&gui, &terrain_gui, &grass_gui, &terrain_actions, &sculpt_mode);
        vk_gui_draw_water_controls(&gui, &water_gui);
        vk_gui_draw_toon_controls(&gui, &toon_gui);
        vk_gui_draw(&gui, 0, draw_count);

        if(terrain_actions.save)
            request_save = true;
        if(terrain_actions.load)
            request_load = true;
        if(terrain_actions.regenerate)
            request_regen = true;

        terrain_actions.save       = false;
        terrain_actions.load       = false;
        terrain_actions.regenerate = false;

        ImGuiIO* io                  = igGetIO_Nil();
        bool     imgui_capture_mouse = gui.enabled && io->WantCaptureMouse;
        bool     imgui_capture_kb    = gui.enabled && io->WantCaptureKeyboard;


        // Brush adjustments (only in sculpt mode)
        if(sculpt_mode && !imgui_capture_kb)
        {
            if(glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS)  // + key
                terrain_gui.brush_strength = fminf(terrain_gui.brush_strength + 0.005f, 2.0f);
            if(glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS)
                terrain_gui.brush_strength = fmaxf(terrain_gui.brush_strength - 0.005f, 0.01f);
            if(glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)  // ]
                terrain_gui.brush_hardness = fminf(terrain_gui.brush_hardness + 0.01f, 1.0f);
            if(glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)  // [
                terrain_gui.brush_hardness = fmaxf(terrain_gui.brush_hardness - 0.01f, 0.0f);
            if(glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS)  // . (bigger brush)
                terrain_gui.brush_radius = fminf(terrain_gui.brush_radius + 0.2f, 50.0f);
            if(glfwGetKey(window, GLFW_KEY_COMMA) == GLFW_PRESS)  // , (smaller brush)
                terrain_gui.brush_radius = fmaxf(terrain_gui.brush_radius - 0.2f, 1.0f);
        }

        bool save_key  = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
        bool load_key  = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
        bool regen_key = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;

        if(save_key && !last_save_key)
            request_save = true;
        if(load_key && !last_load_key)
            request_load = true;
        if(regen_key && !last_regen_key)
            request_regen = true;

        last_save_key  = save_key;
        last_load_key  = load_key;
        last_regen_key = regen_key;

        float dx = (float)(mx - last_mx);
        float dy = (float)(my - last_my);

        last_mx = mx;
        last_my = my;

        if(!sculpt_mode && !gui.enabled)
            camera_apply_mouse(&cam, dx, dy);
        RaymarchUBO u = {.resolution = {(float)swap.extent.width, (float)swap.extent.height}, .time = (float)glfwGetTime()};
        memcpy(raymarch_ubo.mapping, &u, sizeof(u));


        float dt = 1.0f / 60.0f;  // replace with real delta time later
        if(!gui.enabled && !imgui_capture_kb)
            camera_update_keyboard(&cam, window, dt);

        GlobalUBO ubo    = {0};
        float     aspect = (float)swap.extent.width / (float)swap.extent.height;
        build_global_ubo(&ubo, &cam, aspect);

        memcpy(global_ubo_buf.mapping, &ubo, sizeof(ubo));

        WaterMaterialGpu water_mat = {0};
        water_mat.shallow_color[0] = water_gui.shallow_color[0];
        water_mat.shallow_color[1] = water_gui.shallow_color[1];
        water_mat.shallow_color[2] = water_gui.shallow_color[2];
        water_mat.shallow_color[3] = 1.0f;

        water_mat.deep_color[0] = water_gui.deep_color[0];
        water_mat.deep_color[1] = water_gui.deep_color[1];
        water_mat.deep_color[2] = water_gui.deep_color[2];
        water_mat.deep_color[3] = 1.0f;

        water_mat.foam_color[0] = water_gui.foam_color[0];
        water_mat.foam_color[1] = water_gui.foam_color[1];
        water_mat.foam_color[2] = water_gui.foam_color[2];
        water_mat.foam_color[3] = 1.0f;

        water_mat.params0[0] = water_gui.tiling;
        water_mat.params0[1] = water_gui.foam_tiling;
        water_mat.params0[2] = water_gui.normal_speed;
        water_mat.params0[3] = water_gui.foam_enabled ? water_gui.foam_strength : 0.0f;

        water_mat.params1[0] = water_gui.normal_speed2;
        water_mat.params1[1] = water_gui.normal_scale2;
        water_mat.params1[2] = water_gui.color_variation;
        water_mat.params1[3] = water_gui.distortion_strength;

        water_mat.textures[0] = water_normal_slot;
        water_mat.textures[1] = water_foam_slot;
        water_mat.textures[2] = water_noise_slot;
        water_mat.textures[3] = 0;

        WaterInstanceGpu water_inst = {0};
        glm_mat4_identity(water_inst.model);
        vec3 water_translate = {0.0f, water_gui.water_height, 0.0f};
        glm_translate(water_inst.model, water_translate);
        water_inst.material_index = 0;

        memcpy(water_material_buf.mapping, &water_mat, sizeof(water_mat));
        memcpy(water_instance_buf.mapping, &water_inst, sizeof(water_inst));

        CullDataGpu cull = {0};
        glm_mat4_copy(ubo.view, cull.view);
        float tan_half_y = tanf(cam.fov_y * 0.5f);
        float tan_half_x = tan_half_y * aspect;
        cull.frustum[0]  = 1.0f;
        cull.frustum[1]  = tan_half_x;
        cull.frustum[2]  = 1.0f;
        cull.frustum[3]  = tan_half_y;
        cull.params[0]   = cam.znear;
        cull.params[1]   = cam.zfar;
        cull.params[2]   = lod_target;
        cull.params[3]   = (float)swap.extent.height;
        cull.counts[0]   = draw_count;
        cull.counts[1]   = lod_enabled;

        memcpy(cull_data_buffer.mapping, &cull, sizeof(cull));
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);

        if(w == 0 || h == 0)
        {
            igRender();
            continue;
        }

        if(g_framebuffer_resized || w != (int)swap.extent.width || h != (int)swap.extent.height)
        {
            vkDeviceWaitIdle(device);
            vk_swapchain_recreate(device, gpu, &swap, w, h, qf.graphics_queue, upload_pool);
            ImGui_ImplVulkan_SetMinImageCount(swap.image_count);
            destroy_depth_target(&allocator, &depth);
            create_depth_target(&allocator, &depth, swap.extent.width, swap.extent.height, depth_format);
            recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width, swap.extent.height, &hdr);
            hdr.sampler = tonemap_sampler;
            recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width, swap.extent.height, &hdr_dof);
            hdr_dof.sampler = tonemap_sampler;
            for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
            {
                RenderWrite dof_resize_writes[] = {
                    RW_IMG("uColor", hdr.view, hdr.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    RW_IMG("uDepth", depth.view[i], tonemap_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                };
                render_object_write_frame(&dof_obj, i, dof_resize_writes,
                                          (uint32_t)(sizeof(dof_resize_writes) / sizeof(dof_resize_writes[0])));
            }
            RenderWrite tonemap_resize_writes[] = {
                RW_IMG("uColor", hdr_dof.view, hdr_dof.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            };
            render_object_write_static(&tonemap_obj, tonemap_resize_writes, 1);
            vk_debug_text_on_swapchain_recreated(&dbg, &persistent_desc, &desc_cache, &swap);
            g_framebuffer_resized = false;
            igRender();
            continue;
        }

        // Tiny Glade style: click to anchor, drag up/down to sculpt
        bool  mouse_down   = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool  paint_active = false;
        float sculpt_delta = 0.0f;

        // Update hover position for brush visualization
        if(sculpt_mode && !sculpt_dragging && !imgui_capture_mouse)
        {
            vec2  hover_xz;
            float terrain_y_hint = terrain_gui.height_scale * 0.5f;
            if(screen_to_world_xz_heightfield(&cam, (float)mx, (float)my, (float)w, (float)h, aspect, terrain_y_hint,
                                              terrain_map_min[0], terrain_map_min[1], terrain_map_max[0],
                                              terrain_map_max[1], terrain_gui.freq, terrain_gui.noise_offset[0],
                                              terrain_gui.noise_offset[1], terrain_gui.height_scale, hover_xz))
            {
                brush_hover_xz[0] = hover_xz[0];
                brush_hover_xz[1] = hover_xz[1];
                brush_hover_valid = true;
            }
            else
            {
                brush_hover_valid = false;
            }
        }

        if(sculpt_mode && mouse_down && !imgui_capture_mouse)
        {
            if(!sculpt_dragging)
            {
                // Start drag: pick anchor point
                vec2  anchor;
                float terrain_y_hint = terrain_gui.height_scale * 0.5f;
                if(screen_to_world_xz_heightfield(&cam, (float)mx, (float)my, (float)w, (float)h, aspect, terrain_y_hint,
                                                  terrain_map_min[0], terrain_map_min[1], terrain_map_max[0],
                                                  terrain_map_max[1], terrain_gui.freq, terrain_gui.noise_offset[0],
                                                  terrain_gui.noise_offset[1], terrain_gui.height_scale, anchor))
                {
                    if(anchor[0] >= terrain_map_min[0] - terrain_gui.brush_radius
                       && anchor[0] <= terrain_map_max[0] + terrain_gui.brush_radius
                       && anchor[1] >= terrain_map_min[1] - terrain_gui.brush_radius
                       && anchor[1] <= terrain_map_max[1] + terrain_gui.brush_radius)
                    {
                        sculpt_dragging     = true;
                        sculpt_anchor_xz[0] = anchor[0];
                        sculpt_anchor_xz[1] = anchor[1];
                        sculpt_last_my      = (float)my;
                    }
                }
            }
            else
            {
                // Continue drag: compute vertical delta
                float dy       = (sculpt_last_my - (float)my);
                sculpt_last_my = (float)my;
                sculpt_delta   = dy * terrain_gui.brush_strength;
                paint_active   = (fabsf(sculpt_delta) > 0.001f);
            }
        }
        else
        {
            sculpt_dragging = false;
        }

        bool recreate = false;
        vkWaitForFences(device, 1, &frame_sync[current_frame].in_flight_fence, VK_TRUE, UINT64_MAX);

        if(request_load)
        {
            TerrainSaveHeader hdr = {0};
            if(terrain_load_heightmap(TERRAIN_SAVE_PATH, &allocator, device, qf.graphics_queue, upload_pool, &sculpt_delta_img, &hdr))
            {
                terrain_gui.height_scale    = hdr.heightScale;
                terrain_gui.freq            = hdr.freq;
                terrain_gui.noise_offset[0] = hdr.noiseOffset[0];
                terrain_gui.noise_offset[1] = hdr.noiseOffset[1];
                printf("[TERRAIN] Loaded sculpt delta from %s\n", TERRAIN_SAVE_PATH);

                // Re-bake base terrain with new parameters
                terrain_bake_base_heightmap(&allocator, device, qf.graphics_queue, upload_pool, &base_height,
                                            HEIGHTMAP_RES, terrain_map_min[0], terrain_map_min[1], terrain_map_max[0],
                                            terrain_map_max[1], terrain_gui.freq, terrain_gui.noise_offset[0],
                                            terrain_gui.noise_offset[1], terrain_gui.height_scale);
            }
            else
            {
                printf("[TERRAIN] Failed to load %s\n", TERRAIN_SAVE_PATH);
            }
            request_load = false;
        }

        if(request_regen)
        {
            terrain_gui.noise_offset[0] = ((float)rand() / (float)RAND_MAX) * 1000.0f;
            terrain_gui.noise_offset[1] = ((float)rand() / (float)RAND_MAX) * 1000.0f;

            // Clear sculpt delta
            terrain_clear_heightmap(device, qf.graphics_queue, upload_pool, &sculpt_delta_img);

            // Re-bake base terrain with new seed
            terrain_bake_base_heightmap(&allocator, device, qf.graphics_queue, upload_pool, &base_height, HEIGHTMAP_RES,
                                        terrain_map_min[0], terrain_map_min[1], terrain_map_max[0], terrain_map_max[1],
                                        terrain_gui.freq, terrain_gui.noise_offset[0], terrain_gui.noise_offset[1],
                                        terrain_gui.height_scale);
            printf("[TERRAIN] Procedural terrain regenerated (seed %.2f, %.2f)\n", terrain_gui.noise_offset[0],
                   terrain_gui.noise_offset[1]);
            request_regen = false;
        }

        if(request_save)
        {
            TerrainSaveHeader hdr = {
                .magic       = TERRAIN_SAVE_MAGIC,
                .version     = TERRAIN_SAVE_VERSION,
                .res         = HEIGHTMAP_RES,
                .reserved    = 0,
                .mapMin      = {terrain_map_min[0], terrain_map_min[1]},
                .mapMax      = {terrain_map_max[0], terrain_map_max[1]},
                .noiseOffset = {terrain_gui.noise_offset[0], terrain_gui.noise_offset[1]},
                .heightScale = terrain_gui.height_scale,
                .freq        = terrain_gui.freq,
            };

            if(terrain_save_heightmap(TERRAIN_SAVE_PATH, &allocator, device, qf.graphics_queue, upload_pool, &sculpt_delta_img, &hdr))
            {
                printf("[TERRAIN] Saved sculpt delta to %s\n", TERRAIN_SAVE_PATH);
            }
            else
            {
                printf("[TERRAIN] Failed to save %s\n", TERRAIN_SAVE_PATH);
            }
            request_save = false;
        }


        GpuProfiler* P = &prof[current_frame];
        gpu_prof_resolve(P);
        vkResetFences(device, 1, &frame_sync[current_frame].in_flight_fence);
        /* reset EVERYTHING allocated for this frame */
        vkResetCommandPool(device, cmd_pools[current_frame], 0);
        // Acquire image
        if(!vk_swapchain_acquire(device, &swap, frame_sync[current_frame].image_available_semaphore, VK_NULL_HANDLE,
                                 UINT64_MAX, &recreate))
        {
            if(recreate)
            {
                vk_swapchain_recreate(device, gpu, &swap, w, h, qf.graphics_queue, upload_pool);
                ImGui_ImplVulkan_SetMinImageCount(swap.image_count);
                destroy_depth_target(&allocator, &depth);
                create_depth_target(&allocator, &depth, swap.extent.width, swap.extent.height, depth_format);
                recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width, swap.extent.height, &hdr);
                hdr.sampler = tonemap_sampler;
                recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width,
                                    swap.extent.height, &hdr_dof);
                hdr_dof.sampler = tonemap_sampler;
                for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
                {
                    RenderWrite dof_resize_writes[] = {
                        RW_IMG("uColor", hdr.view, hdr.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                        RW_IMG("uDepth", depth.view[i], tonemap_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    };
                    render_object_write_frame(&dof_obj, i, dof_resize_writes,
                                              (uint32_t)(sizeof(dof_resize_writes) / sizeof(dof_resize_writes[0])));
                }
                RenderWrite tonemap_resize_writes[] = {
                    RW_IMG("uColor", hdr_dof.view, hdr_dof.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                };
                render_object_write_static(&tonemap_obj, tonemap_resize_writes, 1);
                vk_debug_text_on_swapchain_recreated(&dbg, &persistent_desc, &desc_cache, &swap);
                continue;
            }
        }
        image_index = swap.current_image;

        // -------------------------------------------------------------
        // RENDER HERE using swap.images[image_index] via your FB/pipeline
        // -------------------------------------------------------------
        render_reset_state();                 // IMPORTANT
        render_pipeline_hot_reload_update();  // safe-ish now
        VkCommandBuffer cmd = cmd_buffers[current_frame];
        vk_cmd_begin(cmd, true);


        gpu_prof_begin_frame(cmd, P);
        /* transition for rendering target */

        // Transition HDR image for rendering
        if(hdr.state.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            image_to_color(cmd, &hdr);

        // Transition depth image for depth attachment usage
        IMAGE_BARRIER_IMMEDIATE(cmd, depth.image[current_frame], depth.layout[current_frame],
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT);
        depth.layout[current_frame] = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

        if(paint_active)
        {
            GPU_SCOPE(cmd, P, "terrain_paint", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            {
                if(sculpt_delta_img.state.layout != VK_IMAGE_LAYOUT_GENERAL)
                    image_to_general_compute_rw(cmd, &sculpt_delta_img);

                render_instance_bind(cmd, &terrain_paint_inst, VK_PIPELINE_BIND_POINT_COMPUTE, current_frame);

                TerrainPaintPC brush_pc = {
                    .centerXZ = {sculpt_anchor_xz[0], sculpt_anchor_xz[1]},
                    .radius   = terrain_gui.brush_radius,
                    .strength = sculpt_delta * 0.02f,  // Per-frame delta scaled to height
                    .hardness = terrain_gui.brush_hardness,
                    .pad0     = 0.0f,
                    .mapMin   = {terrain_map_min[0], terrain_map_min[1]},
                    .mapMax   = {terrain_map_max[0], terrain_map_max[1]},
                };

                render_instance_set_push_data(&terrain_paint_inst, &brush_pc, sizeof(TerrainPaintPC));
                render_instance_push(cmd, &terrain_paint_inst);

                uint32_t group_x = (HEIGHTMAP_RES + 7u) / 8u;
                uint32_t group_y = (HEIGHTMAP_RES + 7u) / 8u;
                vkCmdDispatch(cmd, group_x, group_y, 1);

                image_to_sampled(cmd, &sculpt_delta_img);
            }
        }
        else if(sculpt_delta_img.state.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            image_to_sampled(cmd, &sculpt_delta_img);
        }

        VkRenderingAttachmentInfo color_attach = {.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                  .imageView   = hdr.view,
                                                  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                  .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
                                                  .clearValue  = {.color = {.float32 = {0.05f, 0.05f, 0.08f, 1.0f}}}};
        VkRenderingAttachmentInfo depth_attach = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = depth.view[current_frame],
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.depthStencil = {0.0f, 0}},
        };


        VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                     .renderArea = {.offset = {0, 0}, .extent = {swap.extent.width, swap.extent.height}},
                                     .layerCount           = 1,
                                     .colorAttachmentCount = 1,
                                     .pColorAttachments    = &color_attach,

                                     .pDepthAttachment = &depth_attach};


        vk_cmd_set_viewport_scissor(cmd, swap.extent);
        vkCmdBeginRendering(cmd, &rendering);

        GPU_SCOPE(cmd, P, "terrain", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)
        {

            // Determine brush display position + direction visualization
            vec2  brush_display_xz  = {0.0f, 0.0f};
            float brush_active_flag = 0.0f;
            float brush_delta_vis   = 0.0f;
            if(sculpt_mode)
            {
                if(sculpt_dragging)
                {
                    brush_display_xz[0] = sculpt_anchor_xz[0];
                    brush_display_xz[1] = sculpt_anchor_xz[1];
                    brush_active_flag   = 1.0f;  // Actively sculpting
                    brush_delta_vis     = fmaxf(-1.0f, fminf(1.0f, sculpt_delta * 0.1f));
                }
                else if(brush_hover_valid)
                {
                    brush_display_xz[0] = brush_hover_xz[0];
                    brush_display_xz[1] = brush_hover_xz[1];
                    brush_active_flag   = 0.5f;  // Hovering
                }
            }

            TerrainPC pc = {
                .time        = (float)glfwGetTime(),
                .heightScale = terrain_gui.height_scale,
                .freq        = terrain_gui.freq,
                .worldScale  = 1.0f,  // leave 1 unless you want bigger terrain
                .mapMin      = {terrain_map_min[0], terrain_map_min[1]},
                .mapMax      = {terrain_map_max[0], terrain_map_max[1]},
                .noiseOffset = {terrain_gui.noise_offset[0], terrain_gui.noise_offset[1]},
                .brushXZ     = {brush_display_xz[0], brush_display_xz[1]},
                .brushRadius = terrain_gui.brush_radius,
                .brushActive = brush_active_flag,
                .brushDelta  = brush_delta_vis,
            };


            // --- TERRAIN ---
            render_instance_bind(cmd, &terrain_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
            render_instance_set_push_data(&terrain_inst, &pc, sizeof(pc));
            render_instance_push(cmd, &terrain_inst);
            render_draw_indexed_mesh(cmd, &terrain_gpu);
        }


        GPU_SCOPE(cmd, P, "grass", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)
        {

            GrassPC gpc = {
                .time         = (float)glfwGetTime(),
                .heightScale  = terrain_gui.height_scale,
                .freq         = terrain_gui.freq,
                .worldScale   = 1.0f,
                .mapMin       = {terrain_map_min[0], terrain_map_min[1]},
                .mapMax       = {terrain_map_max[0], terrain_map_max[1]},
                .noiseOffset  = {terrain_gui.noise_offset[0], terrain_gui.noise_offset[1]},
                .bladeHeight  = grass_gui.blade_height,
                .bladeWidth   = grass_gui.blade_width,
                .windStrength = grass_gui.wind_strength,
                .density      = grass_gui.density,
                .farDistance  = grass_gui.far_distance,
                .pad0         = 0.0f,
            };


            render_instance_bind(cmd, &grass_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
            render_instance_set_push_data(&grass_inst, &gpc, sizeof(gpc));
            render_instance_push(cmd, &grass_inst);
            vkCmdDraw(cmd, 6, GRASS_INSTANCE_COUNT, 0, 0);
            // if(grass_gpu_mesh.index_count > 0)
            // {
            //     VkDeviceSize offsets[] = {0};
            //     vkCmdBindVertexBuffers(cmd, 0, 1, &grass_gpu_mesh.vertex.buffer, offsets);
            //     vkCmdBindIndexBuffer(cmd, grass_gpu_mesh.index.buffer, 0, VK_INDEX_TYPE_UINT32);
            //     vkCmdDrawIndexed(cmd, grass_gpu_mesh.index_count, GRASS_INSTANCE_COUNT, 0, 0, 0);
            // }
        }

        if(water_gui.enabled)
        {


            WaterPC wpc = {
                .time    = (float)glfwGetTime(),
                .opacity = water_gui.opacity,

                .normalScale  = water_gui.normal_scale,
                .foamStrength = water_gui.foam_enabled ? water_gui.foam_strength : 0.0f,

                .specular        = water_gui.specular_enabled ? water_gui.specular : 0.0f,
                .fresnelPower    = water_gui.fresnel_power,
                .fresnelStrength = water_gui.fresnel_enabled ? water_gui.fresnel_strength : 0.0f,

                .specPower = water_gui.spec_power,
                .sunDirIntensity = {water_gui.sun_dir[0], water_gui.sun_dir[1], water_gui.sun_dir[2], water_gui.sun_intensity},
            };
            vec3  sun_dir = {wpc.sunDirIntensity[0], wpc.sunDirIntensity[1], wpc.sunDirIntensity[2]};
            float sun_len = glm_vec3_norm(sun_dir);
            if(sun_len < 1e-3f)
            {
                sun_dir[0] = 0.3f;
                sun_dir[1] = 1.0f;
                sun_dir[2] = 0.2f;
                sun_len    = glm_vec3_norm(sun_dir);
            }
            glm_vec3_scale(sun_dir, 1.0f / sun_len, sun_dir);
            wpc.sunDirIntensity[0] = sun_dir[0];
            wpc.sunDirIntensity[1] = sun_dir[1];
            wpc.sunDirIntensity[2] = sun_dir[2];


            GPU_SCOPE(cmd, P, "water", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)
            {
                render_instance_bind(cmd, &water_ro_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
                render_instance_set_push_data(&water_ro_inst, &wpc, sizeof(wpc));
                render_instance_push(cmd, &water_ro_inst);

                render_draw_indexed_mesh(cmd, &water_gpu);
            }
        }

        vkCmdEndRendering(cmd);

        // Ensure terrain/water writes are visible to subsequent passes
        IMAGE_BARRIER_IMMEDIATE(cmd, swap.images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .src_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .dst_stage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .src_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .dst_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        IMAGE_BARRIER_IMMEDIATE(cmd, depth.image[current_frame], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, .src_stage = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                .dst_stage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                .src_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                .dst_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                .aspect = VK_IMAGE_ASPECT_DEPTH_BIT);

        // UI pass without depth to avoid depth-test conflicts
        VkRenderingAttachmentInfo color_attach_ui = color_attach;
        color_attach_ui.loadOp                    = VK_ATTACHMENT_LOAD_OP_LOAD;

        VkRenderingInfo rendering_ui   = rendering;
        rendering_ui.pColorAttachments = &color_attach_ui;
        rendering_ui.pDepthAttachment  = NULL;

        VkRenderingAttachmentInfo color_attach_gfx = color_attach;
        color_attach_gfx.loadOp                    = VK_ATTACHMENT_LOAD_OP_LOAD;

        VkRenderingAttachmentInfo depth_attach_gfx = depth_attach;
        depth_attach_gfx.loadOp                    = VK_ATTACHMENT_LOAD_OP_LOAD;

        VkRenderingInfo rendering_gfx   = rendering;
        rendering_gfx.pColorAttachments = &color_attach_gfx;
        rendering_gfx.pDepthAttachment  = &depth_attach_gfx;


        // Ensure UI color writes are visible to the mesh pass
        IMAGE_BARRIER_IMMEDIATE(cmd, swap.images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .src_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .dst_stage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .src_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .dst_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        GPU_SCOPE(cmd, P, "cull", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        {
            vkCmdFillBuffer(cmd, draw_count_buffer.buffer, draw_count_buffer.offset, sizeof(uint32_t), 0);
            BUFFER_BARRIER_IMMEDIATE(cmd, draw_count_buffer.buffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

            render_instance_bind(cmd, &cull_inst, VK_PIPELINE_BIND_POINT_COMPUTE, current_frame);

            uint32_t group_count = (draw_count + 63u) / 64u;
            vkCmdDispatch(cmd, group_count, 1, 1);

            BUFFER_BARRIER_IMMEDIATE(cmd, draw_cmd_buffer.buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
            BUFFER_BARRIER_IMMEDIATE(cmd, indirect_buffer.buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
            BUFFER_BARRIER_IMMEDIATE(cmd, draw_count_buffer.buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
        }

        GPU_SCOPE(cmd, P, "gfx", VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
        {
            vkCmdBeginRendering(cmd, &rendering_gfx);


            ToonPC toon_pc = {
                .light_dir_intensity = {toon_gui.light_dir[0], toon_gui.light_dir[1], toon_gui.light_dir[2], toon_gui.light_intensity},
                .indirect_min_color = {toon_gui.indirect_min_color[0], toon_gui.indirect_min_color[1],
                                       toon_gui.indirect_min_color[2], toon_gui.indirect_multiplier},
                .shadow_map_color   = {toon_gui.shadow_color[0], toon_gui.shadow_color[1], toon_gui.shadow_color[2],
                                       toon_gui.receive_shadow},
                .outline_color = {toon_gui.outline_color[0], toon_gui.outline_color[1], toon_gui.outline_color[2], 1.0f},
                .params0 = {toon_gui.cel_mid, toon_gui.cel_soft, 0.0f, toon_gui.outline_z_offset},
                .params1 = {toon_gui.use_alpha_clip ? 1.0f : 0.0f, toon_gui.cutoff, toon_gui.use_emission ? 1.0f : 0.0f,
                            toon_gui.emission_mul_by_base},
                .params2 = {toon_gui.use_occlusion ? 1.0f : 0.0f, toon_gui.occlusion_strength,
                            toon_gui.occlusion_remap_start, toon_gui.occlusion_remap_end},
                .params3 = {toon_gui.is_face ? 1.0f : 0.0f, toon_gui.outline_z_remap_start, toon_gui.outline_z_remap_end, 0.0f},
            };

            vec3 light_dir = {toon_pc.light_dir_intensity[0], toon_pc.light_dir_intensity[1], toon_pc.light_dir_intensity[2]};
            float light_len = glm_vec3_norm(light_dir);
            if(light_len > 1e-3f)
            {
                glm_vec3_scale(light_dir, 1.0f / light_len, light_dir);
                toon_pc.light_dir_intensity[0] = light_dir[0];
                toon_pc.light_dir_intensity[1] = light_dir[1];
                toon_pc.light_dir_intensity[2] = light_dir[2];
            }

            render_instance_bind(cmd, &toon_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
            render_instance_set_push_data(&toon_inst, &toon_pc, sizeof(ToonPC));
            render_instance_push(cmd, &toon_inst);

            vkCmdBindIndexBuffer(cmd, gpu_scene.index.buffer, 0, VK_INDEX_TYPE_UINT32);

            render_draw_indirect_count(cmd, indirect_buffer.buffer, indirect_buffer.offset, draw_count_buffer.buffer,
                                       draw_count_buffer.offset, draw_count);

            toon_pc.params0[2] = toon_gui.outline_width;
            render_instance_bind(cmd, &toon_outline_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
            render_instance_set_push_data(&toon_outline_inst, &toon_pc, sizeof(ToonPC));
            render_instance_push(cmd, &toon_outline_inst);
            render_draw_indirect_count(cmd, indirect_buffer.buffer, indirect_buffer.offset, draw_count_buffer.buffer,
                                       draw_count_buffer.offset, draw_count);

            vkCmdEndRendering(cmd);
        }

        //         VkRenderingAttachmentInfo color_attach_overlay = color_attach;
        //         color_attach_overlay.loadOp                    = VK_ATTACHMENT_LOAD_OP_LOAD;
        //
        //         VkRenderingInfo rendering_no_depth   = rendering;
        //         rendering_no_depth.pColorAttachments = &color_attach_overlay;
        //         rendering_no_depth.pDepthAttachment  = NULL;
        //
        // vk_cmd_set_viewport_scissor(cmd, swap.extent);
        //         vkCmdBeginRendering(cmd, &rendering_no_depth);
        //         render_object_bind(cmd, &raymarch_obj, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);
        //         vkCmdDraw(cmd, 3, 1, 0, 0);
        //         vkCmdEndRendering(cmd);


        image_transition(cmd, &hdr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        IMAGE_BARRIER_IMMEDIATE(cmd, depth.image[current_frame], depth.layout[current_frame],
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .src_stage = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                .dst_stage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                .src_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                .dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT);
        depth.layout[current_frame] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if(hdr_dof.state.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            image_to_color(cmd, &hdr_dof);

        // -------------------------------------------------
        // DOF pass: HDR + Depth → HDR_DOF
        // -------------------------------------------------
        VkRenderingAttachmentInfo dof_color = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = hdr_dof.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
        };

        VkRenderingInfo dof_rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                         .renderArea = {.offset = {0, 0}, .extent = {swap.extent.width, swap.extent.height}},
                                         .layerCount           = 1,
                                         .colorAttachmentCount = 1,
                                         .pColorAttachments    = &dof_color,
                                         .pDepthAttachment     = NULL};

        vk_cmd_set_viewport_scissor(cmd, swap.extent);
        vkCmdBeginRendering(cmd, &dof_rendering);
        GPU_SCOPE(cmd, P, "dof", VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
        {
            render_instance_bind(cmd, &dof_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);

            DOFPC dof_pc = {

                .focal_distance = 10.0f,  // meters: good for outdoor / terrain scenes
                .focal_length   = 0.05f,  // 50mm: human-eye-ish, neutral perspective
                .coc_scale      = 0.0f,   // computed, not hardcoded (see below)
                .max_coc_px     = 8.0f,   // pixels: strong but not stupid
                .z_near         = cam.znear,
            };
            render_instance_set_push_data(&dof_inst, &dof_pc, sizeof(dof_pc));
            render_instance_push(cmd, &dof_inst);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkCmdEndRendering(cmd);

        image_transition(cmd, &hdr_dof, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        // -------------------------------------------------
        // Tone mapping pass: HDR → swapchain
        // -------------------------------------------------

        // Transition swapchain for color output
        IMAGE_BARRIER_IMMEDIATE(cmd, swap.images[image_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo tonemap_color = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = swap.image_views[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        };

        VkRenderingInfo tonemap_rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                             .renderArea = {.offset = {0, 0}, .extent = {swap.extent.width, swap.extent.height}},
                                             .layerCount           = 1,
                                             .colorAttachmentCount = 1,
                                             .pColorAttachments    = &tonemap_color,
                                             .pDepthAttachment     = NULL};

        vk_cmd_set_viewport_scissor(cmd, swap.extent);
        vkCmdBeginRendering(cmd, &tonemap_rendering);

        render_instance_bind(cmd, &tonemap_inst, VK_PIPELINE_BIND_POINT_GRAPHICS, current_frame);

        struct
        {
            float exposure;
            float gamma;
        } tonemap_pc = {.exposure = 1.0f, .gamma = 2.2f};

        render_instance_set_push_data(&tonemap_inst, &tonemap_pc, sizeof(tonemap_pc));
        render_instance_push(cmd, &tonemap_inst);

        // fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        VkRenderingAttachmentInfo imgui_color = {

            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = swap.image_views[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        };

        VkRenderingInfo imgui_rendering = {

            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .layerCount           = 1,
            .renderArea           = {0, 0, swap.extent.width, swap.extent.height},
            .colorAttachmentCount = 1,
            .pColorAttachments    = &imgui_color,
        };
        GPU_SCOPE(cmd, P, "ui", VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
        {

            vk_cmd_set_viewport_scissor(cmd, swap.extent);
            vkCmdBeginRendering(cmd, &imgui_rendering);
            vk_gui_imgui_render(&gui, cmd);
            vkCmdEndRendering(cmd);
        }
        GPU_SCOPE(cmd, P, "debug_text", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        {
            vk_debug_text_begin_frame(&dbg);

            vk_debug_text_printf(&dbg, 1, 2, 2, pack_rgba8(255, 255, 0, 255), "CPU frame: %.3f ms", cpu_frame_ms[current_frame]);

            gpu_prof_debug_text(P, &dbg, 1, 6, 2, pack_rgba8(255, 255, 0, 255), pack_rgba8(0, 255, 0, 255));

            vk_debug_text_flush(&dbg, cmd, swap.images[image_index], image_index);
        }

        IMAGE_BARRIER_IMMEDIATE(cmd, swap.images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .src_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .dst_stage  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                .src_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .dst_access = 0);
        gpu_prof_end_frame(cmd, P);
        vk_cmd_end(cmd);
        VkSemaphoreSubmitInfo wait_info   = {.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                             .semaphore = frame_sync[current_frame].image_available_semaphore,
                                             .value     = 0,
                                             .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphoreSubmitInfo signal_info = {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = swap.render_finished[image_index],
            .value     = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        VkCommandBufferSubmitInfo cmdInfo = {.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
                                             .pNext         = NULL,
                                             .commandBuffer = cmd_buffers[current_frame],
                                             .deviceMask    = 0};

        VkSubmitInfo2 submit = {.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                .waitSemaphoreInfoCount   = 1,
                                .pWaitSemaphoreInfos      = &wait_info,
                                .commandBufferInfoCount   = 1,
                                .pCommandBufferInfos      = &cmdInfo,
                                .signalSemaphoreInfoCount = 1,
                                .pSignalSemaphoreInfos    = &signal_info


        };

        VK_CHECK(vkQueueSubmit2(qf.graphics_queue, 1, &submit, frame_sync[current_frame].in_flight_fence));
        if(!vk_swapchain_present(qf.present_queue, &swap, &swap.render_finished[swap.current_image], 1, &recreate))
        {
            if(recreate)
            {

                vk_swapchain_recreate(device, gpu, &swap, w, h, qf.graphics_queue, upload_pool);
                ImGui_ImplVulkan_SetMinImageCount(swap.image_count);


                {
                    destroy_depth_target(&allocator, &depth);
                    create_depth_target(&allocator, &depth, swap.extent.width, swap.extent.height, depth_format);
                }
                recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width, swap.extent.height, &hdr);
                hdr.sampler = tonemap_sampler;
                recreate_hdr_target(&allocator, device, qf.graphics_queue, upload_pool, swap.extent.width,
                                    swap.extent.height, &hdr_dof);
                hdr_dof.sampler = tonemap_sampler;
                for(uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
                {
                    RenderWrite dof_resize_writes[] = {
                        RW_IMG("uColor", hdr.view, hdr.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                        RW_IMG("uDepth", depth.view[i], tonemap_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    };
                    render_object_write_frame(&dof_obj, i, dof_resize_writes,
                                              (uint32_t)(sizeof(dof_resize_writes) / sizeof(dof_resize_writes[0])));
                }
                RenderWrite tonemap_resize_writes[] = {
                    RW_IMG("uColor", hdr_dof.view, hdr_dof.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                };
                render_object_write_static(&tonemap_obj, tonemap_resize_writes, 1);
                vk_debug_text_on_swapchain_recreated(&dbg, &persistent_desc, &desc_cache, &swap);

                continue;
            }
        }

        cpu_frame_ms[current_frame] = (float)((glfwGetTime() - cpu_frame_start) * 1000.0);
        current_frame               = (current_frame + 1) % MAX_FRAME_IN_FLIGHT;
        TracyCFrameMarkEnd("Frame");
    }

    vkDeviceWaitIdle(device);

    TerrainSaveHeader autosave_hdr = {
        .magic       = TERRAIN_SAVE_MAGIC,
        .version     = TERRAIN_SAVE_VERSION,
        .res         = HEIGHTMAP_RES,
        .reserved    = 0,
        .mapMin      = {terrain_map_min[0], terrain_map_min[1]},
        .mapMax      = {terrain_map_max[0], terrain_map_max[1]},
        .noiseOffset = {terrain_gui.noise_offset[0], terrain_gui.noise_offset[1]},
        .heightScale = terrain_gui.height_scale,
        .freq        = terrain_gui.freq,
    };
    // Save sculpt delta (not base terrain - that gets re-baked from params)
    terrain_save_heightmap(TERRAIN_SAVE_PATH, &allocator, device, qf.graphics_queue, upload_pool, &sculpt_delta_img, &autosave_hdr);


    vk_gui_imgui_shutdown();
    if(imgui_pool)
        vkDestroyDescriptorPool(device, imgui_pool, NULL);
    vk_debug_text_destroy(&dbg);
    descriptor_allocator_destroy(&persistent_desc);
    descriptor_allocator_destroy(&bindless_desc);
    descriptor_layout_cache_destroy(&desc_cache);
    pipeline_layout_cache_destroy(device, &pipe_cache);

    bindless_textures_destroy(&bindless, &allocator, device);

    if(indirect_uses_fallback)
        res_destroy_buffer(&allocator, &indirect_fallback_buffer);

    buffer_arena_destroy(&allocator, &host_arena);
    buffer_arena_destroy(&allocator, &device_arena);
    res_destroy_buffer(&allocator, &gpu_scene.index);
    res_destroy_buffer(&allocator, &gpu_scene.vertex);
    res_destroy_buffer(&allocator, &terrain_gpu.index);
    res_destroy_buffer(&allocator, &terrain_gpu.vertex);
    if(grass_gpu_mesh.vertex.buffer)
        res_destroy_buffer(&allocator, &grass_gpu_mesh.vertex);
    if(grass_gpu_mesh.index.buffer)
        res_destroy_buffer(&allocator, &grass_gpu_mesh.index);

    if(heightmap_sampler)
        vkDestroySampler(device, heightmap_sampler, NULL);
    if(tonemap_sampler)
        vkDestroySampler(device, tonemap_sampler, NULL);
    if(hdr.view)
        vkDestroyImageView(device, hdr.view, NULL);
    if(hdr.image)
        res_destroy_image(&allocator, hdr.image, hdr.allocation);
    if(hdr_dof.view)
        vkDestroyImageView(device, hdr_dof.view, NULL);
    if(hdr_dof.image)
        res_destroy_image(&allocator, hdr_dof.image, hdr_dof.allocation);
    if(base_height.view)
        vkDestroyImageView(device, base_height.view, NULL);
    if(base_height.image)
        res_destroy_image(&allocator, base_height.image, base_height.allocation);
    if(sculpt_delta_img.view)
        vkDestroyImageView(device, sculpt_delta_img.view, NULL);
    if(sculpt_delta_img.image)
        res_destroy_image(&allocator, sculpt_delta_img.image, sculpt_delta_img.allocation);

    destroy_depth_target(&allocator, &depth);

    vk_swapchain_destroy(device, &swap);

    res_deinit(&allocator);  // <- allocator dies LAST

    for(u32 i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        gpu_prof_destroy(&prof[i]);
    }

    for(u32 i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        vkDestroyCommandPool(device, cmd_pools[i], NULL);
    }


    for(u32 i = 0; i < MAX_FRAME_IN_FLIGHT; i++)
    {
        if(frame_sync[i].image_available_semaphore)
            vkDestroySemaphore(device, frame_sync[i].image_available_semaphore, NULL);

        if(frame_sync[i].in_flight_fence)
            vkDestroyFence(device, frame_sync[i].in_flight_fence, NULL);
    }

    if(upload_pool)
        vkDestroyCommandPool(device, upload_pool, NULL);

    render_object_destroy(device, &tri_obj);
    render_object_destroy(device, &toon_obj);
    render_object_destroy(device, &toon_outline_obj);
    render_object_destroy(device, &raymarch_obj);
    render_object_destroy(device, &terrain_obj);
    render_object_destroy(device, &grass_obj);
    render_object_destroy(device, &water_obj);
    render_object_destroy(device, &cull_obj);
    render_object_destroy(device, &terrain_paint_obj);
    render_object_destroy(device, &dof_obj);
    vkDestroySurfaceKHR(ctx.instance, surface, NULL);
    vkDestroyDevice(device, NULL);

    vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debug_utils, NULL);
    vkDestroyInstance(ctx.instance, NULL);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
