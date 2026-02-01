
#ifndef COORDINATE_SYSTEM_SLANG
#define COORDINATE_SYSTEM_SLANG

// Repo coordinate system standard:
// Right-handed world
// +X right, +Y up, +Z back, -Z forward
// Vulkan clip space with reverse-Z

static const float3 AXIS_RIGHT   = float3( 1.0,  0.0,  0.0);
static const float3 AXIS_UP      = float3( 0.0,  1.0,  0.0);
static const float3 AXIS_BACK    = float3( 0.0,  0.0,  1.0);
static const float3 AXIS_FORWARD = float3( 0.0,  0.0, -1.0);

// View-space forward depth (positive in front of camera)
float view_depth_forward(float3 viewPos)
{
    return -viewPos.z;
}

#endif // COORDINATE_SYSTEM_SLANG
