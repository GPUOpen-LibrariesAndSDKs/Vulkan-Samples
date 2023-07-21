//  Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

#pragma once

#include <vector>

#include "camera.h"
#include "core/shader_module.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/camera.h"
#include "vulkan_sample.h"

// Not a part of vulkan_core.h yet, so include it separately.
#include "vk_amdx_shader_enqueue.h"

class GpuDispatch : public vkb::VulkanSample
{
  public:
	GpuDispatch();

	virtual bool prepare(vkb::Platform& platform) override;
    virtual void update(float delta_time) override;
    virtual bool resize(uint32_t width, uint32_t height) override;
    virtual void input_event(const vkb::InputEvent &input_event) override;
    virtual void finish() override;

	virtual ~GpuDispatch() = default;

protected:
    static constexpr auto ShaderPermutationNone = UINT_MAX;

    enum PresentMode : uint32_t
    {
        PRESENT_MODE_DEFAULT,
        PRESENT_MODE_BURST,
        PRESENT_MODE_SINGLE,
    };

    enum Scene : uint32_t
    {
        SCENE_TEAPOT,
        SCENE_MONKEYS,
        SCENE_MATERIAL_MAP_1,
        SCENE_MATERIAL_MAP_2,
        SCENE_SANITY_CHECK,         // only work graphs, very simple shaders to check the functionality
    };

    // Determines how shader enqueue "graph" is implemented.
    enum EnqueueGraphType
    {
        ENQUEUE_GRAPH_TYPE_FIXED_EXPANSION,                     // Use fixed expansion nodes
        ENQUEUE_GRAPH_TYPE_DYNAMIC_EXPANSION,                   // Use dynamic expansion nodes
        ENQUEUE_GRAPH_TYPE_AGGREGATION,                         // Use aggregation nodes to classify per pixel
    };

    const uint32_t   TileSize = 16;     // width/height of the compute workgroup used for tile classification

    Scene            scene                            = SCENE_TEAPOT;
    EnqueueGraphType graph_type                       = ENQUEUE_GRAPH_TYPE_FIXED_EXPANSION;
    bool             resources_ready                  = false;
    bool             textures_ready                   = false;  // tracked separately as it's done only once at startup
    bool             requires_init_commands           = false;
    uint32_t         highlighted_shader_permutation   = ShaderPermutationNone;  // used by classified modes

    // Tweaks
    uint32_t         num_material_bits                = 2;      // the number of materials bits used in the scene
    uint32_t         num_instances                    = 1;      // the number of models in the scene
    uint32_t         num_textures_per_material        = 1;
    float            alu_complexity                   = 1.0f;   // adjusts the number of iterations computing noise in the shaders (0.0 is min, 1.0 is max)
    float            camera_distance                  = 1.0f;   // higher number is farther away from origin
    bool             rotate_animation                 = true;   // whether to play rotate animation
    bool             reset_scratch_buffer_inline      = false;
    bool             always_reset_scratch_buffer      = false;
    bool             deferred_clear_swapchain_image   = false;  // will clear the image before drawing into it
    bool             use_hlsl_shaders                 = false;
    PresentMode      present_mode                     = PRESENT_MODE_DEFAULT;  // how to present frames

    vkb::Camera                                  camera;
    std::unique_ptr<vkb::sg::SubMesh>            model;

    std::unique_ptr<vkb::sg::Image>              material_map;    // For scenes based on a material id map
    std::unique_ptr<vkb::sg::Image>              source_texture;  // Other textures are created from this
    std::vector<std::unique_ptr<vkb::sg::Image>> textures;

    // Keep the compiled modules around, to avoid glslang recompilation on resizes, etc.
    std::unordered_map<std::string, VkShaderModule> shader_module_cache;

    bool                                                    is_shader_enqueue_supported = false;
    VkPhysicalDeviceShaderEnqueuePropertiesAMDX              shader_enqueue_properties = {};
    VkExecutionGraphPipelineScratchSizeAMDX                  enqueue_scratch_buffer_size = {};

    VkPipelineCache         pipeline_cache                 = VK_NULL_HANDLE;
    VkRenderPass            gui_render_pass                = VK_NULL_HANDLE;

    VkRenderPass            render_pass                    = VK_NULL_HANDLE;
    VkDescriptorPool        descriptor_pool                = VK_NULL_HANDLE;
    VkDescriptorSetLayout   descriptor_set_layout          = VK_NULL_HANDLE;
    VkPipelineLayout        graphics_pipeline_layout       = VK_NULL_HANDLE;
    VkPipeline              graphics_pipeline              = VK_NULL_HANDLE;
    VkPipeline              background_graphics_pipeline   = VK_NULL_HANDLE;

    VkDescriptorSetLayout   classify_descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout        classify_pipeline_layout       = VK_NULL_HANDLE;
    VkPipeline              classify_pipeline              = VK_NULL_HANDLE;
    VkPipeline              classify_and_compose_pipeline  = VK_NULL_HANDLE;

    VkSampler               default_sampler                = VK_NULL_HANDLE;
    VkSampler               texture_sampler                = VK_NULL_HANDLE;
    VkDescriptorSetLayout   compose_descriptor_set_layout  = VK_NULL_HANDLE;
    VkPipelineLayout        compose_pipeline_layout        = VK_NULL_HANDLE;
    std::vector<VkPipeline> compose_pipelines;

    std::unique_ptr<vkb::core::Buffer>      instance_buffer;            // per-instance vertex data
    std::unique_ptr<vkb::core::Buffer>      staging_buffer;             // helper buffer for data uploads

    struct PerFrame
    {
        std::unique_ptr<vkb::core::Buffer>  uniform_buffer;
        std::unique_ptr<vkb::core::Buffer>  dispatch_buffer;
        std::unique_ptr<vkb::core::Buffer>  classification_buffer;
        std::unique_ptr<vkb::core::Buffer>  enqueue_scratch_buffer;     // for GPU dispatch

        VkDescriptorSet                     descriptor_set;
        VkDescriptorSet                     compose_descriptor_set;
        VkDescriptorSet                     classify_descriptor_set;
        VkFramebuffer                       framebuffer;
        VkFramebuffer                       gui_framebuffer;

        bool                                enqueue_scratch_buffer_ready;
    };
    std::vector<PerFrame> per_frame_data;

    void draw_gui() override;
    void prepare_render_context() override;
    void request_gpu_features(vkb::PhysicalDevice &gpu) override;

    void update_gui(float delta_time);
    void record_active_frame_commands(vkb::CommandBuffer& cb, float delta_time);
    void record_init_commands(vkb::CommandBuffer& cb);
    void record_scratch_buffer_reset(vkb::CommandBuffer& cb, PerFrame& frame_data);
    void prepare_resources();
    void create_gui_render_pass();

    VkExtent2D                          get_compute_tiles_extent();
    std::unique_ptr<vkb::RenderTarget>  create_render_target(vkb::core::Image &&swapchain_image);
    std::unique_ptr<vkb::sg::SubMesh>   load_model(const std::string& file, uint32_t index = 0);
    std::unique_ptr<vkb::sg::Image>     load_image(const std::string& file);
    std::unique_ptr<vkb::sg::Image>     convert_material_id_color_image(const vkb::sg::Image& material_color, uint32_t* out_num_colors);
    VkPipelineShaderStageCreateInfo     load_shader(const std::string& file, VkShaderStageFlagBits stage, const vkb::ShaderVariant& variant={});
    VkPipelineShaderStageCreateInfo     load_spv_shader(const std::string& file, VkShaderStageFlagBits stage);
    void                                draw_scene(vkb::CommandBuffer& cb, vkb::RenderTarget& rt, PerFrame& frame_data);
    void                                draw_model(std::unique_ptr<vkb::sg::SubMesh>& model, VkCommandBuffer cmd_buf);

    // To help index into render target's image views.
    enum MrtIndex : uint32_t
    {
        MRT_SWAPCHAIN = 0,
        MRT_DEPTH     = 1,
        MRT_MATERIAL  = 2,
        MRT_NORMAL    = 3,
        MRT_TEXCOORD  = 4,
    };

    bool is_material_map_scene() const
    {
        return (scene == SCENE_MATERIAL_MAP_1) || (scene == SCENE_MATERIAL_MAP_2);
    }

    uint32_t num_deferred_shader_permutations() const
    {
        if (is_material_map_scene())
        {
            return (1u << num_material_bits);
        }
        else
        {
	        // The number of shader permutations is determined by:
	        //   1 bit for the background
	        //   1 bit for each material used by the model
	        // From the resulting number subtract 1 for all zero bits case, which is illegal (we always draw something).
            return (1u << num_material_bits) - 1;
        }
    }
};

std::unique_ptr<vkb::VulkanSample> create_gpu_dispatch();
