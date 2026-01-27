#include "vk_gui.h"

#include "GLFW/glfw3.h"

static PFN_vkVoidFunction imgui_vk_loader(const char* function_name, void* user_data)
{
    VkInstance instance = (VkInstance)user_data;
    return vkGetInstanceProcAddr(instance, function_name);
}

void vk_gui_init_state(VkGuiState* gui)
{
    if(!gui)
        return;

    gui->enabled         = true;
    gui->cursor_disabled = false;
    gui->prev_tab_state  = GLFW_RELEASE;

    gui->bloom_threshold = 0.0f;
    gui->bloom_knee      = 0.0f;
    gui->bloom_intensity = 0.0f;
    gui->bloom_combine   = 0.0f;
    gui->bloom_enabled   = false;

    gui->fog_color[0]  = 0.0f;
    gui->fog_color[1]  = 0.0f;
    gui->fog_color[2]  = 0.0f;
    gui->fog_density   = 0.0f;
    gui->fog_start     = 0.0f;
    gui->fog_end       = 0.0f;
    gui->fog_height    = 0.0f;
    gui->fog_falloff   = 0.0f;
    gui->fog_ray_steps = 0;
    gui->fog_enabled   = false;

    gui->godray_intensity = 0.0f;
    gui->godray_decay     = 0.0f;
    gui->godray_weight    = 0.0f;
    gui->godray_enabled   = false;

    gui->sun_dir[0]    = -0.3f;
    gui->sun_dir[1]    = 1.0f;
    gui->sun_dir[2]    = -0.2f;
    gui->sun_distance  = 1000.0f;
    gui->sun_intensity = 1.0f;
    gui->sun_phase_g   = 0.35f;

    gui->lensflare_enabled  = true;
    gui->lensflare_strength = 0.9f;
    gui->lensflare_f1       = 0.8f;
    gui->lensflare_f2       = 0.6f;
    gui->lensflare_f3       = 0.4f;
    gui->lensflare_chroma   = 0.004f;

    gui->tonemap_operator = 3;
    gui->tonemap_gamma    = 2.2f;
    gui->tonemap_exposure = 1.0f;

    gui->color_grading_enabled = true;
    gui->color_lift[0]         = 0.0f;
    gui->color_lift[1]         = 0.0f;
    gui->color_lift[2]         = 0.0f;
    gui->color_gamma[0]        = 1.0f;
    gui->color_gamma[1]        = 1.0f;
    gui->color_gamma[2]        = 1.0f;
    gui->color_gain[0]         = 1.0f;
    gui->color_gain[1]         = 1.0f;
    gui->color_gain[2]         = 1.0f;
    gui->color_saturation      = 1.0f;
    gui->color_contrast        = 1.0f;
    gui->color_temperature     = 0.0f;
    gui->color_tint            = 0.0f;

    gui->auto_exposure_enabled      = false;
    gui->auto_exposure_min_log_lum  = -8.0f;
    gui->auto_exposure_max_log_lum  = 8.0f;
    gui->auto_exposure_low_percent  = 0.10f;
    gui->auto_exposure_high_percent = 0.90f;
    gui->auto_exposure_speed_up     = 3.0f;
    gui->auto_exposure_speed_down   = 1.0f;
    gui->auto_exposure_transition   = 1.5f;

    gui->chromatic_aberration_enabled  = true;
    gui->chromatic_aberration_strength = 0.6f;
    gui->vignette_enabled              = true;
    gui->vignette_intensity            = 0.25f;
    gui->vignette_roundness            = 1.0f;
    gui->vignette_smoothness           = 0.35f;

    gui->dof_enabled        = true;
    gui->dof_mode           = 0;
    gui->dof_focal_distance = 8.0f;
    gui->dof_coc_scale      = 6.0f;
    gui->dof_max_coc        = 8.0f;
    gui->dof_max_depth      = 100.0f;

    gui->contour_nd_enabled      = true;
    gui->contour_nd_normal_coeff = 1.0f;
    gui->contour_nd_depth_coeff  = 1.0f;
    gui->contour_nd_thickness    = 1.2f;
    gui->contour_nd_depth_start  = 8.0f;
    gui->contour_nd_depth_end    = 40.0f;

    gui->contour_obj_enabled     = true;
    gui->contour_obj_method      = 2;
    gui->contour_obj_thickness   = 1.2f;
    gui->contour_obj_depth_start = 8.0f;
    gui->contour_obj_depth_end   = 40.0f;

    gui->fxaa_enabled    = true;
    gui->fxaa_reduce_min = 1.0f / 128.0f;
    gui->fxaa_reduce_mul = 1.0f / 8.0f;
    gui->fxaa_span_max   = 8.0f;

    gui->pass_hiz         = true;
    gui->pass_cull        = true;
    gui->pass_gfx         = true;
    gui->pass_bloom       = true;
    gui->pass_tonemap     = true;
    gui->pass_lens_flare  = true;
    gui->pass_dof         = true;
    gui->pass_contour_nd  = true;
    gui->pass_contour_obj = true;
    gui->pass_composite   = true;
    gui->pass_fxaa        = true;
    gui->pass_debug_text  = true;
    gui->pass_imgui       = true;
}

void vk_gui_handle_input(VkGuiState* gui, GLFWwindow* window, double* last_mx, double* last_my)
{
    if(!gui || !window)
        return;

    int tab_state = glfwGetKey(window, GLFW_KEY_TAB);
    if(tab_state == GLFW_PRESS && gui->prev_tab_state == GLFW_RELEASE)
    {
        gui->enabled = !gui->enabled;
    }
    gui->prev_tab_state = tab_state;

    bool want_cursor_disabled = !gui->enabled;
    if(want_cursor_disabled != gui->cursor_disabled)
    {
        glfwSetInputMode(window, GLFW_CURSOR, want_cursor_disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        gui->cursor_disabled = want_cursor_disabled;
        if(last_mx && last_my)
            glfwGetCursorPos(window, last_mx, last_my);
    }
}

void vk_gui_begin_frame(void)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    igNewFrame();
}

void vk_gui_draw(VkGuiState* gui, uint32_t hiz_mips, uint32_t draw_count)
{
    if(!gui || !gui->enabled)
        return;

    igBegin("Renderer", NULL, 0);
    igText("Press TAB to toggle UI/cursor");
    igSeparator();
    igText("Bloom");
    igCheckbox("Enable Bloom", &gui->bloom_enabled);
    igSliderFloat("Threshold", &gui->bloom_threshold, 0.0f, 5.0f, "%.2f", 0);
    igSliderFloat("Knee", &gui->bloom_knee, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Upsample Intensity", &gui->bloom_intensity, 0.0f, 3.0f, "%.2f", 0);
    igSliderFloat("Combine Intensity", &gui->bloom_combine, 0.0f, 2.0f, "%.2f", 0);
    igSeparator();
    igText("Fog & God Rays");
    igCheckbox("Enable Fog", &gui->fog_enabled);
    igColorEdit3("Fog Color", gui->fog_color, 0);
    igSliderFloat("Fog Density", &gui->fog_density, 0.0f, 0.2f, "%.3f", 0);
    igSliderFloat("Fog Start", &gui->fog_start, 0.0f, 100.0f, "%.1f", 0);
    igSliderFloat("Fog End", &gui->fog_end, 1.0f, 200.0f, "%.1f", 0);
    igSliderFloat("Fog Height", &gui->fog_height, -20.0f, 40.0f, "%.1f", 0);
    igSliderFloat("Fog Falloff", &gui->fog_falloff, 0.0f, 1.0f, "%.3f", 0);
    igSliderInt("Fog Ray Steps", &gui->fog_ray_steps, 1, 32, "%d", 0);
    igCheckbox("Enable God Rays", &gui->godray_enabled);
    igSliderFloat("Godray Intensity", &gui->godray_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Godray Decay", &gui->godray_decay, 0.8f, 0.99f, "%.3f", 0);
    igSliderFloat("Godray Weight", &gui->godray_weight, 0.0f, 1.5f, "%.2f", 0);
    igSliderFloat("Sun Intensity", &gui->sun_intensity, 0.0f, 5.0f, "%.2f", 0);
    igSliderFloat("Sun Phase G", &gui->sun_phase_g, -0.9f, 0.9f, "%.2f", 0);
    igSeparator();
    igText("Lens Flare");
    igCheckbox("Enable Lens Flare", &gui->lensflare_enabled);
    igSliderFloat("Strength", &gui->lensflare_strength, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("F1", &gui->lensflare_f1, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("F2", &gui->lensflare_f2, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("F3", &gui->lensflare_f3, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Chroma", &gui->lensflare_chroma, 0.0f, 0.02f, "%.3f", 0);
    igSeparator();
    igText("Sun Position");
    igSliderFloat3("Sun Direction", gui->sun_dir, -1.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Sun Distance", &gui->sun_distance, 10.0f, 5000.0f, "%.1f", 0);
    igSeparator();
    igText("Tonemap");
    igSliderInt("Operator (0=Default,1=Uncharted,2=Hejl,3=ACES)", &gui->tonemap_operator, 0, 3, "%d", 0);
    igSliderFloat("Gamma", &gui->tonemap_gamma, 1.0f, 3.0f, "%.2f", 0);
    igSliderFloat("Exposure", &gui->tonemap_exposure, 0.1f, 5.0f, "%.2f", 0);
    igSeparator();
    igText("Color Grading");
    igCheckbox("Enable Grading", &gui->color_grading_enabled);
    igSliderFloat3("Lift", gui->color_lift, -0.5f, 0.5f, "%.2f", 0);
    igSliderFloat3("Gamma", gui->color_gamma, 0.5f, 2.0f, "%.2f", 0);
    igSliderFloat3("Gain", gui->color_gain, 0.5f, 2.0f, "%.2f", 0);
    igSliderFloat("Saturation", &gui->color_saturation, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Contrast", &gui->color_contrast, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Temperature", &gui->color_temperature, -1.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Tint", &gui->color_tint, -1.0f, 1.0f, "%.2f", 0);
    igSeparator();
    igText("Auto Exposure");
    igCheckbox("Enable Auto Exposure", &gui->auto_exposure_enabled);
    igSliderFloat("Min Log Lum", &gui->auto_exposure_min_log_lum, -12.0f, 0.0f, "%.2f", 0);
    igSliderFloat("Max Log Lum", &gui->auto_exposure_max_log_lum, 0.0f, 16.0f, "%.2f", 0);
    igSliderFloat("Low Percent", &gui->auto_exposure_low_percent, 0.0f, 0.45f, "%.2f", 0);
    igSliderFloat("High Percent", &gui->auto_exposure_high_percent, 0.55f, 1.0f, "%.2f", 0);
    igSliderFloat("Speed Up", &gui->auto_exposure_speed_up, 0.1f, 10.0f, "%.2f", 0);
    igSliderFloat("Speed Down", &gui->auto_exposure_speed_down, 0.1f, 10.0f, "%.2f", 0);
    igSliderFloat("Transition", &gui->auto_exposure_transition, 0.1f, 4.0f, "%.2f", 0);
    igSeparator();
    igText("Post Effects");
    igCheckbox("Chromatic Aberration", &gui->chromatic_aberration_enabled);
    igSliderFloat("CA Strength", &gui->chromatic_aberration_strength, 0.0f, 2.0f, "%.2f", 0);
    igCheckbox("Vignette", &gui->vignette_enabled);
    igSliderFloat("Vignette Intensity", &gui->vignette_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Vignette Roundness", &gui->vignette_roundness, 0.1f, 2.0f, "%.2f", 0);
    igSliderFloat("Vignette Smoothness", &gui->vignette_smoothness, 0.05f, 1.0f, "%.2f", 0);
    igSeparator();
    igText("Depth of Field");
    igCheckbox("Enable DoF", &gui->dof_enabled);
    igSliderInt("DoF Mode (0=Gaussian,1=Bokeh)", &gui->dof_mode, 0, 1, "%d", 0);
    igSliderFloat("Focal Distance", &gui->dof_focal_distance, 0.1f, 200.0f, "%.2f", 0);
    igSliderFloat("CoC Scale", &gui->dof_coc_scale, 0.0f, 20.0f, "%.2f", 0);
    igSliderFloat("Max CoC", &gui->dof_max_coc, 0.0f, 16.0f, "%.2f", 0);
    igSliderFloat("Max Depth", &gui->dof_max_depth, 1.0f, 1000.0f, "%.1f", 0);
    igSeparator();
    igText("Contours (Normal/Depth)");
    igCheckbox("Enable ND Contour", &gui->contour_nd_enabled);
    igSliderFloat("ND Normal Coeff", &gui->contour_nd_normal_coeff, 0.0f, 3.0f, "%.2f", 0);
    igSliderFloat("ND Depth Coeff", &gui->contour_nd_depth_coeff, 0.0f, 3.0f, "%.2f", 0);
    igSliderFloat("ND Thickness", &gui->contour_nd_thickness, 0.0f, 6.0f, "%.2f", 0);
    igSliderFloat("ND Depth Start", &gui->contour_nd_depth_start, 0.0f, 200.0f, "%.1f", 0);
    igSliderFloat("ND Depth End", &gui->contour_nd_depth_end, 0.0f, 400.0f, "%.1f", 0);
    igSeparator();
    igText("Contours (Object)");
    igCheckbox("Enable Object Contour", &gui->contour_obj_enabled);
    igSliderInt("Contour Method", &gui->contour_obj_method, 0, 3, "%d", 0);
    igSliderFloat("Obj Thickness", &gui->contour_obj_thickness, 0.0f, 6.0f, "%.2f", 0);
    igSliderFloat("Obj Depth Start", &gui->contour_obj_depth_start, 0.0f, 200.0f, "%.1f", 0);
    igSliderFloat("Obj Depth End", &gui->contour_obj_depth_end, 0.0f, 400.0f, "%.1f", 0);
    igSeparator();
    igText("FXAA");
    igCheckbox("Enable FXAA", &gui->fxaa_enabled);
    igSliderFloat("Reduce Min", &gui->fxaa_reduce_min, 0.0f, 0.01f, "%.5f", 0);
    igSliderFloat("Reduce Mul", &gui->fxaa_reduce_mul, 0.0f, 0.5f, "%.3f", 0);
    igSliderFloat("Span Max", &gui->fxaa_span_max, 1.0f, 16.0f, "%.1f", 0);
    igSeparator();
    igText("RG Pass Toggles");
    igCheckbox("HiZ", &gui->pass_hiz);
    igCheckbox("Cull", &gui->pass_cull);
    igCheckbox("Gfx", &gui->pass_gfx);
    igCheckbox("Bloom", &gui->pass_bloom);
    igCheckbox("Tonemap", &gui->pass_tonemap);
    igCheckbox("Lens Flare", &gui->pass_lens_flare);
    igCheckbox("DoF", &gui->pass_dof);
    igCheckbox("Contour ND", &gui->pass_contour_nd);
    igCheckbox("Contour Obj", &gui->pass_contour_obj);
    igCheckbox("Composite", &gui->pass_composite);
    igCheckbox("FXAA", &gui->pass_fxaa);
    igCheckbox("Debug Text", &gui->pass_debug_text);
    igCheckbox("ImGui", &gui->pass_imgui);
    igSeparator();
    igText("HiZ mips: %u", hiz_mips);
    igText("Draws: %u", draw_count);
    igEnd();
}

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
                       VkCommandPool     upload_pool)
{
    (void)upload_pool;

    igCreateContext(NULL);

    ImGuiIO* io     = igGetIO_Nil();
    io->IniFilename = NULL;
    io->LogFilename = NULL;

    igStyleColorsDark(NULL);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imgui_vk_loader, instance);

    ImGui_ImplVulkan_InitInfo info    = {0};
    info.ApiVersion                   = VK_API_VERSION_1_3;
    info.Instance                     = instance;
    info.PhysicalDevice               = gpu;
    info.Device                       = device;
    info.QueueFamily                  = queue_family;
    info.Queue                        = queue;
    info.DescriptorPool               = imgui_pool;
    info.MinImageCount                = min_image_count;
    info.ImageCount                   = image_count;
    info.UseDynamicRendering          = true;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    info.PipelineInfoMain.PipelineRenderingCreateInfo = (VkPipelineRenderingCreateInfoKHR){
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format,
        .depthAttachmentFormat   = depth_format,
    };

    info.PipelineInfoMain.SwapChainImageUsage = swapchain_usage;

    ImGui_ImplVulkan_Init(&info);
}

void vk_gui_imgui_shutdown(void)
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    igDestroyContext(NULL);
}

void vk_gui_imgui_render(VkGuiState* gui, VkCommandBuffer cmd)
{
    if(!gui)
        return;

    igRender();
    if(gui->enabled)
        ImGui_ImplVulkan_RenderDrawData(igGetDrawData(), cmd, VK_NULL_HANDLE);
}

void vk_gui_draw_terrain_controls(VkGuiState* gui, VkTerrainGuiParams* terrain, VkGrassGuiParams* grass, VkTerrainGuiActions* actions, bool* sculpt_mode)
{
    if(!gui || !gui->enabled)
        return;

    igBegin("Terrain & Grass", NULL, 0);
    igText("TAB: toggle GUI mode");
    igCheckbox("Sculpt Mode", sculpt_mode);
    igSeparator();

    if(terrain)
    {
        igText("Terrain");
        igSliderFloat("Height Scale", &terrain->height_scale, 5.0f, 80.0f, "%.1f", 0);
        igSliderFloat("Freq", &terrain->freq, 0.005f, 0.08f, "%.3f", 0);
        igInputFloat2("Noise Offset", terrain->noise_offset, "%.2f", 0);
        igSliderFloat("Brush Radius", &terrain->brush_radius, 1.0f, 50.0f, "%.1f", 0);
        igSliderFloat("Brush Strength", &terrain->brush_strength, 0.01f, 2.0f, "%.3f", 0);
        igSliderFloat("Brush Hardness", &terrain->brush_hardness, 0.0f, 1.0f, "%.2f", 0);

        if(actions)
        {
            if(igButton("Load Heightmap (L)", (ImVec2){0, 0}))
                actions->load = true;
            igSameLine(0.0f, -1.0f);
            if(igButton("Save Heightmap (K)", (ImVec2){0, 0}))
                actions->save = true;
            if(igButton("Regenerate Terrain (R)", (ImVec2){0, 0}))
                actions->regenerate = true;
        }
    }

    igSeparator();

    if(grass)
    {
        igText("Grass");
        igSliderFloat("Blade Height", &grass->blade_height, 0.2f, 3.5f, "%.2f", 0);
        igSliderFloat("Blade Width", &grass->blade_width, 0.02f, 0.4f, "%.3f", 0);
        igSliderFloat("Wind Strength", &grass->wind_strength, 0.0f, 2.0f, "%.2f", 0);
        igSliderFloat("Density", &grass->density, 0.05f, 3.0f, "%.2f", 0);
        igSliderFloat("Far Distance", &grass->far_distance, 20.0f, 200.0f, "%.1f", 0);
    }

    igEnd();
}
void vk_gui_draw_toon_controls(VkGuiState* gui, VkToonGuiParams* toon)
{
    if(!gui || !gui->enabled || !toon)
        return;

    igBegin("Toon", NULL, 0);
    igCheckbox("Enable Toon", &toon->enabled);
    igSeparator();

    igText("Lighting");
    igSliderFloat3("Light Dir", toon->light_dir, -1.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Light Intensity", &toon->light_intensity, 0.0f, 5.0f, "%.2f", 0);
    igColorEdit3("Indirect Min", toon->indirect_min_color, 0);
    igSliderFloat("Indirect Mult", &toon->indirect_multiplier, 0.0f, 2.0f, "%.2f", 0);
    igColorEdit3("Shadow Color", toon->shadow_color, 0);
    igSliderFloat("Shadow Strength", &toon->receive_shadow, 0.0f, 1.0f, "%.2f", 0);
    igSeparator();

    igText("Cel Shade");
    igSliderFloat("Mid Point", &toon->cel_mid, -1.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Softness", &toon->cel_soft, 0.0f, 0.5f, "%.3f", 0);
    igCheckbox("Is Face", &toon->is_face);
    igSeparator();

    igText("Outline");
    igColorEdit3("Outline Color", toon->outline_color, 0);
    igSliderFloat("Outline Width", &toon->outline_width, 0.0f, 4.0f, "%.2f", 0);
    igSliderFloat("Outline ZOffset", &toon->outline_z_offset, 0.0f, 0.01f, "%.5f", 0);
    igSliderFloat("ZOffset Remap Start", &toon->outline_z_remap_start, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("ZOffset Remap End", &toon->outline_z_remap_end, 0.0f, 1.0f, "%.2f", 0);
    igSeparator();

    igText("Alpha/Emission/Occlusion");
    igCheckbox("Alpha Clip", &toon->use_alpha_clip);
    igSliderFloat("Cutoff", &toon->cutoff, 0.0f, 1.0f, "%.2f", 0);
    igCheckbox("Emission", &toon->use_emission);
    igSliderFloat("Emission * Base", &toon->emission_mul_by_base, 0.0f, 1.0f, "%.2f", 0);
    igCheckbox("Occlusion", &toon->use_occlusion);
    igSliderFloat("Occlusion Strength", &toon->occlusion_strength, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Occlusion Remap Start", &toon->occlusion_remap_start, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Occlusion Remap End", &toon->occlusion_remap_end, 0.0f, 1.0f, "%.2f", 0);
    igEnd();
}
void vk_gui_draw_water_controls(VkGuiState* gui, VkWaterGuiParams* water)
{
    if(!gui || !gui->enabled || !water)
        return;

    igBegin("Water", NULL, 0);
    igCheckbox("Enable Water", &water->enabled);
    igCheckbox("Foam", &water->foam_enabled);


    igSameLine(0.0f, -1.0f);
    igCheckbox("Fresnel", &water->fresnel_enabled);
    igSameLine(0.0f, -1.0f);
    igCheckbox("Specular  ", &water->specular_enabled);
    igSeparator();

    igSliderFloat("Water Height", &water->water_height, -10.0f, 40.0f, "%.2f", 0);
    igSliderFloat("Depth Fade", &water->depth_fade, 0.5f, 40.0f, "%.2f", 0);
    igSliderFloat("Foam Distance", &water->foam_distance, 0.1f, 12.0f, "%.2f", 0);
    igSliderFloat("Foam Scale", &water->foam_scale, 0.2f, 8.0f, "%.2f", 0);
    igSliderFloat("Foam Speed", &water->foam_speed, 0.0f, 4.0f, "%.2f", 0);
    igSliderFloat("Normal Scale", &water->normal_scale, 0.1f, 3.0f, "%.2f", 0);
    igSliderFloat("Normal Scale 2", &water->normal_scale2, 0.0f, 3.0f, "%.2f", 0);
    igSliderFloat("Specular slider", &water->specular, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Spec Power", &water->spec_power, 4.0f, 256.0f, "%.1f", 0);
    igSliderFloat("Opacity", &water->opacity, 0.05f, 1.0f, "%.2f", 0);

    igSeparator();
    igSliderFloat("Tiling", &water->tiling, 0.1f, 10.0f, "%.2f", 0);
    igSliderFloat("Foam Tiling", &water->foam_tiling, 0.1f, 16.0f, "%.2f", 0);
    igSliderFloat("Normal Speed", &water->normal_speed, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Normal Speed 2", &water->normal_speed2, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Foam Strength", &water->foam_strength, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Fresnel Power", &water->fresnel_power, 1.0f, 8.0f, "%.2f", 0);
    igSliderFloat("Fresnel Strength", &water->fresnel_strength, 0.0f, 2.0f, "%.2f", 0);
    igSliderFloat("Color Variation", &water->color_variation, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Distortion", &water->distortion_strength, 0.0f, 1.5f, "%.2f", 0);

    igSeparator();
    igSliderFloat3("Sun Dir", water->sun_dir, -1.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Sun Intensity", &water->sun_intensity, 0.0f, 3.0f, "%.2f", 0);

    igSeparator();
    igColorEdit3("Shallow Color", water->shallow_color, 0);
    igColorEdit3("Deep Color", water->deep_color, 0);
    igColorEdit3("Foam Color", water->foam_color, 0);

    igEnd();
}
