

#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "external/meshoptimizer/src/meshoptimizer.h"


// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

 int meshopt_quantizeUnorm(float v, int N)
{
    const float scale = (float)((1u << N) - 1u);

    v = (v >= 0.0f) ? v : 0.0f;
    v = (v <= 1.0f) ? v : 1.0f;

    return (int)(v * scale + 0.5f);
}

 int meshopt_quantizeSnorm(float v, int N)
{
    const float scale = (float)((1u << (N - 1)) - 1u);
    const float round = (v >= 0.0f) ? 0.5f : -0.5f;

    v = (v >= -1.0f) ? v : -1.0f;
    v = (v <=  1.0f) ? v :  1.0f;

    return (int)(v * scale + round);
}
static char* str_dup(const char* s)
{
    if(!s) return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static char* path_join_dir_file(const char* dir, const char* file)
{
    if(!dir || !file) return NULL;

    size_t dl = strlen(dir);
    size_t fl = strlen(file);

    bool needSlash = (dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\');

    char* out = (char*)malloc(dl + fl + (needSlash ? 2 : 1));
    if(!out) return NULL;

    memcpy(out, dir, dl);

    size_t p = dl;
    if(needSlash)
        out[p++] = '/';

    memcpy(out + p, file, fl);
    out[p + fl] = 0;

    return out;
}
static char* path_dirname(const char* path)
{
    if(!path) return str_dup("");

    const char* last1 = strrchr(path, '/');
    const char* last2 = strrchr(path, '\\');
    const char* last = last1 > last2 ? last1 : last2;

    if(!last) return str_dup("");

    size_t len = (size_t)(last - path + 1);
    char* out = (char*)malloc(len + 1);
    if(!out) return NULL;

    memcpy(out, path, len);
    out[len] = 0;
    return out;
}

static char* decode_uri_to_new_string(const char* uri)
{
    if(!uri) return NULL;

    char* tmp = str_dup(uri);
    if(!tmp) return NULL;

    size_t outlen = cgltf_decode_uri(tmp);
    tmp[outlen] = 0;
    return tmp;
}

// ------------------------------------------------------------
// Packing helpers
// ------------------------------------------------------------

static uint16_t pack_f16(float x)
{
    return (uint16_t)meshopt_quantizeHalf(x);
}

static uint32_t pack_snorm_10_10_10_2(float x, float y, float z, int sign_bit)
{
    uint32_t px = (uint32_t)(meshopt_quantizeSnorm(x, 10) + 511);
    uint32_t py = (uint32_t)(meshopt_quantizeSnorm(y, 10) + 511);
    uint32_t pz = (uint32_t)(meshopt_quantizeSnorm(z, 10) + 511);

    uint32_t v = (px & 1023u) | ((py & 1023u) << 10) | ((pz & 1023u) << 20);
    if(sign_bit) v |= (1u << 30);
    return v;
}

static uint16_t pack_tangent_oct_8_8(float tx, float ty, float tz)
{
    float tsum = fabsf(tx) + fabsf(ty) + fabsf(tz);
    if(tsum < 1e-8f)
        return 0;

    float u = 0.f, v = 0.f;
    if(tz >= 0.f)
    {
        u = tx / tsum;
        v = ty / tsum;
    }
    else
    {
        u = (1.f - fabsf(ty / tsum)) * (tx >= 0.f ? 1.f : -1.f);
        v = (1.f - fabsf(tx / tsum)) * (ty >= 0.f ? 1.f : -1.f);
    }

    uint32_t pu = (uint32_t)(meshopt_quantizeSnorm(u, 8) + 127);
    uint32_t pv = (uint32_t)(meshopt_quantizeSnorm(v, 8) + 127);

    return (uint16_t)((pu & 255u) | ((pv & 255u) << 8));
}

// ------------------------------------------------------------
// Transform decomposition
// ------------------------------------------------------------

static void decompose_transform(float translation[3], float rotation[4], float scale[3], const float* transform16)
{
    float m[4][4] = {0};
    memcpy(m, transform16, 16 * sizeof(float));

    translation[0] = m[3][0];
    translation[1] = m[3][1];
    translation[2] = m[3][2];

    float det =
        m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    float sign = (det < 0.f) ? -1.f : 1.f;

    scale[0] = sqrtf(m[0][0]*m[0][0] + m[0][1]*m[0][1] + m[0][2]*m[0][2]) * sign;
    scale[1] = sqrtf(m[1][0]*m[1][0] + m[1][1]*m[1][1] + m[1][2]*m[1][2]) * sign;
    scale[2] = sqrtf(m[2][0]*m[2][0] + m[2][1]*m[2][1] + m[2][2]*m[2][2]) * sign;

    float rsx = (scale[0] == 0.f) ? 0.f : 1.f / scale[0];
    float rsy = (scale[1] == 0.f) ? 0.f : 1.f / scale[1];
    float rsz = (scale[2] == 0.f) ? 0.f : 1.f / scale[2];

    float r00 = m[0][0] * rsx, r10 = m[1][0] * rsy, r20 = m[2][0] * rsz;
    float r01 = m[0][1] * rsx, r11 = m[1][1] * rsy, r21 = m[2][1] * rsz;
    float r02 = m[0][2] * rsx, r12 = m[1][2] * rsy, r22 = m[2][2] * rsz;

    int qc = r22 < 0 ? (r00 > r11 ? 0 : 1) : (r00 < -r11 ? 2 : 3);
    float qs1 = (qc & 2) ? -1.f : 1.f;
    float qs2 = (qc & 1) ? -1.f : 1.f;
    float qs3 = ((qc - 1) & 2) ? -1.f : 1.f;

    float qt = 1.f - qs3 * r00 - qs2 * r11 - qs1 * r22;
    float qs = 0.5f / sqrtf(qt);

    rotation[qc ^ 0] = qs * qt;
    rotation[qc ^ 1] = qs * (r01 + qs1 * r10);
    rotation[qc ^ 2] = qs * (r20 + qs2 * r02);
    rotation[qc ^ 3] = qs * (r12 + qs3 * r21);
}

// ------------------------------------------------------------
// Primitive loading
// ------------------------------------------------------------

static bool load_primitive_vertices(VertexPacked** outVertices, uint32_t* outVertexCount, const cgltf_primitive* prim)
{
    if(!prim || prim->attributes_count == 0)
        return false;

    size_t vertexCount = prim->attributes[0].data->count;
    if(vertexCount == 0)
        return false;

    VertexPacked* verts = NULL;
    arrsetlen(verts, (int)vertexCount);
    memset(verts, 0, sizeof(VertexPacked) * vertexCount);

    float* scratch = (float*)malloc(sizeof(float) * vertexCount * 4);
    if(!scratch)
    {
        arrfree(verts);
        return false;
    }

    // POSITION required
    {
        const cgltf_accessor* pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
        if(!pos)
        {
            free(scratch);
            arrfree(verts);
            return false;
        }

        cgltf_accessor_unpack_floats(pos, scratch, vertexCount * 3);

        for(size_t i = 0; i < vertexCount; i++)
        {
            verts[i].vx = pack_f16(scratch[i*3 + 0]);
            verts[i].vy = pack_f16(scratch[i*3 + 1]);
            verts[i].vz = pack_f16(scratch[i*3 + 2]);
        }
    }

    // NORMAL optional
    {
        const cgltf_accessor* nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
        if(nrm)
        {
            cgltf_accessor_unpack_floats(nrm, scratch, vertexCount * 3);

            for(size_t i = 0; i < vertexCount; i++)
            {
                float nx = scratch[i*3 + 0];
                float ny = scratch[i*3 + 1];
                float nz = scratch[i*3 + 2];

                verts[i].np = pack_snorm_10_10_10_2(nx, ny, nz, 0);
            }
        }
    }

    // TANGENT optional
    {
        const cgltf_accessor* tan = cgltf_find_accessor(prim, cgltf_attribute_type_tangent, 0);
        if(tan)
        {
            cgltf_accessor_unpack_floats(tan, scratch, vertexCount * 4);

            for(size_t i = 0; i < vertexCount; i++)
            {
                float tx = scratch[i*4 + 0];
                float ty = scratch[i*4 + 1];
                float tz = scratch[i*4 + 2];
                float tw = scratch[i*4 + 3];

                verts[i].tp = pack_tangent_oct_8_8(tx, ty, tz);

                if(tw < 0.f)
                    verts[i].np |= (1u << 30);
            }
        }
    }

    // TEXCOORD_0 optional
    {
        const cgltf_accessor* uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
        if(uv)
        {
            cgltf_accessor_unpack_floats(uv, scratch, vertexCount * 2);

            for(size_t i = 0; i < vertexCount; i++)
            {
                verts[i].tu = pack_f16(scratch[i*2 + 0]);
                verts[i].tv = pack_f16(scratch[i*2 + 1]);
            }
        }
    }

    free(scratch);

    *outVertices = verts;
    *outVertexCount = (uint32_t)vertexCount;
    return true;
}

static bool load_primitive_indices(uint32_t** outIndices, uint32_t* outIndexCount, const cgltf_primitive* prim)
{
    if(!prim || !prim->indices)
        return false;

    size_t count = prim->indices->count;
    if(count == 0)
        return false;

    uint32_t* idx = NULL;
    arrsetlen(idx, (int)count);

    cgltf_accessor_unpack_indices(prim->indices, idx, 4, count);

    *outIndices = idx;
    *outIndexCount = (uint32_t)count;
    return true;
}

// ------------------------------------------------------------
// Mesh append
// ------------------------------------------------------------

static void compute_bounds(VertexPacked* verts, uint32_t count, vec3 outCenter, float* outRadius)
{
    vec3 center = {0,0,0};

    for(uint32_t i = 0; i < count; i++)
    {
        float x = meshopt_dequantizeHalf(verts[i].vx);
        float y = meshopt_dequantizeHalf(verts[i].vy);
        float z = meshopt_dequantizeHalf(verts[i].vz);

        center[0] += x;
        center[1] += y;
        center[2] += z;
    }

    float inv = (count > 0) ? (1.f / (float)count) : 0.f;
    center[0] *= inv;
    center[1] *= inv;
    center[2] *= inv;

    float radius = 0.f;
    for(uint32_t i = 0; i < count; i++)
    {
        float x = meshopt_dequantizeHalf(verts[i].vx);
        float y = meshopt_dequantizeHalf(verts[i].vy);
        float z = meshopt_dequantizeHalf(verts[i].vz);

        float dx = x - center[0];
        float dy = y - center[1];
        float dz = z - center[2];
        float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if(d > radius) radius = d;
    }

    glm_vec3_copy(center, outCenter);
    *outRadius = radius;
}

static void append_mesh(Geometry* geom, VertexPacked* verts, uint32_t vcount, uint32_t* indices, uint32_t icount)
{
    uint32_t* remap = (uint32_t*)malloc(sizeof(uint32_t) * vcount);
    if(!remap) return;

    size_t unique = meshopt_generateVertexRemap(remap, indices, icount, verts, vcount, sizeof(VertexPacked));

    meshopt_remapVertexBuffer(verts, verts, vcount, sizeof(VertexPacked), remap);
    meshopt_remapIndexBuffer(indices, indices, icount, remap);

    vcount = (uint32_t)unique;

    meshopt_optimizeVertexCache(indices, indices, icount, vcount);
    meshopt_optimizeVertexFetch(verts, indices, icount, verts, vcount, sizeof(VertexPacked));

    free(remap);

    Mesh mesh;
    memset(&mesh, 0, sizeof(mesh));

    mesh.vertexOffset = (uint32_t)arrlen(geom->vertices);
    mesh.vertexCount  = vcount;

    for(uint32_t i = 0; i < vcount; i++)
        arrpush(geom->vertices, verts[i]);

    MeshLod lod0;
    lod0.indexOffset = (uint32_t)arrlen(geom->indices);
    lod0.indexCount  = icount;
    lod0.error       = 0.f;

    for(uint32_t i = 0; i < icount; i++)
        arrpush(geom->indices, indices[i]);

    mesh.lodCount = 1;
    mesh.lods[0]  = lod0;

    float* positions = (float*)malloc(sizeof(float) * vcount * 3u);
    if(positions)
    {
        for(uint32_t i = 0; i < vcount; i++)
        {
            positions[i * 3u + 0u] = meshopt_dequantizeHalf(verts[i].vx);
            positions[i * 3u + 1u] = meshopt_dequantizeHalf(verts[i].vy);
            positions[i * 3u + 2u] = meshopt_dequantizeHalf(verts[i].vz);
        }

        static const float lod_ratios[] = {0.5f, 0.25f, 0.12f, 0.06f};
        size_t             prev_count   = icount;

        for(uint32_t li = 0; li < (uint32_t)(sizeof(lod_ratios) / sizeof(lod_ratios[0])) && mesh.lodCount < SCENE_MAX_LODS; li++)
        {
            size_t target = (size_t)((float)icount * lod_ratios[li]);
            if(target < 36)
                target = 36;
            target = (target / 3u) * 3u;
            if(target >= prev_count)
                continue;

            uint32_t* lod_indices = (uint32_t*)malloc(sizeof(uint32_t) * icount);
            if(!lod_indices)
                break;

            float  result_error = 0.0f;
            size_t lod_count    = meshopt_simplify(lod_indices, indices, icount, positions, vcount, sizeof(float) * 3u,
                                                   target, 1e-3f, meshopt_SimplifyErrorAbsolute, &result_error);

            if(lod_count < 3 || lod_count + 6 >= prev_count)
            {
                free(lod_indices);
                continue;
            }

            meshopt_optimizeVertexCache(lod_indices, lod_indices, lod_count, vcount);

            MeshLod lod = {0};
            lod.indexOffset = (uint32_t)arrlen(geom->indices);
            lod.indexCount  = (uint32_t)lod_count;
            lod.error       = result_error;

            for(uint32_t i = 0; i < lod.indexCount; i++)
                arrpush(geom->indices, lod_indices[i]);

            mesh.lods[mesh.lodCount++] = lod;
            prev_count                 = lod_count;

            free(lod_indices);
        }

        free(positions);
    }

    compute_bounds(&geom->vertices[mesh.vertexOffset], mesh.vertexCount, mesh.center, &mesh.radius);

    arrpush(geom->meshes, mesh);
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

bool scene_load_gltf(Scene* scene, const char* path)
{
    if(!scene || !path)
        return false;

    uint32_t materialBase = scene->materials ? (uint32_t)arrlen(scene->materials) : 0;
    uint32_t textureBase  = scene->texturePaths ? (uint32_t)arrlen(scene->texturePaths) : 0;
    uint32_t meshBase     = scene->geometry.meshes ? (uint32_t)arrlen(scene->geometry.meshes) : 0;

    bool init_scene = (materialBase == 0 && textureBase == 0 && meshBase == 0 && (!scene->draws || arrlen(scene->draws) == 0));

    if(init_scene)
    {
        // Dummy material at index 0
        Material dummy = {0};
        dummy.albedoTexture   = 0;
        dummy.normalTexture   = 0;
        dummy.specularTexture = 0;
        dummy.emissiveTexture = 0;
        dummy.occlusionTexture = 0;
        glm_vec4_one(dummy.diffuseFactor);
        glm_vec4_one(dummy.specularFactor);
        glm_vec3_zero(dummy.emissiveFactor);
        arrpush(scene->materials, dummy);

        // Dummy texture slot at index 0
        arrpush(scene->texturePaths, str_dup(""));

        // defaults
        scene->sunDirection[0] = 0.3f;
        scene->sunDirection[1] = 0.8f;
        scene->sunDirection[2] = 0.2f;

        glm_vec3_zero(scene->camera.position);
        glm_quat_identity(scene->camera.orientation);
        scene->camera.fovY  = glm_rad(60.f);
        scene->camera.znear = 0.1f;

        materialBase = 1;
        textureBase  = 1;
    }

    cgltf_options options = {0};
    cgltf_data* data = NULL;

    cgltf_result res = cgltf_parse_file(&options, path, &data);
    if(res != cgltf_result_success)
    {
        fprintf(stderr, "cgltf_parse_file failed\n");
        return false;
    }

    res = cgltf_load_buffers(&options, data, path);
    if(res != cgltf_result_success)
    {
        fprintf(stderr, "cgltf_load_buffers failed\n");
        cgltf_free(data);
        return false;
    }

    res = cgltf_validate(data);
    if(res != cgltf_result_success)
    {
        fprintf(stderr, "cgltf_validate failed\n");
        cgltf_free(data);
        return false;
    }

    char* basedir = path_dirname(path);

    // ------------------------------------------------------------
    // 1) Load geometry primitives
    // Build mapping: for each glTF mesh => (firstPrimitiveMeshIndex, primitiveCount)
    // ------------------------------------------------------------
    typedef struct PrimitiveRange
    {
        uint32_t first;
        uint32_t count;
    } PrimitiveRange;

    PrimitiveRange* primitives = NULL;
    arrsetlen(primitives, (int)data->meshes_count);
    memset(primitives, 0, sizeof(PrimitiveRange) * data->meshes_count);

    // store primitive material pointer list in geometry-mesh order
    cgltf_material** primitiveMaterials = NULL;

    for(size_t mi = 0; mi < data->meshes_count; mi++)
    {
        const cgltf_mesh* mesh = &data->meshes[mi];

        uint32_t meshOffset = (uint32_t)arrlen(scene->geometry.meshes);
        uint32_t primCount  = 0;

        for(size_t pi = 0; pi < mesh->primitives_count; pi++)
        {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            if(prim->type != cgltf_primitive_type_triangles || !prim->indices)
                continue;

            VertexPacked* verts = NULL;
            uint32_t vcount = 0;

            uint32_t* inds = NULL;
            uint32_t icount = 0;

            if(!load_primitive_vertices(&verts, &vcount, prim))
                continue;

            if(!load_primitive_indices(&inds, &icount, prim))
            {
                arrfree(verts);
                continue;
            }

            append_mesh(&scene->geometry, verts, vcount, inds, icount);

            // record primitive material in same order as appended meshes
            arrpush(primitiveMaterials, prim->material);

            primCount++;

            arrfree(verts);
            arrfree(inds);
        }

        primitives[mi].first = meshOffset;
        primitives[mi].count = primCount;
    }

    // ------------------------------------------------------------
    // 2) Load textures (paths only, keep .dds swap)
    // ------------------------------------------------------------
    for(size_t ti = 0; ti < data->textures_count; ti++)
    {
        cgltf_texture* tex = &data->textures[ti];
        if(!tex->image || !tex->image->uri)
            continue;

        char* uri = decode_uri_to_new_string(tex->image->uri);
        if(!uri) continue;

        char* dot = strrchr(uri, '.');
        if(dot) strcpy(dot, ".dds");

        char* full = path_join_dir_file(basedir, uri);
        if(full)
            arrpush(scene->texturePaths, str_dup(full));

        free(full);
        free(uri);
    }

    // ------------------------------------------------------------
    // 3) Load materials (indexing matches: scene materials start at 1)
    // ------------------------------------------------------------
    int textureOffset = (int)textureBase; // base index for this glTF's textures

    for(size_t i = 0; i < data->materials_count; i++)
    {
        cgltf_material* m = &data->materials[i];
        Material mat;
        memset(&mat, 0, sizeof(mat));

        glm_vec4_one(mat.diffuseFactor);
        glm_vec4_one(mat.specularFactor);
        glm_vec3_zero(mat.emissiveFactor);

        if(m->has_pbr_metallic_roughness)
        {
            if(m->pbr_metallic_roughness.base_color_texture.texture)
                mat.albedoTexture = textureOffset + (int)cgltf_texture_index(data, m->pbr_metallic_roughness.base_color_texture.texture);

            mat.diffuseFactor[0] = m->pbr_metallic_roughness.base_color_factor[0];
            mat.diffuseFactor[1] = m->pbr_metallic_roughness.base_color_factor[1];
            mat.diffuseFactor[2] = m->pbr_metallic_roughness.base_color_factor[2];
            mat.diffuseFactor[3] = m->pbr_metallic_roughness.base_color_factor[3];

            if(m->pbr_metallic_roughness.metallic_roughness_texture.texture)
                mat.specularTexture = textureOffset + (int)cgltf_texture_index(data, m->pbr_metallic_roughness.metallic_roughness_texture.texture);
        }

        if(m->normal_texture.texture)
            mat.normalTexture = textureOffset + (int)cgltf_texture_index(data, m->normal_texture.texture);

        if(m->emissive_texture.texture)
            mat.emissiveTexture = textureOffset + (int)cgltf_texture_index(data, m->emissive_texture.texture);

        if(m->occlusion_texture.texture)
            mat.occlusionTexture = textureOffset + (int)cgltf_texture_index(data, m->occlusion_texture.texture);

        mat.emissiveFactor[0] = m->emissive_factor[0];
        mat.emissiveFactor[1] = m->emissive_factor[1];
        mat.emissiveFactor[2] = m->emissive_factor[2];

        arrpush(scene->materials, mat);
    }

    // ------------------------------------------------------------
    // 4) Nodes -> Draws (THIS is the important part)
    // ------------------------------------------------------------
    for(size_t ni = 0; ni < data->nodes_count; ni++)
    {
        cgltf_node* node = &data->nodes[ni];

        // camera
        if(init_scene && node->camera && node->camera->type == cgltf_camera_type_perspective)
        {
            float matrix[16];
            cgltf_node_transform_world(node, matrix);

            float t[3], r[4], s[3];
            decompose_transform(t, r, s, matrix);

            scene->camera.position[0] = t[0];
            scene->camera.position[1] = t[1];
            scene->camera.position[2] = t[2];

            scene->camera.orientation[0] = r[0];
            scene->camera.orientation[1] = r[1];
            scene->camera.orientation[2] = r[2];
            scene->camera.orientation[3] = r[3];

            scene->camera.fovY = node->camera->data.perspective.yfov;
        }

        // sun
        if(init_scene && node->light && node->light->type == cgltf_light_type_directional)
        {
            float matrix[16];
            cgltf_node_transform_world(node, matrix);

            scene->sunDirection[0] = matrix[8];
            scene->sunDirection[1] = matrix[9];
            scene->sunDirection[2] = matrix[10];
        }

        // drawable mesh node
        if(node->mesh)
        {
            uint32_t gltfMeshIndex = (uint32_t)cgltf_mesh_index(data, node->mesh);
            PrimitiveRange range   = primitives[gltfMeshIndex];

            float matrix[16];
            cgltf_node_transform_world(node, matrix);

            float t[3], r[4], s[3];
            decompose_transform(t, r, s, matrix);

            // NOTE: your MeshDraw only supports uniform scale
            float uniformScale = fmaxf(s[0], fmaxf(s[1], s[2]));

            for(uint32_t j = 0; j < range.count; j++)
            {
                uint32_t geomMeshIndex = range.first + j;

                MeshDraw draw = {0};

                draw.position[0] = t[0];
                draw.position[1] = t[1];
                draw.position[2] = t[2];

                draw.scale = uniformScale;

                draw.orientation[0] = r[0];
                draw.orientation[1] = r[1];
                draw.orientation[2] = r[2];
                draw.orientation[3] = r[3];

                draw.meshIndex = geomMeshIndex;

                // material mapping:
                // primitiveMaterials is in geometry-mesh order
                uint32_t localMeshIndex = geomMeshIndex - meshBase;
                cgltf_material* pm = (localMeshIndex < (uint32_t)arrlen(primitiveMaterials)) ? primitiveMaterials[localMeshIndex] : NULL;
                draw.materialIndex = pm ? (uint32_t)(materialBase + cgltf_material_index(data, pm)) : 0;

                draw.postPass = 0;
                if(pm && pm->alpha_mode != cgltf_alpha_mode_opaque)
                    draw.postPass = 1;
                if(pm && pm->has_transmission)
                    draw.postPass = 2;

                arrpush(scene->draws, draw);
            }
        }
    }

    // cleanup
    arrfree(primitiveMaterials);
    arrfree(primitives);

    free(basedir);
    cgltf_free(data);

    printf("Loaded scene: %u meshes, %u draws, %u vertices, %u indices\n",
           (uint32_t)arrlen(scene->geometry.meshes),
           (uint32_t)arrlen(scene->draws),
           (uint32_t)arrlen(scene->geometry.vertices),
           (uint32_t)arrlen(scene->geometry.indices));

    return true;
}

bool scene_load_gltf_at(Scene* scene, const char* path, const vec3 position, const versor rotation, float scale,
                        uint32_t* outTemplateCount)
{
    uint32_t prev_draw_count = scene && scene->draws ? (uint32_t)arrlen(scene->draws) : 0;

    if(!scene_load_gltf(scene, path))
        return false;

    uint32_t templateCount = (uint32_t)arrlen(scene->draws) - prev_draw_count;
    if(outTemplateCount)
        *outTemplateCount = templateCount;

    if(templateCount > 0)
    {
        uint32_t end = prev_draw_count + templateCount;
        for(uint32_t i = prev_draw_count; i < end; i++)
        {
            MeshDraw* draw = &scene->draws[i];

            vec3 scaled_pos;
            glm_vec3_scale(draw->position, scale, scaled_pos);

            vec3 rotated_pos;
            glm_quat_rotatev(rotation, scaled_pos, rotated_pos);
            glm_vec3_add(position, rotated_pos, draw->position);

            draw->scale *= scale;

            versor out_rot;
            glm_quat_mul(rotation, draw->orientation, out_rot);
            glm_quat_copy(out_rot, draw->orientation);
        }

        scene_spawn_from_draws(scene, prev_draw_count, templateCount, position, rotation, scale);
    }

    return true;
}

// ------------------------------------------------------------
// SceneObject API
// ------------------------------------------------------------

static int scene_object_index_by_id(Scene* scene, uint32_t objectId)
{
    if(!scene || objectId == 0)
        return -1;

    for(int i = 0; i < arrlen(scene->objects); i++)
    {
        if(scene->objects[i].id == objectId)
            return i;
    }
    return -1;
}

uint32_t scene_object_create(Scene* scene, uint32_t meshIndex, uint32_t materialIndex, uint32_t templateIndex,
                             const vec3 position, const versor rotation, float scale)
{
    if(!scene)
        return 0;

    if(scene->nextObjectId == 0)
        scene->nextObjectId = 1;

    SceneObject obj = {0};
    obj.id            = scene->nextObjectId++;
    obj.meshIndex     = meshIndex;
    obj.materialIndex = materialIndex;
    obj.templateIndex = templateIndex;
    glm_vec3_copy(position, obj.position);
    glm_quat_copy(rotation, obj.rotation);
    obj.scale = scale;

    arrpush(scene->objects, obj);
    return obj.id;
}

uint32_t scene_spawn_from_draws(Scene* scene, uint32_t templateOffset, uint32_t templateCount,
                                const vec3 position, const versor rotation, float scale)
{
    if(!scene || templateCount == 0)
        return 0;

    uint32_t created = 0;
    uint32_t draw_count = (uint32_t)arrlen(scene->draws);

    for(uint32_t i = 0; i < templateCount; i++)
    {
        uint32_t templateIndex = templateOffset + i;
        if(templateIndex >= draw_count)
            break;

        MeshDraw* src = &scene->draws[templateIndex];
        scene_object_create(scene, src->meshIndex, src->materialIndex, templateIndex, position, rotation, scale);
        created++;
    }

    return created;
}

bool scene_object_remove(Scene* scene, uint32_t objectId)
{
    if(!scene)
        return false;

    int idx = scene_object_index_by_id(scene, objectId);
    if(idx < 0)
        return false;

    int last = arrlen(scene->objects) - 1;
    if(idx != last)
        scene->objects[idx] = scene->objects[last];

    arrpop(scene->objects);
    return true;
}

SceneObject* scene_object_get(Scene* scene, uint32_t objectId)
{
    int idx = scene_object_index_by_id(scene, objectId);
    if(idx < 0)
        return NULL;
    return &scene->objects[idx];
}

void scene_object_set_transform(Scene* scene, uint32_t objectId, const vec3 position, const versor rotation, float scale)
{
    SceneObject* obj = scene_object_get(scene, objectId);
    if(!obj)
        return;
    glm_vec3_copy(position, obj->position);
    glm_quat_copy(rotation, obj->rotation);
    obj->scale = scale;
}

void scene_object_translate(Scene* scene, uint32_t objectId, const vec3 delta)
{
    SceneObject* obj = scene_object_get(scene, objectId);
    if(!obj)
        return;
    glm_vec3_add(obj->position, delta, obj->position);
}

void scene_object_rotate(Scene* scene, uint32_t objectId, const versor delta)
{
    SceneObject* obj = scene_object_get(scene, objectId);
    if(!obj)
        return;
    versor out;
    glm_quat_mul(delta, obj->rotation, out);
    glm_quat_copy(out, obj->rotation);
}

void scene_object_scale(Scene* scene, uint32_t objectId, float scaleDelta)
{
    SceneObject* obj = scene_object_get(scene, objectId);
    if(!obj)
        return;
    obj->scale = fmaxf(0.01f, obj->scale + scaleDelta);
}


void scene_free(Scene* scene)
{
    if(!scene) return;

    arrfree(scene->geometry.vertices);
    arrfree(scene->geometry.indices);
    arrfree(scene->geometry.meshes);

    arrfree(scene->materials);
    arrfree(scene->draws);

    if(scene->texturePaths)
    {
        for(int i = 0; i < arrlen(scene->texturePaths); i++)
            free(scene->texturePaths[i]);
        arrfree(scene->texturePaths);
    }

    if(scene->animations)
    {
        for(int i = 0; i < arrlen(scene->animations); i++)
            arrfree(scene->animations[i].keyframes);
        arrfree(scene->animations);
    }

    arrfree(scene->objects);

    memset(scene, 0, sizeof(*scene));
}
