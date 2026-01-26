#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE (1)
#define IMGUI_IMPL_VULKAN_USE_VOLK
#define CIMGUI_USE_GLFW
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_USE_VULKAN

#include "vk_defaults.h"

#ifdef Status
#undef Status
#endif

#include "external/cimgui/cimgui.h"
#include "external/cimgui/cimgui_impl.h"

#include "tinytypes.h"

typedef struct GLFWwindow GLFWwindow;

typedef struct VkGuiState
{
	bool enabled;
	bool cursor_disabled;
	int  prev_tab_state;

	float bloom_threshold;
	float bloom_knee;
	float bloom_intensity;
	float bloom_combine;

	bool  bloom_enabled;

	float fog_color[3];
	float fog_density;
	float fog_start;
	float fog_end;
	float fog_height;
	float fog_falloff;
	int   fog_ray_steps;
	bool  fog_enabled;

	float godray_intensity;
	float godray_decay;
	float godray_weight;
	bool  godray_enabled;

	float sun_dir[3];
	float sun_distance;
	float sun_intensity;
	float sun_phase_g;

	bool  lensflare_enabled;
	float lensflare_strength;
	float lensflare_f1;
	float lensflare_f2;
	float lensflare_f3;
	float lensflare_chroma;

	int   tonemap_operator;
	float tonemap_gamma;
	float tonemap_exposure;

	bool  color_grading_enabled;
	float color_lift[3];
	float color_gamma[3];
	float color_gain[3];
	float color_saturation;
	float color_contrast;
	float color_temperature;
	float color_tint;

	bool  auto_exposure_enabled;
	float auto_exposure_min_log_lum;
	float auto_exposure_max_log_lum;
	float auto_exposure_low_percent;
	float auto_exposure_high_percent;
	float auto_exposure_speed_up;
	float auto_exposure_speed_down;
	float auto_exposure_transition;

	bool  chromatic_aberration_enabled;
	float chromatic_aberration_strength;
	bool  vignette_enabled;
	float vignette_intensity;
	float vignette_roundness;
	float vignette_smoothness;

	bool  dof_enabled;
	int   dof_mode;
	float dof_focal_distance;
	float dof_coc_scale;
	float dof_max_coc;
	float dof_max_depth;

	bool  contour_nd_enabled;
	float contour_nd_normal_coeff;
	float contour_nd_depth_coeff;
	float contour_nd_thickness;
	float contour_nd_depth_start;
	float contour_nd_depth_end;

	bool  contour_obj_enabled;
	int   contour_obj_method;
	float contour_obj_thickness;
	float contour_obj_depth_start;
	float contour_obj_depth_end;

	bool  fxaa_enabled;
	float fxaa_reduce_min;
	float fxaa_reduce_mul;
	float fxaa_span_max;

	bool pass_hiz;
	bool pass_cull;
	bool pass_gfx;
	bool pass_bloom;
	bool pass_tonemap;
	bool pass_lens_flare;
	bool pass_dof;
	bool pass_contour_nd;
	bool pass_contour_obj;
	bool pass_composite;
	bool pass_fxaa;
	bool pass_debug_text;
	bool pass_imgui;
} VkGuiState;

typedef struct VkTerrainGuiParams
{
	float height_scale;
	float freq;
	float noise_offset[2];
	float brush_radius;
	float brush_strength;
	float brush_hardness;
} VkTerrainGuiParams;

typedef struct VkGrassGuiParams
{
	float blade_height;
	float blade_width;
	float wind_strength;
	float density;
	float far_distance;
} VkGrassGuiParams;

typedef struct VkWaterGuiParams
{
	bool  enabled;
	bool  foam_enabled;
	bool  fresnel_enabled;
	bool  specular_enabled;
	float water_height;
	float depth_fade;
	float foam_distance;
	float foam_scale;
	float foam_speed;
	float normal_scale;
	float normal_scale2;
	float specular;
	float spec_power;
	float opacity;
	float tiling;
	float foam_tiling;
	float normal_speed;
	float normal_speed2;
	float foam_strength;
	float fresnel_power;
	float fresnel_strength;
	float color_variation;
	float distortion_strength;
	float sun_dir[3];
	float sun_intensity;
	float shallow_color[3];
	float deep_color[3];
	float foam_color[3];
} VkWaterGuiParams;

typedef struct VkToonGuiParams
{
	bool  enabled;
	float light_dir[3];
	float light_intensity;
	float indirect_min_color[3];
	float indirect_multiplier;
	float shadow_color[3];
	float receive_shadow;
	float outline_color[3];
	float outline_width;
	float outline_z_offset;
	float outline_z_remap_start;
	float outline_z_remap_end;
	float cel_mid;
	float cel_soft;
	bool  use_alpha_clip;
	float cutoff;
	bool  use_emission;
	float emission_mul_by_base;
	bool  use_occlusion;
	float occlusion_strength;
	float occlusion_remap_start;
	float occlusion_remap_end;
	bool  is_face;
} VkToonGuiParams;

typedef struct VkTerrainGuiActions
{
	bool save;
	bool load;
	bool regenerate;
} VkTerrainGuiActions;

void vk_gui_init_state(VkGuiState* gui);
void vk_gui_handle_input(VkGuiState* gui, GLFWwindow* window, double* last_mx, double* last_my);
void vk_gui_begin_frame(void);
void vk_gui_draw(VkGuiState* gui, uint32_t hiz_mips, uint32_t draw_count);

void vk_gui_imgui_init(GLFWwindow*       window,
					   VkInstance        instance,
					   VkPhysicalDevice  gpu,
					   VkDevice          device,
					   uint32_t          queue_family,
					   VkQueue           queue,
					   VkDescriptorPool  imgui_pool,
					   uint32_t          min_image_count,
					   uint32_t          image_count,
					   VkFormat          swapchain_format,
					   VkFormat          depth_format,
					   VkImageUsageFlags swapchain_usage,
					   VkCommandPool     upload_pool);
void vk_gui_imgui_shutdown(void);
void vk_gui_imgui_render(VkGuiState* gui, VkCommandBuffer cmd);

void vk_gui_draw_terrain_controls(VkGuiState*           gui,
								  VkTerrainGuiParams*  terrain,
								  VkGrassGuiParams*    grass,
								  VkTerrainGuiActions* actions,
								  bool*                sculpt_mode);

void vk_gui_draw_water_controls(VkGuiState* gui, VkWaterGuiParams* water);
void vk_gui_draw_toon_controls(VkGuiState* gui, VkToonGuiParams* toon);
