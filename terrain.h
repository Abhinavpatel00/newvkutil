#include "vk_defaults.h"
#include "vk_barrier.h"
#include "vk_cmd.h"
#include "vk_resources.h"
#include "camera.h"
#include <math.h>

static const char*    TERRAIN_SAVE_PATH    = "terrain_heightmap.bin";
static const uint32_t TERRAIN_SAVE_MAGIC   = 0x54455252u;  // 'TERR'
static const uint32_t TERRAIN_SAVE_VERSION = 1u;


static const uint32_t TERRAIN_GRID         = 256;
static const float    TERRAIN_CELL         = 1.0f;
static const uint32_t HEIGHTMAP_RES        = 512;

typedef struct TerrainVertex
{
    vec3 pos;
    vec3 nrm;
    vec2 uv;
} TerrainVertex;

typedef struct TerrainSaveHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t res;
    uint32_t reserved;
    float    mapMin[2];
    float    mapMax[2];
    float    noiseOffset[2];
    float    heightScale;
    float    freq;
} TerrainSaveHeader;


typedef struct TerrainPC
{
    float time;
    float heightScale;
    float freq;
    float worldScale;
    vec2  mapMin;
    vec2  mapMax;
    vec2  noiseOffset;
    // Brush visualization
    vec2  brushXZ;      // Current brush world position
    float brushRadius;  // Brush radius for visualization
    float brushActive;  // 1.0 if sculpting, 0.0 otherwise
    float brushDelta;   // -1..1 for direction visualization
} TerrainPC;


typedef struct TerrainPaintPC
{
    vec2  centerXZ;
    float radius;
    float strength;
    float hardness;
    float pad0;
    vec2  mapMin;
    vec2  mapMax;
} TerrainPaintPC;

static void terrain_clear_heightmap(VkDevice device, VkQueue gfx_queue, VkCommandPool pool, Image* image)
{
    VkCommandBuffer cmd = begin_one_time_cmd(device, pool);
    image_to_transfer_dst(cmd, image);
    VkClearColorValue       clear_val   = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}};
    VkImageSubresourceRange clear_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    vkCmdClearColorImage(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_val, 1, &clear_range);
    image_to_sampled(cmd, image);
    end_one_time_cmd(device, gfx_queue, pool, cmd);
}

static bool terrain_save_heightmap(const char*              path,
                                   ResourceAllocator*       allocator,
                                   VkDevice                 device,
                                   VkQueue                  gfx_queue,
                                   VkCommandPool            pool,
                                   Image*                   image,
                                   const TerrainSaveHeader* header)
{
    VkDeviceSize data_size = (VkDeviceSize)header->res * (VkDeviceSize)header->res * sizeof(uint16_t);

    Buffer staging = {0};
    res_create_buffer(allocator, data_size, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &staging);

    VkCommandBuffer cmd = begin_one_time_cmd(device, pool);
    image_to_transfer_src(cmd, image);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {header->res, header->res, 1},
    };
    vkCmdCopyImageToBuffer(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);

    image_to_sampled(cmd, image);
    end_one_time_cmd(device, gfx_queue, pool, cmd);

    FILE* f = fopen(path, "wb");
    if(!f)
    {
        res_destroy_buffer(allocator, &staging);
        return false;
    }

    size_t wrote = fwrite(header, sizeof(*header), 1, f);
    wrote += fwrite(staging.mapping, 1, (size_t)data_size, f);
    fclose(f);

    res_destroy_buffer(allocator, &staging);

    return wrote == (1 + (size_t)data_size);
}

static bool terrain_load_heightmap(const char*        path,
                                   ResourceAllocator* allocator,
                                   VkDevice           device,
                                   VkQueue            gfx_queue,
                                   VkCommandPool      pool,
                                   Image*             image,
                                   TerrainSaveHeader* out_header)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;

    if(fread(out_header, sizeof(*out_header), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    if(out_header->magic != TERRAIN_SAVE_MAGIC || out_header->version != TERRAIN_SAVE_VERSION || out_header->res != HEIGHTMAP_RES)
    {
        fclose(f);
        return false;
    }

    VkDeviceSize data_size = (VkDeviceSize)out_header->res * (VkDeviceSize)out_header->res * sizeof(uint16_t);

    Buffer staging = {0};
    res_create_buffer(allocator, data_size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &staging);

    if(fread(staging.mapping, 1, (size_t)data_size, f) != (size_t)data_size)
    {
        fclose(f);
        res_destroy_buffer(allocator, &staging);
        return false;
    }
    fclose(f);

    VkCommandBuffer cmd = begin_one_time_cmd(device, pool);
    image_to_transfer_dst(cmd, image);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {out_header->res, out_header->res, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging.buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    image_to_sampled(cmd, image);
    end_one_time_cmd(device, gfx_queue, pool, cmd);

    res_destroy_buffer(allocator, &staging);
    return true;
}

static inline float fractf(float x)
{
    return x - floorf(x);
}

static float hash2i(int x, int y)
{
    uint32_t h = (uint32_t)(x * 374761393u + y * 668265263u);
    h          = (h ^ (h >> 13u)) * 1274126177u;
    return (h & 0x00FFFFFFu) / 16777215.0f;  // [0..1]
}

static float noise2(float x, float y)
{
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);

    float fx = fractf(x);
    float fy = fractf(y);

    float a = hash2i(ix, iy);
    float b = hash2i(ix + 1, iy);
    float c = hash2i(ix, iy + 1);
    float d = hash2i(ix + 1, iy + 1);

    // smoothstep
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
}

static float fbm2(float x, float y)
{
    float sum  = 0.0f;
    float amp  = 1.0f;
    float freq = 0.02f;

    for(int i = 0; i < 5; i++)
    {
        float n = noise2(x * freq, y * freq) * 2.0f - 1.0f;  // [-1..1]
        sum += amp * n;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

static float terrain_height(float x, float z)
{
    float h = fbm2(x, z);

    // stylize mountains a bit
    float s = fabsf(h);
    h       = (h < 0.0f ? -1.0f : 1.0f) * powf(s, 1.6f);

    return h * 8.0f;  // amplitude
}





// ------------------------------------------------------------------
// CPU terrain bake: generates base heightmap once at startup
// Uses the same noise patterns as the original procedural shader
// ------------------------------------------------------------------
static float dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void mul_mat3_vec3(const float m[9], const float v[3], float out[3])
{
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
    out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

static float cpu_dot_noise(float px, float py, float pz)
{
    static const float PHI     = 1.618033988f;
    static const float GOLD[9] = {-0.571464913f, +0.814921382f, +0.096597072f, -0.278044873f, -0.303026659f,
                                  +0.911518454f, +0.772087367f, +0.494042493f, +0.399753815f};

    float p[3] = {px, py, pz};
    float gp[3];
    mul_mat3_vec3(GOLD, p, gp);

    float phip[3] = {PHI * px, PHI * py, PHI * pz};
    float gphip[3];
    mul_mat3_vec3(GOLD, phip, gphip);

    float cos_gp[3]    = {cosf(gp[0]), cosf(gp[1]), cosf(gp[2])};
    float sin_gphip[3] = {sinf(gphip[0]), sinf(gphip[1]), sinf(gphip[2])};

    return dot3(cos_gp, sin_gphip);  // ~[-3..+3]
}

static float cpu_dot_noise11(float px, float py, float pz)
{
    float n = cpu_dot_noise(px, py, pz) * (1.0f / 3.0f);
    if(n < -1.0f)
        n = -1.0f;
    if(n > 1.0f)
        n = 1.0f;
    return n;
}

static float cpu_fbm_dot(float px, float py, float pz)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float f   = 1.0f;

    for(int i = 0; i < 5; i++)
    {
        sum += amp * cpu_dot_noise11(px * f, py * f, pz * f);
        f *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

static float cpu_ridged_fbm_dot(float px, float py, float pz)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float f   = 1.0f;

    for(int i = 0; i < 5; i++)
    {
        float n = cpu_dot_noise11(px * f, py * f, pz * f);
        n       = 1.0f - fabsf(n);
        n *= n;
        sum += amp * n;
        f *= 2.0f;
        amp *= 0.5f;
    }
    return sum;  // [0..~1]
}

static void cpu_warp_dot(float px, float py, float pz, float strength, float* out_x, float* out_y, float* out_z)
{
    float wx = cpu_fbm_dot(px + 17.1f, py + 3.2f, pz + 11.7f);
    float wy = cpu_fbm_dot(px + 5.4f, py + 19.3f, pz + 7.1f);
    float wz = cpu_fbm_dot(px + 13.7f, py + 9.2f, pz + 21.4f);
    *out_x   = px + strength * wx;
    *out_y   = py + strength * wy;
    *out_z   = pz + strength * wz;
}

static float cpu_terrain_height_procedural(float xz_x, float xz_y, float freq, float noise_offset_x, float noise_offset_y, float height_scale)
{
    float px = (xz_x + noise_offset_x) * freq;
    float py = (xz_y + noise_offset_y) * freq;
    float pz = 0.0f;

    // Apply domain warp
    float wpx, wpy, wpz;
    cpu_warp_dot(px, py, pz, 0.6f, &wpx, &wpy, &wpz);

    float h = cpu_ridged_fbm_dot(wpx, wpy, wpz);
    h       = powf(h, 1.25f);

    return h * height_scale;
}

static void terrain_bake_base_heightmap(ResourceAllocator* allocator,
                                        VkDevice           device,
                                        VkQueue            gfx_queue,
                                        VkCommandPool      pool,
                                        Image*             base_height_image,
                                        uint32_t           res,
                                        float              map_min_x,
                                        float              map_min_y,
                                        float              map_max_x,
                                        float              map_max_y,
                                        float              freq,
                                        float              noise_offset_x,
                                        float              noise_offset_y,
                                        float              height_scale)
{
    VkDeviceSize data_size = (VkDeviceSize)res * (VkDeviceSize)res * sizeof(uint16_t);

    Buffer staging = {0};
    res_create_buffer(allocator, data_size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &staging);

    uint16_t* pixels = (uint16_t*)staging.mapping;

    printf("[TERRAIN] Baking base heightmap %ux%u...\n", res, res);

    for(uint32_t y = 0; y < res; y++)
    {
        for(uint32_t x = 0; x < res; x++)
        {
            // Map pixel to world XZ (same mapping as shaders)
            float u       = ((float)x + 0.5f) / (float)res;
            float v       = ((float)y + 0.5f) / (float)res;
            float world_x = map_min_x + (map_max_x - map_min_x) * u;
            float world_z = map_min_y + (map_max_y - map_min_y) * v;

            float h = cpu_terrain_height_procedural(world_x, world_z, freq, noise_offset_x, noise_offset_y, height_scale);

            // Convert to half float (R16_SFLOAT)
            // Simple approximation for positive heights in reasonable range
            // Using bit manipulation for proper half-float conversion
            union
            {
                float    f;
                uint32_t u;
            } conv;
            conv.f        = h;
            uint32_t f32  = conv.u;
            uint32_t sign = (f32 >> 16) & 0x8000;
            int32_t  exp  = ((f32 >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (f32 >> 13) & 0x3FF;

            uint16_t h16;
            if(exp <= 0)
            {
                h16 = (uint16_t)sign;  // flush to zero
            }
            else if(exp >= 31)
            {
                h16 = (uint16_t)(sign | 0x7C00);  // infinity
            }
            else
            {
                h16 = (uint16_t)(sign | (exp << 10) | mant);
            }

            pixels[y * res + x] = h16;
        }
    }

    printf("[TERRAIN] Base heightmap baked, uploading to GPU...\n");

    VkCommandBuffer cmd = begin_one_time_cmd(device, pool);
    image_to_transfer_dst(cmd, base_height_image);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {res, res, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging.buffer, base_height_image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    image_to_sampled(cmd, base_height_image);
    end_one_time_cmd(device, gfx_queue, pool, cmd);

    res_destroy_buffer(allocator, &staging);
    printf("[TERRAIN] Base heightmap upload complete.\n");
}




static bool screen_to_world_xz_camera(const Camera* cam, float mx, float my, float width, float height, float aspect, float terrain_y, vec2 out_xz)
{
    if(width <= 0.0f || height <= 0.0f)
        return false;

    float ndc_x = (2.0f * mx / width) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / height);

    vec3 forward, right, up;
    camera_get_basis((Camera*)cam, forward, right, up);

    float tan_half_y = tanf(cam->fov_y * 0.5f);
    float tan_half_x = tan_half_y * aspect;

    vec3 rd = {forward[0], forward[1], forward[2]};
    glm_vec3_muladds(right, ndc_x * tan_half_x, rd);
    glm_vec3_muladds(up, ndc_y * tan_half_y, rd);
    glm_vec3_normalize(rd);

    vec3 ro = {cam->position[0], cam->position[1], cam->position[2]};

    if(fabsf(rd[1]) < 1e-5f)
        return false;

    // Intersect with plane y = terrain_y (approximate terrain height)
    float t = (terrain_y - ro[1]) / rd[1];
    if(t < 0.0f)
        return false;

    out_xz[0] = ro[0] + rd[0] * t;
    out_xz[1] = ro[2] + rd[2] * t;
    return true;
}

static bool screen_to_world_xz_heightfield(const Camera* cam,
                                           float         mx,
                                           float         my,
                                           float         width,
                                           float         height,
                                           float         aspect,
                                           float         terrain_y_hint,
                                           float         map_min_x,
                                           float         map_min_y,
                                           float         map_max_x,
                                           float         map_max_y,
                                           float         freq,
                                           float         noise_offset_x,
                                           float         noise_offset_y,
                                           float         height_scale,
                                           vec2          out_xz)
{
    if(width <= 0.0f || height <= 0.0f)
        return false;

    float ndc_x = (2.0f * mx / width) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / height);

    vec3 forward, right, up;
    camera_get_basis((Camera*)cam, forward, right, up);

    float tan_half_y = tanf(cam->fov_y * 0.5f);
    float tan_half_x = tan_half_y * aspect;

    vec3 rd = {forward[0], forward[1], forward[2]};
    glm_vec3_muladds(right, ndc_x * tan_half_x, rd);
    glm_vec3_muladds(up, ndc_y * tan_half_y, rd);
    glm_vec3_normalize(rd);

    vec3 ro = {cam->position[0], cam->position[1], cam->position[2]};

    if(fabsf(rd[1]) < 1e-5f)
        return false;

    // Initial guess: intersect with a horizontal plane
    float t = (terrain_y_hint - ro[1]) / rd[1];
    if(t < 0.0f)
        return false;

    // Refine with a few iterations against procedural heightfield
    for(int i = 0; i < 4; i++)
    {
        vec3 p = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
        float h = cpu_terrain_height_procedural(p[0], p[2], freq, noise_offset_x, noise_offset_y, height_scale);
        float t_new = (h - ro[1]) / rd[1];
        if(!isfinite(t_new))
            break;
        t = t_new;
    }

    vec3 p = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
    if(p[0] < map_min_x || p[0] > map_max_x || p[2] < map_min_y || p[2] > map_max_y)
        return false;

    out_xz[0] = p[0];
    out_xz[1] = p[2];
    return true;
}


static void terrain_generate_grid(uint32_t        grid_w,
                                  uint32_t        grid_h,
                                  float           cell_size,
                                  TerrainVertex** out_verts,
                                  uint32_t*       out_vcount,
                                  uint32_t**      out_inds,
                                  uint32_t*       out_icount)
{
    uint32_t vcount = grid_w * grid_h;
    uint32_t icount = (grid_w - 1) * (grid_h - 1) * 6;

    TerrainVertex* verts = (TerrainVertex*)malloc(sizeof(TerrainVertex) * vcount);
    uint32_t*      inds  = (uint32_t*)malloc(sizeof(uint32_t) * icount);

    // vertices
    for(uint32_t y = 0; y < grid_h; y++)
    {
        for(uint32_t x = 0; x < grid_w; x++)
        {
            uint32_t i = y * grid_w + x;

            float fx = ((float)x - (float)(grid_w - 1) * 0.5f) * cell_size;
            float fz = ((float)y - (float)(grid_h - 1) * 0.5f) * cell_size;
            float fy = terrain_height(fx, fz);

            glm_vec3_copy((vec3){fx, fy, fz}, verts[i].pos);
            glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, verts[i].nrm);

            verts[i].uv[0] = (float)x / (float)(grid_w - 1);
            verts[i].uv[1] = (float)y / (float)(grid_h - 1);
        }
    }

    // indices (two triangles per quad)
    uint32_t k = 0;
    for(uint32_t y = 0; y < grid_h - 1; y++)
    {
        for(uint32_t x = 0; x < grid_w - 1; x++)
        {
            uint32_t i0 = y * grid_w + x;
            uint32_t i1 = y * grid_w + (x + 1);
            uint32_t i2 = (y + 1) * grid_w + x;
            uint32_t i3 = (y + 1) * grid_w + (x + 1);

            // consistent winding
            inds[k++] = i0;
            inds[k++] = i2;
            inds[k++] = i1;
            inds[k++] = i1;
            inds[k++] = i2;
            inds[k++] = i3;
        }
    }

    // recompute normals (accumulate face normals)
    for(uint32_t i = 0; i < vcount; i++)
        glm_vec3_zero(verts[i].nrm);

    for(uint32_t i = 0; i < icount; i += 3)
    {
        uint32_t ia = inds[i + 0];
        uint32_t ib = inds[i + 1];
        uint32_t ic = inds[i + 2];

        vec3 e1, e2, fn;
        glm_vec3_sub(verts[ib].pos, verts[ia].pos, e1);
        glm_vec3_sub(verts[ic].pos, verts[ia].pos, e2);

        glm_vec3_cross(e1, e2, fn);

        glm_vec3_add(verts[ia].nrm, fn, verts[ia].nrm);
        glm_vec3_add(verts[ib].nrm, fn, verts[ib].nrm);
        glm_vec3_add(verts[ic].nrm, fn, verts[ic].nrm);
    }

    for(uint32_t i = 0; i < vcount; i++)
        glm_vec3_normalize(verts[i].nrm);

    *out_verts  = verts;
    *out_inds   = inds;
    *out_vcount = vcount;
    *out_icount = icount;
}

static void terrain_upload_to_gpu(ResourceAllocator*   allocator,
                                  VkDevice             device,
                                  VkQueue              gfx_queue,
                                  VkCommandPool        upload_pool,
                                  const TerrainVertex* verts,
                                  uint32_t             vcount,
                                  const uint32_t*      inds,
                                  uint32_t             icount,
                                  GpuMeshBuffers*      out_gpu)
{
    VkDeviceSize vb_size = (VkDeviceSize)vcount * sizeof(TerrainVertex);
    VkDeviceSize ib_size = (VkDeviceSize)icount * sizeof(uint32_t);

    res_create_buffer(allocator, vb_size, VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &out_gpu->vertex);

    res_create_buffer(allocator, ib_size, VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, 0, &out_gpu->index);

    upload_to_gpu_buffer(allocator, gfx_queue, upload_pool, out_gpu->vertex.buffer, 0, verts, vb_size);
    upload_to_gpu_buffer(allocator, gfx_queue, upload_pool, out_gpu->index.buffer, 0, inds, ib_size);

    out_gpu->vertex_count = vcount;
    out_gpu->index_count  = icount;
}


