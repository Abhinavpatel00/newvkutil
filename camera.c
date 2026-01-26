
#include "camera.h"
#include "external/cglm/include/cglm/cglm.h"
#include "external/cglm/include/cglm/types.h"

void camera_init(Camera* cam)
{
    glm_vec3_copy((vec3){0.0f, 0.0f, 2.0f}, cam->position);
    glm_quat_identity(cam->rotation);

    cam->fov_y = glm_rad(60.0f);
    cam->znear = 0.1f;
    cam->zfar  = 100.0f;

    cam->move_speed = 2.5f;
    cam->mouse_sens = 0.0025f;
}

void camera_get_basis(Camera* cam, vec3 out_forward, vec3 out_right, vec3 out_up)
{
    vec3 f = {0.0f, 0.0f, -1.0f};  // forward = -Z
    vec3 r = {1.0f, 0.0f, 0.0f};   // right   = +X
    vec3 u = {0.0f, 1.0f, 0.0f};   // up      = +Y

    glm_quat_rotatev(cam->rotation, f, out_forward);
    glm_quat_rotatev(cam->rotation, r, out_right);
    glm_quat_rotatev(cam->rotation, u, out_up);

    glm_vec3_normalize(out_forward);
    glm_vec3_normalize(out_right);
    glm_vec3_normalize(out_up);
}

void camera_build_view(mat4 out_view, Camera* cam)
{
    // View matrix is inverse(camera transform)
    versor inv_rot;
    glm_quat_inv(cam->rotation, inv_rot);

    mat4 rot_m;
    glm_quat_mat4(inv_rot, rot_m);

    mat4 trans_m;
    glm_translate_make(trans_m, (vec3){-cam->position[0], -cam->position[1], -cam->position[2]});

    // view = R^-1 * T^-1
    glm_mat4_mul(rot_m, trans_m, out_view);
}

static void camera_build_proj_reverse_z(mat4 out_proj, Camera* cam, float aspect)
{
    float f = 1.0f / tanf(cam->fov_y * 0.5f);

    float n  = cam->znear;
    float zf = cam->zfar;

    glm_mat4_zero(out_proj);

    out_proj[0][0] = f / aspect;
    out_proj[1][1] = f;

    out_proj[2][2] = n / (zf - n);
    out_proj[2][3] = -1.0f;

    out_proj[3][2] = (zf * n) / (zf - n);
}
static void camera_build_proj_reverse_z_infinite(mat4 out_proj, Camera* cam, float aspect)
{
    float f = 1.0f / tanf(cam->fov_y * 0.5f);
    float n = cam->znear;

    glm_mat4_zero(out_proj);

    out_proj[0][0] = f / aspect;
    out_proj[1][1] = f;

    // Reverse-Z, infinite far
    out_proj[2][2] = 0.0f;
    out_proj[2][3] = -1.0f;

    out_proj[3][2] = n;
    out_proj[3][3] = 0.0f;
}
void camera_build_proj(mat4 out_proj, Camera* cam, float aspect)
{
    camera_build_proj_reverse_z_infinite(out_proj, cam, aspect);
}

void camera_apply_mouse(Camera* cam, float dx, float dy)
{
    float yaw   = -dx * cam->mouse_sens;
    float pitch = -dy * cam->mouse_sens;

    // yaw around world up
    versor q_yaw;
    glm_quatv(q_yaw, yaw, (vec3){0.0f, 1.0f, 0.0f});

    // pitch around camera right
    vec3 forward, right, up;
    camera_get_basis(cam, forward, right, up);

    versor q_pitch;
    glm_quatv(q_pitch, pitch, right);

    // rotation = pitch * yaw * rotation  (or yaw*pitch*rot)
    // Your old order was yaw then pitch. We'll keep it.
    versor tmp;
    glm_quat_mul(q_yaw, cam->rotation, tmp);
    glm_quat_mul(q_pitch, tmp, cam->rotation);

    glm_quat_normalize(cam->rotation);
}

void camera_update_keyboard(Camera* cam, GLFWwindow* win, float dt)
{
    vec3 forward, right, up;
    camera_get_basis(cam, forward, right, up);

    float v = cam->move_speed * dt;

    if(glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS)
        glm_vec3_muladds(forward, v, cam->position);

    if(glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS)
        glm_vec3_muladds(forward, -v, cam->position);

    if(glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS)
        glm_vec3_muladds(right, v, cam->position);

    if(glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS)
        glm_vec3_muladds(right, -v, cam->position);

    if(glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS)
        glm_vec3_muladds(up, v, cam->position);

    if(glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS)
        glm_vec3_muladds(up, -v, cam->position);
}
