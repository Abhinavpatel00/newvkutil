#include "vk_defaults.h"
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <cglm/cglm.h>

#ifndef SCENE_MAX_LODS
#define SCENE_MAX_LODS 8
#endif
 int meshopt_quantizeSnorm(float v, int N);
typedef struct VertexPacked
{
    uint16_t vx, vy, vz;
    uint16_t tp;   // packed tangent: 8-8 oct encoding
    uint32_t np;   // packed normal: 10-10-10-2 + bitangent sign
    uint16_t tu, tv;
} VertexPacked;

typedef struct MeshLod
{
    uint32_t indexOffset;
    uint32_t indexCount;
    float    error;
} MeshLod;

typedef struct Mesh
{
    vec3  center;
    float radius;

    uint32_t vertexOffset;
    uint32_t vertexCount;

    uint32_t lodCount;
    MeshLod  lods[SCENE_MAX_LODS];
} Mesh;

typedef struct Material
{
    int albedoTexture;
    int normalTexture;
    int specularTexture;
    int emissiveTexture;
    int occlusionTexture;

    vec4 diffuseFactor;
    vec4 specularFactor;
    vec3 emissiveFactor;
} Material;

typedef struct MeshDraw
{
    vec3 position;
    float scale;
    versor orientation;

    uint32_t meshIndex;
    uint32_t postPass;
    uint32_t materialIndex;
} MeshDraw;

typedef struct SceneObject
{
    uint32_t id;
    uint32_t meshIndex;
    uint32_t materialIndex;
    uint32_t templateIndex;
    vec3     position;
    versor   rotation;
    float    scale;
} SceneObject;

typedef struct Cam
{
    vec3 position;
    versor orientation;
    float fovY;
    float znear;
} Cam;

typedef struct Keyframe
{
    vec3   translation;
    float  scale;
    versor rotation;
} Keyframe;

typedef struct Animation
{
    uint32_t drawIndex;
    float startTime;
    float period;

    Keyframe* keyframes; // stb_ds dynamic array
} Animation;

typedef struct Geometry
{
    VertexPacked* vertices; // stb_ds
    uint32_t*     indices;  // stb_ds
    Mesh*         meshes;   // stb_ds
} Geometry;

typedef struct Scene
{
    Geometry geometry;

    Material*  materials;     // stb_ds
    MeshDraw*  draws;         // stb_ds
    char**     texturePaths;  // stb_ds (heap strings)
    Animation* animations;    // stb_ds

    SceneObject* objects;      // stb_ds
    uint32_t     nextObjectId;

    Cam camera;
    vec3   sunDirection;
} Scene;

bool scene_load_gltf(Scene* scene, const char* path);
bool scene_load_gltf_at(Scene* scene, const char* path, const vec3 position, const versor rotation, float scale,
                        uint32_t* outTemplateCount);
void scene_free(Scene* scene);

uint32_t     scene_object_create(Scene* scene, uint32_t meshIndex, uint32_t materialIndex, uint32_t templateIndex,
                                 const vec3 position, const versor rotation, float scale);
uint32_t     scene_spawn_from_draws(Scene* scene, uint32_t templateOffset, uint32_t templateCount,
                                    const vec3 position, const versor rotation, float scale);
bool         scene_object_remove(Scene* scene, uint32_t objectId);
SceneObject* scene_object_get(Scene* scene, uint32_t objectId);
void         scene_object_set_transform(Scene* scene, uint32_t objectId, const vec3 position, const versor rotation, float scale);
void         scene_object_translate(Scene* scene, uint32_t objectId, const vec3 delta);
void         scene_object_rotate(Scene* scene, uint32_t objectId, const versor delta);
void         scene_object_scale(Scene* scene, uint32_t objectId, float scaleDelta);

