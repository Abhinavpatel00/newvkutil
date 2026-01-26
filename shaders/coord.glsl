#ifndef COORDINATE_SYSTEM_GLSL
#define COORDINATE_SYSTEM_GLSL

// Repo coordinate system standard:
// Right-handed world, +X right, +Y up, +Z back, -Z forward.
// Vulkan clip space with reverse-Z, depth range [0, 1].

const vec3 AXIS_RIGHT   = vec3(1.0, 0.0, 0.0);
const vec3 AXIS_UP      = vec3(0.0, 1.0, 0.0);
const vec3 AXIS_BACK    = vec3(0.0, 0.0, 1.0);
const vec3 AXIS_FORWARD = vec3(0.0, 0.0, -1.0);

// View-space forward depth (positive in front of camera).
float view_depth_forward(vec3 viewPos)
{
    return -viewPos.z;
}

#endif // COORDINATE_SYSTEM_GLSL
