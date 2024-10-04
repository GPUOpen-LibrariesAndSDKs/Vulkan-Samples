//  Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
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
#include "example.h"

// Not a part of vulkan_core.h yet, so include it separately.
#include "../gpu_dispatch/vk_amdx_shader_enqueue.h"

namespace GpuDrawDispatch
{

// Entrypoints of VK_AMDX_shader_enqueue
extern PFN_vkCreateExecutionGraphPipelinesAMDX           vkCreateExecutionGraphPipelinesAMDX;
extern PFN_vkGetExecutionGraphPipelineScratchSizeAMDX    vkGetExecutionGraphPipelineScratchSizeAMDX;
extern PFN_vkGetExecutionGraphPipelineNodeIndexAMDX      vkGetExecutionGraphPipelineNodeIndexAMDX;
extern PFN_vkCmdInitializeGraphScratchMemoryAMDX         vkCmdInitializeGraphScratchMemoryAMDX;
extern PFN_vkCmdDispatchGraphAMDX                        vkCmdDispatchGraphAMDX;
extern PFN_vkCmdDispatchGraphIndirectAMDX                vkCmdDispatchGraphIndirectAMDX;
extern PFN_vkCmdDispatchGraphIndirectCountAMDX           vkCmdDispatchGraphIndirectCountAMDX;

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 joint0;
    glm::vec4 weight0;
};

struct Instance
{
    glm::vec3 pos;
};

struct UniformBuffer
{
    glm::mat4 projection;
    glm::mat4 modelview;
    glm::mat4 inverseProjModelView;
    glm::vec4 lightPos;
};

// To help index into render target's image views.
enum MrtIndex : uint32_t
{
    MRT_SWAPCHAIN = 0,
    MRT_DEPTH     = 1,
};

struct Payload
{
    uint32_t    dispatch_grid[3];
    float       color[3];
};

class GpuDrawDispatch : public vkb::VulkanSample
{
  public:
    GpuDrawDispatch();

    std::unique_ptr<vkb::sg::SubMesh>   load_model(const std::string& file, uint32_t index = 0, bool use_indexed_draw = false, bool mesh_shader_buffer = false);
    VkPipelineShaderStageCreateInfo     load_shader(const std::string& file, VkShaderStageFlagBits stage);
    VkPipelineShaderStageCreateInfo     load_spv_shader(const std::string& file, VkShaderStageFlagBits stage);

    vkb::RenderContext& get_render_context()
    {
        return VulkanSample::get_render_context();
    }

    vkb::Device* get_device() const
    {
        return device.get();
    }

    uint32_t get_num_frames()
    {
        return vkb::to_u32(get_render_context().get_render_frames().size());
    }

    const VkPhysicalDeviceShaderEnqueuePropertiesAMDX& get_shader_enqueue_properties() const
    {
        return shader_enqueue_properties;
    }

    VkPipelineCache get_pipeline_cache() const
    {
        return pipeline_cache;
    }

protected:
    void finish() override;
    bool prepare(vkb::Platform& platform) override;
    void update(float delta_time) override;
    bool resize(uint32_t width, uint32_t height) override;
    void input_event(const vkb::InputEvent &input_event) override;
    void draw_gui() override;
    void prepare_render_context() override;
    void request_gpu_features(vkb::PhysicalDevice &gpu) override;

    std::unique_ptr<Example> example;

    // Keep the compiled modules around, to avoid glslang recompilation on resizes, etc.
    std::unordered_map<std::string, VkShaderModule> shader_module_cache;

    bool    resources_ready                 = false;
    bool    is_benchmarking                 = false;
    bool    is_stop_after                   = false;
    bool    is_shader_enqueue_supported     = false;
    bool    rotate_animation                = true;     // animate the model

    VkPhysicalDeviceShaderEnqueuePropertiesAMDX     shader_enqueue_properties = {};
    std::string                                     gui_message;

    VkPipelineCache     pipeline_cache     = VK_NULL_HANDLE;
    VkRenderPass        gui_render_pass    = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> per_frame_gui_framebuffer;

    void update_gui(float delta_time);
    void create_and_init_resources(vkb::CommandBuffer& cb);
    void record_frame_commands(vkb::CommandBuffer& cb, float delta_time);

    std::unique_ptr<vkb::RenderTarget>  create_render_target(vkb::core::Image &&swapchain_image);
};

}

std::unique_ptr<vkb::VulkanSample> create_gpu_draw_dispatch();
