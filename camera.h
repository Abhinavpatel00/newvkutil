
#pragma once

#include "vk_defaults.h"
typedef struct Camera
{
    vec3   position;   // world position
    versor rotation;   // quaternion (x,y,z,w)

    float fov_y;       // radians
    float znear;
    float zfar;

    float move_speed;  // units/sec
    float mouse_sens;  // radians per pixel
} Camera;

// Initialize camera with defaults
void camera_init(Camera* cam);

// Mouse look (dx, dy are mouse delta pixels)
void camera_apply_mouse(Camera* cam, float dx, float dy);

// Keyboard movement using GLFW keys (WASD + QE)
void camera_update_keyboard(Camera* cam, GLFWwindow* win, float dt);

// Build view matrix from camera pose
// Coordinate system: right-handed, +X right, +Y up, -Z forward.
void camera_build_view(mat4 out_view,  Camera* cam);

// Build projection matrix (Vulkan clip correction is NOT applied here)
void camera_build_proj(mat4 out_proj,  Camera* cam, float aspect);

// Utility: get forward/right/up basis vectors from camera rotation
void camera_get_basis( Camera* cam, vec3 out_forward, vec3 out_right, vec3 out_up);
